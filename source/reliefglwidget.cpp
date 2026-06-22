#include "reliefglwidget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

ReliefGLWidget::ReliefGLWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

ReliefGLWidget::~ReliefGLWidget() {
    makeCurrent();
    if (vbo.isCreated()) vbo.destroy();
    if (ebo.isCreated()) ebo.destroy();
    if (vao.isCreated()) vao.destroy();
    deleteTextures();
    if (samplerPointId)  glDeleteSamplers(1, &samplerPointId);
    doneCurrent();
}

void ReliefGLWidget::deleteTextures() {
    if (colorTex)  { glDeleteTextures(1, &colorTex);  colorTex  = 0; }
    if (reliefTex) { glDeleteTextures(1, &reliefTex); reliefTex = 0; }
    if (normalTex) { glDeleteTextures(1, &normalTex); normalTex = 0; }
    if (offsetTex) { glDeleteTextures(1, &offsetTex); offsetTex = 0; }
}

// ─── Mesh upload ───────────────────────────────────────────────────────────

void ReliefGLWidget::setMesh(const QEMSimplifier* m) {
    mesh = m;
    meshDirty = true;
    resetCamera();
}

void ReliefGLWidget::updateMeshBuffers() {
    if (!mesh || mesh->vertices.empty()) return;

    Eigen::Vector3d bmin(1e18, 1e18, 1e18), bmax(-1e18, -1e18, -1e18);
    for (const auto& v : mesh->vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radius = (bmax - bmin).norm() * 0.5;
    if (radius < 1e-9) radius = 1.0;
    meshCenter    = glm::vec3((float)center.x(), (float)center.y(), (float)center.z());
    meshNormScale = 1.0f / (float)radius;

    // Per-vertex normals (face-area weighted sum) and tangents (from UV deltas),
    // Gram-Schmidt orthogonalized against the normal. No bitangent-handedness
    // detection — fine for atlases without mirrored UV islands.
    std::vector<Eigen::Vector3d> normals(mesh->vertices.size(), Eigen::Vector3d::Zero());
    std::vector<Eigen::Vector3d> tangents(mesh->vertices.size(), Eigen::Vector3d::Zero());

    for (const auto& face : mesh->faces) {
        if (face.removed) continue;
        const Eigen::Vector3d& p0 = mesh->vertices[face.v[0]].pos;
        const Eigen::Vector3d& p1 = mesh->vertices[face.v[1]].pos;
        const Eigen::Vector3d& p2 = mesh->vertices[face.v[2]].pos;
        const Eigen::Vector2d& uv0 = mesh->vertices[face.v[0]].uv;
        const Eigen::Vector2d& uv1 = mesh->vertices[face.v[1]].uv;
        const Eigen::Vector2d& uv2 = mesh->vertices[face.v[2]].uv;

        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        normals[face.v[0]] += n;
        normals[face.v[1]] += n;
        normals[face.v[2]] += n;

        Eigen::Vector3d e1 = p1 - p0, e2 = p2 - p0;
        Eigen::Vector2d duv1 = uv1 - uv0, duv2 = uv2 - uv0;
        double det = duv1.x() * duv2.y() - duv2.x() * duv1.y();
        if (std::abs(det) > 1e-12) {
            double r = 1.0 / det;
            Eigen::Vector3d tangent = r * (duv2.y() * e1 - duv1.y() * e2);
            tangents[face.v[0]] += tangent;
            tangents[face.v[1]] += tangent;
            tangents[face.v[2]] += tangent;
        }
    }

    for (auto& n : normals) n = n.normalized();
    for (size_t i = 0; i < tangents.size(); i++) {
        const Eigen::Vector3d& n = normals[i];
        Eigen::Vector3d t = tangents[i] - n * n.dot(tangents[i]);
        double len = t.norm();
        tangents[i] = (len > 1e-8) ? (t / len) : Eigen::Vector3d(1.0, 0.0, 0.0);
    }

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    std::vector<int> remap(mesh->vertices.size(), -1);
    int newVertexCount = 0;
    for (size_t i = 0; i < mesh->vertices.size(); i++) {
        if (mesh->vertices[i].removed) continue;
        remap[i] = newVertexCount++;
        const auto& v = mesh->vertices[i];
        const auto& n = normals[i];
        const auto& t = tangents[i];
        vertices.push_back(v.pos.x());
        vertices.push_back(v.pos.y());
        vertices.push_back(v.pos.z());
        vertices.push_back(n.x());
        vertices.push_back(n.y());
        vertices.push_back(n.z());
        vertices.push_back((float)v.uv.x());
        vertices.push_back((float)v.uv.y());
        vertices.push_back(t.x());
        vertices.push_back(t.y());
        vertices.push_back(t.z());
    }

    for (const auto& face : mesh->faces) {
        if (face.removed) continue;
        indices.push_back(remap[face.v[0]]);
        indices.push_back(remap[face.v[1]]);
        indices.push_back(remap[face.v[2]]);
    }

    indexCount = (int)indices.size();

    if (!vao.isCreated()) vao.create();
    if (!vbo.isCreated()) vbo.create();
    if (!ebo.isCreated()) ebo.create();

    vao.bind();

    vbo.bind();
    vbo.allocate(vertices.data(), (int)(vertices.size() * sizeof(float)));

    ebo.bind();
    ebo.allocate(indices.data(), (int)(indices.size() * sizeof(unsigned int)));

    constexpr int stride = 11 * sizeof(float);
    shaderProgram.enableAttributeArray(0);
    shaderProgram.setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    shaderProgram.enableAttributeArray(1);
    shaderProgram.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, stride);
    shaderProgram.enableAttributeArray(2);
    shaderProgram.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 2, stride);
    shaderProgram.enableAttributeArray(3);
    shaderProgram.setAttributeBuffer(3, GL_FLOAT, 8 * sizeof(float), 3, stride);

    vao.release();
}

