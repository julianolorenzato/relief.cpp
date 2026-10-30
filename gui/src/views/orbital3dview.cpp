/**
 * @file orbital3dview.cpp
 * @brief Orbital3DView implementation: camera, mesh buffer construction, and the
 *        light orb glyph shared by every orbital viewport.
 */
#include "gui/views/orbital3dview.h"
#include "gui/geometry.h"
#include "gui/gl_format.h"
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

// ─── Constructor / destructor ─────────────────────────────────────────────────

Orbital3DView::Orbital3DView(const QString& title, QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    titleLabel_ = new QLabel(title.toUpper(), this);
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLabel_->setFixedHeight(28);
    titleLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLabel_->setStyleSheet(
        "QLabel {"
        "  background: rgba(20, 20, 20, 210);"
        "  color: #d0d0d0;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "  letter-spacing: 1px;"
        "  padding: 0 8px;"
        "}"
    );
    titleLabel_->setGeometry(0, 0, width(), titleLabel_->height());
}

Orbital3DView::~Orbital3DView() {
    makeCurrent();
    if (meshVbo_.isCreated()) meshVbo_.destroy();
    if (meshEbo_.isCreated()) meshEbo_.destroy();
    if (meshVao_.isCreated()) meshVao_.destroy();
    if (orbVbo_.isCreated())  orbVbo_.destroy();
    if (orbEbo_.isCreated())  orbEbo_.destroy();
    if (orbVao_.isCreated())  orbVao_.destroy();
    if (haloVbo_.isCreated()) haloVbo_.destroy();
    if (haloEbo_.isCreated()) haloEbo_.destroy();
    if (haloVao_.isCreated()) haloVao_.destroy();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void Orbital3DView::setMesh(const mesh::Mesh* mesh) {
    mesh_      = mesh;
    meshDirty_ = true;
    updateStatsLabel();
    update();
    resetCamera();
}

void Orbital3DView::updateStatsLabel() {
    if (!mesh_) {
        if (statsLabel_) statsLabel_->hide();
        return;
    }

    if (!statsLabel_) {
        statsLabel_ = new QLabel(this);
        statsLabel_->setAlignment(Qt::AlignCenter);
        statsLabel_->setFixedHeight(22);
        statsLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        statsLabel_->setStyleSheet(
            "QLabel {"
            "  background: rgba(20, 20, 20, 180);"
            "  color: #a0a0a0;"
            "  font-size: 10px;"
            "  padding: 0 8px;"
            "}"
        );
    }
    statsLabel_->setText(QString("%1 faces  ·  %2 vertices")
                              .arg(mesh_->faceCount())
                              .arg(mesh_->vertexCount()));
    statsLabel_->setGeometry(0, height() - statsLabel_->height(), width(), statsLabel_->height());
    statsLabel_->raise();
    statsLabel_->show();
}

void Orbital3DView::resizeEvent(QResizeEvent* e) {
    QOpenGLWidget::resizeEvent(e);
    if (statsLabel_) statsLabel_->setGeometry(0, height() - statsLabel_->height(), width(), statsLabel_->height());
    if (titleLabel_) titleLabel_->setGeometry(0, 0, width(), titleLabel_->height());
}

void Orbital3DView::resetCamera() {
    rotX_ = 0.f; rotY_ = 0.f; zoom_ = 3.f;
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}

void Orbital3DView::syncCamera(float rotX, float rotY, float zoom) {
    rotX_ = rotX; rotY_ = rotY; zoom_ = zoom;
    update();
    // No re-emit to avoid feedback loops
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void Orbital3DView::setWireframe(bool v) { wireframe_ = v; update(); }
void Orbital3DView::setCullFace(bool v)  { cullFace_  = v; update(); }

void Orbital3DView::setLightX(double v) { lightPos_.x = (float)v; update(); }
void Orbital3DView::setLightY(double v) { lightPos_.y = (float)v; update(); }
void Orbital3DView::setLightZ(double v) { lightPos_.z = (float)v; update(); }

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void Orbital3DView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    initPrograms();
    initLightPrograms();
    buildOrbMesh();
    buildHaloMesh();
}

void Orbital3DView::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void Orbital3DView::paintGL() {
    // Deferred GL uploads (safe: we are now inside a valid GL context)
    if (mesh_ && meshDirty_) {
        if (buildMeshBuffers())
            onMeshChanged();
        meshDirty_ = false;
    }
    prepareFrame();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!meshVao_.isCreated() || meshIndexCount_ == 0) return;

    if (!isReady()) {
        QPainter p(this);
        p.setPen(QColor(160, 160, 160));
        p.setFont(QFont("sans-serif", 14));
        p.drawText(rect(), Qt::AlignCenter, notReadyMessage());
        p.end();
        return;
    }

    paintScene();
    drawLightOrb();
    afterFrame();
}

