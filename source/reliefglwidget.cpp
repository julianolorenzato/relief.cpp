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
    if (samplerLinearId) glDeleteSamplers(1, &samplerLinearId);
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
void ReliefGLWidget::setReliefType(int type)    { reliefType = type; update(); }
void ReliefGLWidget::setUseAtlas(bool enabled)  { useAtlas = enabled; update(); }
void ReliefGLWidget::setOffsetMapSamplingVersion(int v) { offsetMapSamplingVersion = v; update(); }
void ReliefGLWidget::setFilter0(bool enabled)   { filter0 = enabled; update(); }
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

    glGenSamplers(1, &samplerLinearId);
    glSamplerParameteri(samplerLinearId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glSamplerParameteri(samplerLinearId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(samplerLinearId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(samplerLinearId, GL_TEXTURE_WRAP_T, GL_REPEAT);

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

    shaderProgram.setUniformValue("ReliefType", reliefType);
    shaderProgram.setUniformValue("OffsetMapSamplingVersion", offsetMapSamplingVersion);
    shaderProgram.setUniformValue("UseAtlas", useAtlas);
    shaderProgram.setUniformValue("Filter0", filter0);
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
    glBindTexture(GL_TEXTURE_2D, reliefTex);
    glBindSampler(2, samplerLinearId);
    shaderProgram.setUniformValue("Relief_Map_Linear", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, offsetTex);
    glBindSampler(3, 0);
    shaderProgram.setUniformValue("Offset_Map", 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, normalTex);
    glBindSampler(4, 0);
    shaderProgram.setUniformValue("Normal_Map", 4);

    vao.bind();
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    vao.release();

    glBindSampler(1, 0);
    glBindSampler(2, 0);
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

void ReliefGLWidget::createShaderProgram() {
    const char* vertexShader = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec2 texCoord;
        layout(location = 3) in vec3 tangent;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        uniform vec3 viewPosWorld;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;
        out vec3 Tangent;
        out vec3 ViewDirTS;

        void main() {
            FragPos = vec3(model * vec4(position, 1.0));
            mat3 normalMat = mat3(transpose(inverse(model)));
            vec3 N = normalize(normalMat * normal);
            vec3 T = normalize(normalMat * tangent);
            T = normalize(T - N * dot(N, T));
            vec3 B = cross(N, T);

            mat3 worldToTangent = transpose(mat3(T, B, N));
            vec3 viewDirWorld = normalize(viewPosWorld - FragPos);
            ViewDirTS = worldToTangent * viewDirWorld;

            Normal = N;
            Tangent = T;
            TexCoord = texCoord;

            gl_Position = projection * view * vec4(FragPos, 1.0);
        }
    )";

    // GLSL port of RTMA_Functions.ush (Cone_Relief intentionally omitted — see
    // texture_prep.cpp, the Relief Map's B channel is reserved/unused).
    const char* fragmentShader = R"(
        #version 330 core
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        in vec3 Tangent;
        in vec3 ViewDirTS;
        out vec4 FragColor;

        uniform sampler2D Color_Map;
        uniform sampler2D Relief_Map;
        uniform sampler2D Relief_Map_Linear;
        uniform sampler2D Offset_Map;
        uniform sampler2D Normal_Map;

        uniform int   ReliefType;               // 0 Off, 1 Linear, 3 Mip
        uniform int   OffsetMapSamplingVersion;  // 1, 2, 3
        uniform bool  UseAtlas;
        uniform bool  Filter0;
        uniform int   LinearSteps;
        uniform int   BinarySteps;
        uniform float DepthScale;
        uniform float LastMip;
        uniform float TexelSize;
        uniform int   DebugView;                 // 0 Shaded, 1 Steps, 2 Leaps, 3 UV

        #define EPS 1e-5

        vec3 BoxIntersect(vec3 ray_offset, vec3 ray_direction, vec3 inverse_direction, vec3 aabbMin, vec3 aabbMax) {
            vec3 t0 = (aabbMin - ray_offset) * inverse_direction;
            vec3 t1 = (aabbMax - ray_offset) * inverse_direction;
            vec3 tmax = max(t0, t1);
            float t = min(tmax.x, min(tmax.y, tmax.z));
            return clamp(ray_offset + t * ray_direction, aabbMin, aabbMax);
        }

        vec4 SampleDepthMip(vec2 UV, float mip_level) {
            if (Filter0 && mip_level == 0.0)
                return textureLod(Relief_Map_Linear, UV, mip_level);
            return textureLod(Relief_Map, UV, mip_level);
        }

        // Equivalent to HLSL's mul(vec, RotationMatrix) with that function's row-major
        // rotation literal (verified by hand-expanding the row-vector multiply).
        vec2 rotateXY(vec2 v, float angle) {
            float c = cos(angle), s = sin(angle);
            return vec2(v.x * c + v.y * s, -v.x * s + v.y * c);
        }

        void PerformIslandLeap(
            vec4 OffsetData,
            inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
            inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped, inout int numLeaps) {
            float RotationAngle = OffsetData.z * 6.28319;
            leaping_point = Current_Position.xy;
            tangent_direction.xy = rotateXY(tangent_direction.xy, RotationAngle);
            Current_Position.xy += OffsetData.xy;
            invDir = 1.0 / tangent_direction;
            landing_point = Current_Position.xy;
            jumped = true;
            numLeaps += 1;
        }

        void Island_leap_V1(
            inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
            inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
            inout int OffsetMapTextureSamples, inout int numLeaps) {
            vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
            OffsetMapTextureSamples += 1;
            if (OffsetData.w > 0.0)
                PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
            else
                jumped = false;
        }

        void Island_leap_V2(
            vec4 DepthData,
            inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
            inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
            inout int OffsetMapTextureSamples, inout int numLeaps) {
            if (abs(DepthData.w) > 0.0) {
                vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
                OffsetMapTextureSamples += 1;
                if (OffsetData.w > 0.0)
                    PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
                else
                    jumped = false;
            }
        }

        void Island_leap_V3(
            vec4 DepthData,
            inout vec3 tangent_direction, inout vec3 Current_Position, inout vec3 invDir,
            inout vec2 leaping_point, inout vec2 landing_point, inout bool jumped,
            inout int OffsetMapTextureSamples, inout int DiscriminantDepthTextureSamples, inout int numLeaps) {
            if (abs(DepthData.w) > 0.0) {
                vec4 DepthDiscriminant = textureLod(Relief_Map, Current_Position.xy, 0.0);
                DiscriminantDepthTextureSamples += 1;
                if (DepthDiscriminant.w != 0.0) {
                    vec4 OffsetData = textureLod(Offset_Map, Current_Position.xy, 0.0);
                    OffsetMapTextureSamples += 1;
                    PerformIslandLeap(OffsetData, tangent_direction, Current_Position, invDir, leaping_point, landing_point, jumped, numLeaps);
                } else {
                    jumped = false;
                }
            }
        }

        vec3 Mip_Relief(
            vec3 Tangent_Direction, vec2 UV, int maxSteps, int OffsetMapSamplingVer,
            out int leapCounter, out int stepsOut) {
            float pixelSize = pow(2.0, -LastMip);
            float mip = 0.0;

            vec3 pos = vec3(UV, 0.0);
            vec3 invDir = 1.0 / Tangent_Direction;
            vec2 leapingPoint = vec2(0.0), landingPoint = vec2(0.0);
            bool jumped = false;

            int wallCount = 0, numLeaps = 0, offsetSamples = 0, discSamples = 0;
            int stepsTaken = 0;

            for (int i = 0; i < maxSteps; i++) {
                stepsTaken = i + 1;
                vec4 Depth = -SampleDepthMip(pos.xy, mip);

                if (UseAtlas) {
                    if (OffsetMapSamplingVer == 1)
                        Island_leap_V1(Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
                    else if (OffsetMapSamplingVer == 2)
                        Island_leap_V2(Depth, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
                    else if (OffsetMapSamplingVer == 3)
                        Island_leap_V3(Depth, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, discSamples, numLeaps);
                }

                vec3 aabbMin = vec3(floor(pos.xy / pixelSize) * pixelSize - EPS, Depth.y);
                vec3 aabbMax = vec3(aabbMin.xy + pixelSize + 2.0 * EPS, 0.0);

                if (pos.z > Depth.y + EPS)
                    pos = BoxIntersect(pos, Tangent_Direction, invDir, aabbMin, aabbMax);

                if (pos.z < Depth.y + EPS) {
                    if (Depth.x == Depth.y) break;
                    mip -= 1.0;
                    pixelSize *= 0.5;
                    wallCount = 0;
                } else {
                    wallCount++;
                    if (wallCount >= 3) {
                        mip += 1.0;
                        pixelSize *= 2.0;
                    }
                }
            }

            leapCounter = numLeaps;
            stepsOut = stepsTaken;
            return pos;
        }

        vec3 Linear_Relief(
            vec3 Tangent_Direction, vec2 UV, int maxSteps, int OffsetMapSamplingVer,
            out int leapCounter, out int stepsOut) {
            float sigma = 1.0 / float(maxSteps);
            vec3 pos = vec3(UV, 0.0);
            vec3 invDir = 1.0 / Tangent_Direction;
            vec2 leapingPoint = vec2(0.0), landingPoint = vec2(0.0);
            bool jumped = false;
            int numLeaps = 0, offsetSamples = 0, discSamples = 0;
            int stepsTaken = 0;

            for (int i = 0; i < maxSteps; i++) {
                stepsTaken = i + 1;
                vec4 DepthData = textureLod(Relief_Map, pos.xy, 0.0);

                if (UseAtlas) {
                    if (OffsetMapSamplingVer == 1)
                        Island_leap_V1(Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
                    else if (OffsetMapSamplingVer == 2)
                        Island_leap_V2(DepthData, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, numLeaps);
                    else if (OffsetMapSamplingVer == 3)
                        Island_leap_V3(DepthData, Tangent_Direction, pos, invDir, leapingPoint, landingPoint, jumped, offsetSamples, discSamples, numLeaps);
                }

                if (pos.z >= DepthData.x) break;
                pos.xy += sigma * Tangent_Direction.xy;
                pos.z  += sigma;
            }

            leapCounter = numLeaps;
            stepsOut = stepsTaken;
            return pos;
        }

        vec2 BinarySearch(vec3 Starting_Point, vec3 Tangent_Direction, int Binary_Search_Steps) {
            vec3 Current_Position = Starting_Point;
            vec3 New_Tangent_Direction = Tangent_Direction;
            for (int i = 0; i < Binary_Search_Steps; i++) {
                float height = textureLod(Relief_Map, Current_Position.xy, 0.0).x;
                Current_Position = (Current_Position.z < height)
                    ? Current_Position + New_Tangent_Direction
                    : Current_Position - New_Tangent_Direction;
                New_Tangent_Direction *= 0.5;
            }
            return Current_Position.xy;
        }

        vec2 ReliefMapping(vec3 Tangent_Direction, vec2 UV, out int leapCounter, out int stepsOut) {
            vec3 Tangent = Tangent_Direction;
            vec3 StartingPoint = vec3(UV, 0.0);
            int numLeaps = 0, numSteps = 0;

            if (ReliefType == 1) {
                StartingPoint = Linear_Relief(Tangent_Direction, UV, LinearSteps, OffsetMapSamplingVersion, numLeaps, numSteps);
            } else if (ReliefType == 3) {
                StartingPoint = Mip_Relief(Tangent_Direction, UV, LinearSteps, OffsetMapSamplingVersion, numLeaps, numSteps);
            }

            Tangent = Tangent * TexelSize;

            vec2 FinalUV = (ReliefType > 0) ? BinarySearch(StartingPoint, Tangent, BinarySteps) : UV;

            leapCounter = numLeaps;
            stepsOut = numSteps;
            return FinalUV;
        }

        void main() {
            vec3 N = normalize(Normal);
            vec3 T = normalize(Tangent - N * dot(N, Tangent));
            vec3 B = cross(N, T);
            mat3 TBN = mat3(T, B, N);

            vec2 finalUV = TexCoord;
            int leapCounter = 0, stepsTaken = 0;

            if (ReliefType != 0) {
                vec3 viewTS = normalize(ViewDirTS);
                vec3 tangentDir = vec3(-viewTS.xy / max(abs(viewTS.z), 1e-4) * DepthScale, 1.0);
                finalUV = ReliefMapping(tangentDir, TexCoord, leapCounter, stepsTaken);
            }

            if (DebugView == 1) { FragColor = vec4(vec3(float(stepsTaken) / float(max(LinearSteps, 1))), 1.0); return; }
            if (DebugView == 2) { FragColor = vec4(vec3(float(leapCounter) / 4.0), 1.0); return; }
            if (DebugView == 3) { FragColor = vec4(finalUV, 0.0, 1.0); return; }

            vec3 albedo = texture(Color_Map, finalUV).rgb;
            vec3 nTS = texture(Normal_Map, finalUV).rgb;
            vec3 shadingNormal = normalize(TBN * nTS);

            vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
            float diff = max(dot(shadingNormal, lightDir), 0.0);
            vec3 result = albedo * (0.3 + 0.7 * diff);
            FragColor = vec4(result, 1.0);
        }
    )";

    if (!shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShader)) {
        std::cerr << "Relief vertex shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)) {
        std::cerr << "Relief fragment shader error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
    if (!shaderProgram.link()) {
        std::cerr << "Relief shader linking error: " << shaderProgram.log().toStdString() << "\n";
        return;
    }
}