// ─── Texture upload ────────────────────────────────────────────────────────

void ReliefGLWidget::uploadPyramid(GLuint& texId, const MipPyramid& pyr) {
    if (texId) { glDeleteTextures(1, &texId); texId = 0; }
    if (pyr.mips.empty()) return;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    GLenum internalFormat = pyr.channels == 3 ? GL_RGB32F : GL_RGBA32F;
    GLenum format         = pyr.channels == 3 ? GL_RGB    : GL_RGBA;

    int w = pyr.width, h = pyr.height;
    for (int level = 0; level < pyr.levelCount(); level++) {
        glTexImage2D(GL_TEXTURE_2D, level, internalFormat, w, h, 0, format, GL_FLOAT, pyr.mips[level].data());
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, pyr.levelCount() - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ReliefGLWidget::uploadOffsetMap(GLuint& texId, const OffsetMapResult& off) {
    if (texId) { glDeleteTextures(1, &texId); texId = 0; }
    if (off.data.empty()) return;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, off.width, off.height, 0, GL_RGBA, GL_FLOAT, off.data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ReliefGLWidget::setTextures(const TexturePrepResult& result) {
    pendingTextures = &result;
    texturesDirty = true;

    int res = std::max(1, result.reliefMap.width);
    lastMip   = std::log2((float)res);
    texelSize = 1.0f / (float)res;

    update();
}

// ─── Slots ─────────────────────────────────────────────────────────────────

void ReliefGLWidget::setWireframe(bool enabled) { wireframe = enabled; update(); }
void ReliefGLWidget::setCullFace(bool enabled)  { cullFace  = enabled; update(); }
void ReliefGLWidget::setReliefEnabled(bool enabled) { reliefEnabled = enabled; update(); }
void ReliefGLWidget::setUseAtlas(bool enabled)  { useAtlas = enabled; update(); }
void ReliefGLWidget::setSteps(int s)            { steps = std::max(1, s); update(); }
void ReliefGLWidget::setBinarySteps(int s)      { binarySteps = std::max(0, s); update(); }
void ReliefGLWidget::setDepthScale(double scale) { depthScale = (float)scale; update(); }
void ReliefGLWidget::setDebugView(int mode)     { debugView = mode; update(); }

// ─── GL lifecycle ──────────────────────────────────────────────────────────

void ReliefGLWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    createShaderProgram();

    glGenSamplers(1, &samplerPointId);
    glSamplerParameteri(samplerPointId, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(samplerPointId, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(samplerPointId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(samplerPointId, GL_TEXTURE_WRAP_T, GL_REPEAT);

    resetCamera();
}

void ReliefGLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ReliefGLWidget::paintGL() {
    if (meshDirty) {
        updateMeshBuffers();
        meshDirty = false;
    }
    if (texturesDirty && pendingTextures) {
        uploadPyramid(colorTex, pendingTextures->colorMap);
        uploadPyramid(reliefTex, pendingTextures->reliefMap);
        uploadPyramid(normalTex, pendingTextures->normalMap);
        uploadOffsetMap(offsetTex, pendingTextures->offsetMap);
        texturesDirty = false;
        pendingTextures = nullptr;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!mesh || !vao.isCreated() || indexCount == 0 || !hasTextures()) return;

    if (cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    shaderProgram.bind();

    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), (float)width() / std::max(1, height()), 0.1f, 100.0f);
    shaderProgram.setUniformValue("projection", QMatrix4x4(glm::value_ptr(projection)).transposed());

    glm::vec3 camPos(
        zoom * sinf(glm::radians(rotationY)) * cosf(glm::radians(rotationX)),
        zoom * sinf(glm::radians(rotationX)),
        zoom * cosf(glm::radians(rotationY)) * cosf(glm::radians(rotationX)));
    glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    shaderProgram.setUniformValue("view", QMatrix4x4(glm::value_ptr(view)).transposed());
    shaderProgram.setUniformValue("viewPosWorld", QVector3D(camPos.x, camPos.y, camPos.z));

    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(meshNormScale))
                    * glm::translate(glm::mat4(1.0f), -meshCenter);
    shaderProgram.setUniformValue("model", QMatrix4x4(glm::value_ptr(model)).transposed());

    shaderProgram.setUniformValue("ReliefEnabled", reliefEnabled);
    shaderProgram.setUniformValue("UseAtlas", useAtlas);
    shaderProgram.setUniformValue("LinearSteps", steps);
    shaderProgram.setUniformValue("BinarySteps", binarySteps);
    shaderProgram.setUniformValue("DepthScale", depthScale);
    shaderProgram.setUniformValue("LastMip", lastMip);
    shaderProgram.setUniformValue("TexelSize", texelSize);
    shaderProgram.setUniformValue("DebugView", debugView);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glBindSampler(0, 0);
    shaderProgram.setUniformValue("Color_Map", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, reliefTex);
    glBindSampler(1, samplerPointId);
    shaderProgram.setUniformValue("Relief_Map", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, offsetTex);
    glBindSampler(2, 0);
    shaderProgram.setUniformValue("Offset_Map", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, normalTex);
    glBindSampler(3, 0);
    shaderProgram.setUniformValue("Normal_Map", 3);

    vao.bind();
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    vao.release();

    glBindSampler(1, 0);
    shaderProgram.release();
}

void ReliefGLWidget::resetCamera() {
    rotationX = 0.0f;
    rotationY = 0.0f;
    zoom = 3.0f;
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

void ReliefGLWidget::syncCamera(float rotX, float rotY, float z) {
    rotationX = rotX;
    rotationY = rotY;
    zoom = z;
    update();
}

void ReliefGLWidget::mousePressEvent(QMouseEvent* event) {
    lastMousePos = event->pos();
}

void ReliefGLWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int dx = event->pos().x() - lastMousePos.x();
    int dy = event->pos().y() - lastMousePos.y();
    rotationY += dx * 0.5f;
    rotationX += dy * 0.5f;
    lastMousePos = event->pos();
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

void ReliefGLWidget::wheelEvent(QWheelEvent* event) {
    zoom -= event->angleDelta().y() * 0.001f;
    zoom = std::max(0.1f, std::min(zoom, 20.0f));
    update();
    emit cameraChanged(rotationX, rotationY, zoom);
}

// ─── Shaders ───────────────────────────────────────────────────────────────
// Sources live in source/shaders/relief.{vert,frag} (bundled via shaders.qrc).
// The fragment shader's Mip relief path is a GLSL port of relief.ush's
// CleanerRelief; see its header comment for details.

void ReliefGLWidget::createShaderProgram() {
    if (!shaderProgram.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/relief.vert")) {
        std::cerr << "Relief vertex shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!shaderProgram.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/relief.frag")) {
        std::cerr << "Relief fragment shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!shaderProgram.link()) {
        std::cerr << "Relief shader linking error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
}