// ─── Buffer builders ──────────────────────────────────────────────────────────

bool Orbital3DView::buildMeshBuffers() {
    if (!mesh_) return false;
    if (mesh_->vertices().empty()) {
        meshIndexCount_ = 0;  // nothing to draw; drop the previous mesh
        return false;
    }

    // The mesh's own bounds, e.g. the box that baked planes project onto.
    Eigen::Vector3d bmin( 1e18,  1e18,  1e18);
    Eigen::Vector3d bmax(-1e18, -1e18, -1e18);
    for (const auto& v : mesh_->vertices()) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    meshBoundsMin_ = glm::vec3((float)bmin.x(), (float)bmin.y(), (float)bmin.z());
    meshBoundsMax_ = glm::vec3((float)bmax.x(), (float)bmax.y(), (float)bmax.z());

    // Center and scale for the model matrix, from the mesh's bounding sphere.
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radius = (bmax - bmin).norm() * 0.5;
    if (radius < 1e-9) radius = 1.0;
    meshCenter_    = glm::vec3((float)center.x(), (float)center.y(), (float)center.z());
    meshNormScale_ = 1.0f / (float)radius;

    buildVertexArray(mesh_, meshVbo_, meshEbo_, meshVao_, meshIndexCount_);
    return true;
}

void Orbital3DView::buildVertexArray(const mesh::Mesh* mesh, QOpenGLBuffer& vbo, QOpenGLBuffer& ebo,
                                     QOpenGLVertexArrayObject& vao, int& indexCount)
{
    // Interleave [pos(3) | normal(3) | uv(2) | tangent(4, w = handedness)] per vertex.
    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    if (mesh && !mesh->vertices().empty()) {
        const mesh::Mesh::GPUMesh gpu = mesh->explodeForGPU();
        verts.reserve(gpu.positions.size() * 12);
        for (size_t i = 0; i < gpu.positions.size(); i++) {
            const auto& p = gpu.positions[i];
            const auto& n = gpu.normals[i];
            const auto& uv = gpu.uvs[i];
            const auto& t = gpu.tangents[i];
            verts.push_back((float)p.x());  verts.push_back((float)p.y());  verts.push_back((float)p.z());
            verts.push_back((float)n.x());  verts.push_back((float)n.y());  verts.push_back((float)n.z());
            verts.push_back((float)uv.x()); verts.push_back((float)uv.y());
            verts.push_back((float)t.x());  verts.push_back((float)t.y());  verts.push_back((float)t.z());
            verts.push_back((float)t.w());
        }
        idxs.assign(gpu.indices.begin(), gpu.indices.end());
    }

    indexCount = (int)idxs.size();
    if (!vao.isCreated()) vao.create();
    if (!vbo.isCreated()) vbo.create();
    if (!ebo.isCreated()) ebo.create();

    vao.bind();
    vbo.bind();
    vbo.allocate(verts.data(), (int)(verts.size() * sizeof(float)));
    ebo.bind();
    ebo.allocate(idxs.data(), (int)(idxs.size() * sizeof(unsigned int)));

    constexpr GLsizei stride = 12 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));

    vao.release();
}

void Orbital3DView::uploadTexture(QOpenGLTexture*& tex, const textures::MipPyramid& pyr,
                                  QOpenGLTexture::TextureFormat format,
                                  QOpenGLTexture::PixelFormat pixelFormat,
                                  QOpenGLTexture::Filter minFilter,
                                  QOpenGLTexture::Filter magFilter)
{
    delete tex;
    tex = nullptr;
    if (pyr.mips.empty()) return;

    tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    tex->setFormat(format);
    tex->setSize(pyr.width, pyr.height);
    tex->setMipLevels(pyr.levelCount());
    tex->allocateStorage();
    for (int lvl = 0; lvl < pyr.levelCount(); lvl++)
        tex->setData(lvl, pixelFormat, QOpenGLTexture::Float32, pyr.mips[lvl].data());
    tex->setMinificationFilter(minFilter);
    tex->setMagnificationFilter(magFilter);
    tex->setWrapMode(QOpenGLTexture::Repeat);
}

