#include "overlayglwidget.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <QMatrix4x4>
#include <QVector3D>
#include <vector>
#include <cmath>

OverlayGLWidget::OverlayGLWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
}

OverlayGLWidget::~OverlayGLWidget() {
    makeCurrent();
    origBuf.vbo.destroy(); origBuf.ebo.destroy(); origBuf.vao.destroy();
    simpBuf.vbo.destroy(); simpBuf.ebo.destroy(); simpBuf.vao.destroy();
    doneCurrent();
}

void OverlayGLWidget::createShader() {
    const char* vert = R"(
        #version 330 core
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        out vec3 fragNormal;
        void main() {
            fragNormal = mat3(transpose(inverse(model))) * normal;
            gl_Position = projection * view * model * vec4(position, 1.0);
        }
    )";
    const char* frag = R"(
        #version 330 core
        in vec3 fragNormal;
        uniform vec3 meshColor;
        out vec4 FragColor;
        void main() {
            vec3 n = normalize(fragNormal);
            float diff = max(dot(n, normalize(vec3(1.0, 1.0, 1.0))), 0.0);
            FragColor = vec4(meshColor * (0.25 + 0.75 * diff), 1.0);
        }
    )";
    shader.addShaderFromSourceCode(QOpenGLShader::Vertex, vert);
    shader.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
    shader.link();
}

void OverlayGLWidget::uploadMesh(const QEMSimplifier* m, MeshBuf& buf) {
    if (!m || m->vertices.empty()) return;

    // Per-vertex smooth normals
    std::vector<Eigen::Vector3d> normals(m->vertices.size(), Eigen::Vector3d::Zero());
    for (const auto& f : m->faces) {
        if (f.removed) continue;
        const auto& p0 = m->vertices[f.v[0]].pos;
        const auto& p1 = m->vertices[f.v[1]].pos;
        const auto& p2 = m->vertices[f.v[2]].pos;
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        normals[f.v[0]] += n; normals[f.v[1]] += n; normals[f.v[2]] += n;
    }
    for (auto& n : normals) n.normalize();

    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    std::vector<int> remap(m->vertices.size(), -1);
    int cnt = 0;

    for (size_t i = 0; i < m->vertices.size(); i++) {
        if (m->vertices[i].removed) continue;
        remap[i] = cnt++;
        const auto& p = m->vertices[i].pos;
        const auto& n = normals[i];
        verts.insert(verts.end(), {(float)p.x(), (float)p.y(), (float)p.z(),
                                   (float)n.x(), (float)n.y(), (float)n.z()});
    }
    for (const auto& f : m->faces) {
        if (f.removed) continue;
        idxs.push_back(remap[f.v[0]]);
        idxs.push_back(remap[f.v[1]]);
        idxs.push_back(remap[f.v[2]]);
    }

    buf.indexCount = (int)idxs.size();

    if (!buf.vao.isCreated()) buf.vao.create();
    if (!buf.vbo.isCreated()) buf.vbo.create();
    if (!buf.ebo.isCreated()) buf.ebo.create();

    buf.vao.bind();
    buf.vbo.bind();
    buf.vbo.allocate(verts.data(), (int)(verts.size() * sizeof(float)));
    buf.ebo.bind();
    buf.ebo.allocate(idxs.data(), (int)(idxs.size() * sizeof(unsigned int)));
    shader.enableAttributeArray(0);
    shader.setAttributeBuffer(0, GL_FLOAT, 0,               3, 6 * sizeof(float));
    shader.enableAttributeArray(1);
    shader.setAttributeBuffer(1, GL_FLOAT, 3*sizeof(float), 3, 6 * sizeof(float));
    buf.vao.release();
}