void Orbital3DView::uploadTexture(QOpenGLTexture*& tex, const tex::Texture& t,
                                  QOpenGLTexture::Filter minFilter,
                                  QOpenGLTexture::Filter magFilter)
{
    delete tex;
    tex = nullptr;
    if (!t.valid()) return;

    QOpenGLPixelTransferOptions opts;
    opts.setAlignment(1);

    tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    tex->setFormat(static_cast<QOpenGLTexture::TextureFormat>(glfmt::internalFormat(t.format)));
    tex->setSize(t.width(), t.height());
    tex->setMipLevels(t.mipCount());
    tex->allocateStorage();
    for (int lvl = 0; lvl < t.mipCount(); lvl++)
        tex->setData(lvl, static_cast<QOpenGLTexture::PixelFormat>(glfmt::pixelFormat(t.format)),
                     static_cast<QOpenGLTexture::PixelType>(glfmt::pixelType(t.format)),
                     t.mips[lvl].pixels.data(), &opts);
    tex->setMinificationFilter(minFilter);
    tex->setMagnificationFilter(magFilter);
    tex->setWrapMode(QOpenGLTexture::Repeat);
}

// ─── Light orb ────────────────────────────────────────────────────────────────

void Orbital3DView::buildProgram(QOpenGLShaderProgram& prog, const char* name,
                                 const QString& vertPath, const QString& fragPath)
{
    if (!prog.addShaderFromSourceFile(QOpenGLShader::Vertex, vertPath))
        std::cerr << name << " vert error: " << prog.log().toStdString() << "\n";
    if (!prog.addShaderFromSourceFile(QOpenGLShader::Fragment, fragPath))
        std::cerr << name << " frag error: " << prog.log().toStdString() << "\n";
    if (!prog.link())
        std::cerr << name << " link error: " << prog.log().toStdString() << "\n";
}

void Orbital3DView::initLightPrograms() {
    buildProgram(orbProg_,  "orb",  ":/shaders/orb.vert",  ":/shaders/orb.frag");
    buildProgram(haloProg_, "halo", ":/shaders/halo.vert", ":/shaders/halo.frag");
}

void Orbital3DView::buildOrbMesh() {
    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    geometry::buildSphereVerts(12, 8, verts, idxs);

    orbIndexCount_ = (int)idxs.size();
    if (!orbVao_.isCreated()) orbVao_.create();
    if (!orbVbo_.isCreated()) orbVbo_.create();
    if (!orbEbo_.isCreated()) orbEbo_.create();

    orbVao_.bind();
    orbVbo_.bind();
    orbVbo_.allocate(verts.data(), (int)(verts.size() * sizeof(float)));
    orbEbo_.bind();
    orbEbo_.allocate(idxs.data(), (int)(idxs.size() * sizeof(unsigned int)));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    orbVao_.release();
}

void Orbital3DView::buildHaloMesh() {
    float quad[8] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f,
    };
    unsigned int idxs[6] = {0, 1, 2, 2, 3, 0};

    haloIndexCount_ = 6;
    if (!haloVao_.isCreated()) haloVao_.create();
    if (!haloVbo_.isCreated()) haloVbo_.create();
    if (!haloEbo_.isCreated()) haloEbo_.create();

    haloVao_.bind();
    haloVbo_.bind();
    haloVbo_.allocate(quad, sizeof(quad));
    haloEbo_.bind();
    haloEbo_.allocate(idxs, sizeof(idxs));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    haloVao_.release();
}

void Orbital3DView::drawLightOrb() {
    if (!orbVao_.isCreated() || orbIndexCount_ == 0) return;

    const QVector3D light(lightPos_.x, lightPos_.y, lightPos_.z);
    const QMatrix4x4 proj = toQt(projMatrix());
    const QMatrix4x4 view = toQt(viewMatrix());
    const glm::vec3 cam = cameraPosition();

    // Light orb glyph: small unlit sphere at the light's world position,
    // drawn after the shaded mesh so it isn't affected by wireframe/relief
    // toggles.
    constexpr float kOrbRadius = 0.06f;
    QMatrix4x4 orbModel;
    orbModel.translate(light);
    orbModel.scale(kOrbRadius);

    // Sphere and halo are small, camera-facing-ish shapes drawn from
    // whichever side the camera happens to be on; cull state is
    // irrelevant to the rest of the frame since nothing draws after them
    // but the (separate) pick pass, which sets its own state.
    glDisable(GL_CULL_FACE);

    orbProg_.bind();
    orbProg_.setUniformValue("projection", proj);
    orbProg_.setUniformValue("view", view);
    orbProg_.setUniformValue("model", orbModel);
    orbProg_.setUniformValue("viewPosWorld", QVector3D(cam.x, cam.y, cam.z));
    orbProg_.setUniformValue("Color", QVector3D(1.0f, 1.0f, 1.0f));

    orbVao_.bind();
    glDrawElements(GL_TRIANGLES, orbIndexCount_, GL_UNSIGNED_INT, nullptr);
    orbVao_.release();
    orbProg_.release();

    // Soft glow halo: an additively-blended, camera-facing billboard
    // centered on the orb, built directly from the view matrix's
    // inverse so it always faces the camera regardless of orbit angle.
    if (!haloVao_.isCreated() || haloIndexCount_ == 0) return;

    QMatrix4x4 viewInv = view.inverted();
    QVector3D camRight(viewInv(0, 0), viewInv(1, 0), viewInv(2, 0));
    QVector3D camUp(viewInv(0, 1), viewInv(1, 1), viewInv(2, 1));
    constexpr float kHaloRadius = kOrbRadius * 3.5f;

    QMatrix4x4 haloModel;
    haloModel.setColumn(0, QVector4D(camRight * kHaloRadius, 0.0f));
    haloModel.setColumn(1, QVector4D(camUp * kHaloRadius, 0.0f));
    haloModel.setColumn(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    haloModel.setColumn(3, QVector4D(light, 1.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    haloProg_.bind();
    haloProg_.setUniformValue("projection", proj);
    haloProg_.setUniformValue("view", view);
    haloProg_.setUniformValue("model", haloModel);
    haloProg_.setUniformValue("Color", QVector3D(1.0f, 0.85f, 0.5f));

    haloVao_.bind();
    glDrawElements(GL_TRIANGLES, haloIndexCount_, GL_UNSIGNED_INT, nullptr);
    haloVao_.release();
    haloProg_.release();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ─── Camera matrices ──────────────────────────────────────────────────────────

QMatrix4x4 Orbital3DView::toQt(const glm::mat4& m) {
    return QMatrix4x4(glm::value_ptr(m)).transposed();
}

glm::vec3 Orbital3DView::cameraPosition() const {
    return glm::vec3(
        zoom_ * sinf(glm::radians(rotY_)) * cosf(glm::radians(rotX_)),
        zoom_ * sinf(glm::radians(rotX_)),
        zoom_ * cosf(glm::radians(rotY_)) * cosf(glm::radians(rotX_)));
}

glm::mat4 Orbital3DView::projMatrix() const {
    return glm::perspective(glm::radians(45.0f),
                            (float)width() / std::max(1, height()),
                            0.1f, 100.0f);
}

glm::mat4 Orbital3DView::viewMatrix() const {
    return glm::lookAt(cameraPosition(), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Orbital3DView::modelMatrix() const {
    return glm::scale(glm::mat4(1.f), glm::vec3(meshNormScale_))
         * glm::translate(glm::mat4(1.f), -meshCenter_);
}

// ─── Mouse / camera ───────────────────────────────────────────────────────────

void Orbital3DView::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
}

void Orbital3DView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int dx = event->pos().x() - lastMouse_.x();
    int dy = event->pos().y() - lastMouse_.y();
    rotY_ += dx * 0.5f;
    rotX_ += dy * 0.5f;
    lastMouse_ = event->pos();
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}

void Orbital3DView::wheelEvent(QWheelEvent* event) {
    zoom_ -= event->angleDelta().y() * 0.001f;
    zoom_ = std::clamp(zoom_, 0.1f, 20.0f);
    update();
    emit cameraChanged(rotX_, rotY_, zoom_);
}