void OverlayGLWidget::setMeshes(const QEMSimplifier* original, const QEMSimplifier* simplified) {
    origMesh = original;
    simpMesh = simplified;

    // Compute scene normalization from original mesh bounding box
    if (original && !original->vertices.empty()) {
        Eigen::Vector3d bmin( 1e18,  1e18,  1e18);
        Eigen::Vector3d bmax(-1e18, -1e18, -1e18);
        for (const auto& v : original->vertices) {
            if (v.removed) continue;
            bmin = bmin.cwiseMin(v.pos);
            bmax = bmax.cwiseMax(v.pos);
        }
        Eigen::Vector3d c = (bmin + bmax) * 0.5;
        double r = (bmax - bmin).norm() * 0.5;
        if (r < 1e-9) r = 1.0;
        sceneCenter = {(float)c.x(), (float)c.y(), (float)c.z()};
        sceneScale  = 1.f / (float)r;
    }

    makeCurrent();
    shader.bind();
    uploadMesh(original,   origBuf);
    uploadMesh(simplified, simpBuf);
    shader.release();
    doneCurrent();
    update();
}

void OverlayGLWidget::updateSimplifiedMesh() {
    makeCurrent();
    shader.bind();
    uploadMesh(simpMesh, simpBuf);
    shader.release();
    doneCurrent();
    update();
}

void OverlayGLWidget::drawBuf(MeshBuf& buf, const QVector3D& color, bool wire) {
    if (!buf.vao.isCreated() || buf.indexCount == 0) return;
    shader.setUniformValue("meshColor", color);
    glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
    buf.vao.bind();
    glDrawElements(GL_TRIANGLES, buf.indexCount, GL_UNSIGNED_INT, nullptr);
    buf.vao.release();
}

void OverlayGLWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.1f, 1.f);
    glEnable(GL_DEPTH_TEST);
    createShader();
}

void OverlayGLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void OverlayGLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (origBuf.indexCount == 0) return;

    glm::mat4 proj = glm::perspective(
        glm::radians(45.f),
        (float)width() / std::max(1, height()),
        0.1f, 100.f);

    glm::vec3 eye(
        zoom * sinf(glm::radians(rotY)) * cosf(glm::radians(rotX)),
        zoom * sinf(glm::radians(rotX)),
        zoom * cosf(glm::radians(rotY)) * cosf(glm::radians(rotX)));
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

    glm::mat4 model = glm::scale(glm::mat4(1.f), glm::vec3(sceneScale))
                    * glm::translate(glm::mat4(1.f), -sceneCenter);

    shader.bind();
    shader.setUniformValue("projection", QMatrix4x4(glm::value_ptr(proj)).transposed());
    shader.setUniformValue("view",       QMatrix4x4(glm::value_ptr(view)).transposed());
    shader.setUniformValue("model",      QMatrix4x4(glm::value_ptr(model)).transposed());

    glEnable(GL_CULL_FACE);
    drawBuf(origBuf, {0.35f, 0.55f, 0.95f}, false);
    drawBuf(simpBuf, {0.95f, 0.50f, 0.10f}, false);

    shader.release();
}

void OverlayGLWidget::resetCamera() {
    rotX = 0.f; rotY = 0.f; zoom = 3.f;
    update();
    emit cameraChanged(rotX, rotY, zoom);
}

void OverlayGLWidget::syncCamera(float rx, float ry, float z) {
    rotX = rx; rotY = ry; zoom = z;
    update();
}

void OverlayGLWidget::mousePressEvent(QMouseEvent* e) {
    lastMouse = e->pos();
}

void OverlayGLWidget::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    rotY += (e->pos().x() - lastMouse.x()) * 0.5f;
    rotX += (e->pos().y() - lastMouse.y()) * 0.5f;
    lastMouse = e->pos();
    update();
    emit cameraChanged(rotX, rotY, zoom);
}

void OverlayGLWidget::wheelEvent(QWheelEvent* e) {
    zoom -= e->angleDelta().y() * 0.001f;
    zoom = std::max(0.1f, std::min(zoom, 20.f));
    update();
    emit cameraChanged(rotX, rotY, zoom);
}
