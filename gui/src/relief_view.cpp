/**
 * @file relief_view.cpp
 * @brief ReliefView implementation: interleaved vertex/tangent buffer
 *        construction, GL texture upload, and the pixel-picking pass.
 */
#include "gui/relief_view.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <iostream>

// ─── Anonymous helpers ────────────────────────────────────────────────────────

namespace
{

    /// @brief Builds the interleaved [pos(3)|normal(3)|uv(2)|tangent(4, w=handedness)]
    ///        vertex buffer and triangle index buffer for `mesh`, computing
    ///        per-vertex normals and Gram-Schmidt-orthogonalized tangents
    ///        (with handedness resolved against the accumulated bitangent).
    /// @param[out] verts Interleaved vertex data, 12 floats per vertex.
    /// @param[out] idxs Triangle indices into `verts`.
    void buildMeshVerts(const QEMSimplifier *mesh,
                        std::vector<float> &verts,
                        std::vector<unsigned int> &idxs)
    {
        if (!mesh || mesh->vertices.empty())
            return;

        std::vector<Eigen::Vector3d> normals(mesh->vertices.size(), Eigen::Vector3d::Zero());
        std::vector<Eigen::Vector3d> tangents(mesh->vertices.size(), Eigen::Vector3d::Zero());
        std::vector<Eigen::Vector3d> bitangents(mesh->vertices.size(), Eigen::Vector3d::Zero());

        for (const auto &f : mesh->faces)
        {
            if (f.removed)
                continue;
            const Eigen::Vector3d &p0 = mesh->vertices[f.v[0]].pos;
            const Eigen::Vector3d &p1 = mesh->vertices[f.v[1]].pos;
            const Eigen::Vector3d &p2 = mesh->vertices[f.v[2]].pos;
            const Eigen::Vector2d &u0 = mesh->vertices[f.v[0]].uv;
            const Eigen::Vector2d &u1 = mesh->vertices[f.v[1]].uv;
            const Eigen::Vector2d &u2 = mesh->vertices[f.v[2]].uv;

            Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
            normals[f.v[0]] += n;
            normals[f.v[1]] += n;
            normals[f.v[2]] += n;

            Eigen::Vector3d e1 = p1 - p0, e2 = p2 - p0;
            Eigen::Vector2d d1 = u1 - u0, d2 = u2 - u0;
            double det = d1.x() * d2.y() - d2.x() * d1.y();
            if (std::abs(det) > 1e-12)
            {
                double r = 1.0 / det;
                Eigen::Vector3d T = r * (d2.y() * e1 - d1.y() * e2);
                Eigen::Vector3d B = r * (d1.x() * e2 - d2.x() * e1);
                tangents[f.v[0]] += T;
                tangents[f.v[1]] += T;
                tangents[f.v[2]] += T;
                bitangents[f.v[0]] += B;
                bitangents[f.v[1]] += B;
                bitangents[f.v[2]] += B;
            }
        }

        for (auto &n : normals)
            n = n.normalized();
        // Per-vertex handedness (Lengyel): the accumulated tangent only fixes
        // T up to sign — cross(N, T) in the shader needs to know whether the
        // real bitangent (from the UV gradient) agrees with that cross product
        // or points the opposite way, otherwise the "V" axis of the tangent
        // frame silently flips for meshes whose UV winding differs (e.g. an
        // OBJ mesh, whose V axis is flipped on load vs. glTF's).
        std::vector<double> handedness(mesh->vertices.size(), 1.0);
        for (size_t i = 0; i < tangents.size(); i++)
        {
            const Eigen::Vector3d &n = normals[i];
            Eigen::Vector3d t = tangents[i] - n * n.dot(tangents[i]);
            double len = t.norm();
            t = len > 1e-8 ? t / len : Eigen::Vector3d(1.0, 0.0, 0.0);
            tangents[i] = t;
            handedness[i] = (n.cross(t).dot(bitangents[i]) < 0.0) ? -1.0 : 1.0;
        }

        std::vector<int> remap(mesh->vertices.size(), -1);
        int cnt = 0;
        for (size_t i = 0; i < mesh->vertices.size(); i++)
        {
            if (mesh->vertices[i].removed)
                continue;
            remap[i] = cnt++;
            const auto &v = mesh->vertices[i];
            const auto &n = normals[i];
            const auto &t = tangents[i];
            verts.push_back((float)v.pos.x());
            verts.push_back((float)v.pos.y());
            verts.push_back((float)v.pos.z());
            verts.push_back((float)n.x());
            verts.push_back((float)n.y());
            verts.push_back((float)n.z());
            verts.push_back((float)v.uv.x());
            verts.push_back((float)v.uv.y());
            verts.push_back((float)t.x());
            verts.push_back((float)t.y());
            verts.push_back((float)t.z());
            verts.push_back((float)handedness[i]);
        }
        for (const auto &f : mesh->faces)
        {
            if (f.removed)
                continue;
            idxs.push_back(remap[f.v[0]]);
            idxs.push_back(remap[f.v[1]]);
            idxs.push_back(remap[f.v[2]]);
        }
    }

} // namespace

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ReliefView::ReliefView(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ReliefView::~ReliefView()
{
    makeCurrent();
    if (this->vbo.isCreated())
        this->vbo.destroy();
    if (this->ebo.isCreated())
        this->ebo.destroy();
    if (this->vao.isCreated())
        this->vao.destroy();
    deleteTextures();
    deletePickFbo();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void ReliefView::setMesh(const QEMSimplifier *mesh)
{
    this->mesh = mesh;
    update();
    resetCamera();
}

void ReliefView::setColorMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->colorTex, pyr, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    doneCurrent();
    update();
}

void ReliefView::setReliefMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->reliefTex, pyr, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::NearestMipMapNearest, QOpenGLTexture::Nearest);
    doneCurrent();
    update();
}

void ReliefView::setNormalMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->normalTex, pyr, QOpenGLTexture::RGB32F, QOpenGLTexture::RGB,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    doneCurrent();
    update();
}

void ReliefView::setOffsetMap(const MipPyramid& off)
{
    makeCurrent();
    uploadTexture(this->offsetTex, off, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
    doneCurrent();
    update();
}

void ReliefView::resetCamera()
{
    this->rotX = 0.f;
    this->rotY = 0.f;
    this->zoom = 3.f;
    update();
    emit cameraChanged(this->rotX, this->rotY, this->zoom);
}

void ReliefView::syncCamera(float rotX, float rotY, float zoom)
{
    this->rotX = rotX;
    this->rotY = rotY;
    this->zoom = zoom;
    update();
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void ReliefView::setReliefEnabled(bool v)
{
    this->reliefEnabled = v;
    update();
}
void ReliefView::setUseAtlas(bool v)
{
    this->useAtlas = v;
    update();
}
void ReliefView::setReliefTextureType(int v)
{
    this->reliefTextureType = v;
    update();
}
void ReliefView::setSteps(int v)
{
    this->steps = std::max(1, v);
    update();
}
void ReliefView::setDepthScale(double v)
{
    this->depthScale = (float)v;
    update();
}
void ReliefView::setDebugView(int v)
{
    this->debugView = v;
    update();
}
void ReliefView::setWireframe(bool v)
{
    this->wireframe = v;
    update();
}
void ReliefView::setCullFace(bool v)
{
    this->cullFace = v;
    update();
}

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void ReliefView::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/relief.vert"))
        std::cerr << "ReliefView vert error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/relief.frag"))
        std::cerr << "ReliefView frag error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.link())
        std::cerr << "ReliefView link error: " << this->prog.log().toStdString() << "\n";

}

void ReliefView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void ReliefView::paintGL()
{
    if (this->mesh && !this->vao.isCreated())
    {
        buildMeshBuffers();
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!this->vao.isCreated() || this->indexCount == 0)
        return;

    if (!hasTextures())
    {
        QPainter p(this);
        p.setPen(QColor(160, 160, 160));
        p.setFont(QFont("sans-serif", 14));
        p.drawText(rect(), Qt::AlignCenter,
                   "Run Textures Preparation first to view relief mapping.");
        p.end();
        return;
    }

    if (this->cullFace)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    glPolygonMode(GL_FRONT_AND_BACK, this->wireframe ? GL_LINE : GL_FILL);

    this->prog.bind();
    this->prog.setUniformValue("projection", projMatrix());
    this->prog.setUniformValue("view", viewMatrix());
    this->prog.setUniformValue("model", modelMatrix());

    float radX = qDegreesToRadians(this->rotX);
    float radY = qDegreesToRadians(this->rotY);
    QVector3D camPos(
        this->zoom * sinf(radY) * cosf(radX),
        this->zoom * sinf(radX),
        this->zoom * cosf(radY) * cosf(radX));
    this->prog.setUniformValue("viewPosWorld", camPos);

    this->prog.setUniformValue("ReliefEnabled", this->reliefEnabled);
    this->prog.setUniformValue("UseAtlas", this->useAtlas);
    this->prog.setUniformValue("ReliefTextureType", this->reliefTextureType);
    this->prog.setUniformValue("LinearSteps", this->steps);
    this->prog.setUniformValue("DepthScale", this->depthScale);
    float lastMip = std::log2((float)std::max(1, this->reliefTex->width()));
    this->prog.setUniformValue("LastMip", lastMip);
    this->prog.setUniformValue("DebugView", this->debugView);

    this->colorTex->bind(0);
    this->prog.setUniformValue("Color_Map", 0);
    this->reliefTex->bind(1);
    this->prog.setUniformValue("Relief_Map", 1);
    this->offsetTex->bind(2);
    this->prog.setUniformValue("Offset_Map", 2);
    this->normalTex->bind(3);
    this->prog.setUniformValue("Normal_Map", 3);

    this->vao.bind();
    glDrawElements(GL_TRIANGLES, this->indexCount, GL_UNSIGNED_INT, nullptr);
    this->vao.release();

    this->prog.release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    if (this->pickPending)
    {
        this->pickPending = false;
        performPick(this->pickPos);
    }
}

// ─── Pixel picking ────────────────────────────────────────────────────────────

void ReliefView::ensurePickFbo(int w, int h)
{
    if (this->pickFbo != 0 && this->pickFboW == w && this->pickFboH == h)
        return;

    deletePickFbo();
    if (w <= 0 || h <= 0)
        return;

    glGenTextures(1, &this->pickColorTex);
    glBindTexture(GL_TEXTURE_2D, this->pickColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenRenderbuffers(1, &this->pickDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, this->pickDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glGenFramebuffers(1, &this->pickFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, this->pickFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->pickColorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->pickDepthRbo);
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    this->pickFboW = w;
    this->pickFboH = h;
}

void ReliefView::deletePickFbo()
{
    if (this->pickFbo)
        glDeleteFramebuffers(1, &this->pickFbo);
    if (this->pickColorTex)
        glDeleteTextures(1, &this->pickColorTex);
    if (this->pickDepthRbo)
        glDeleteRenderbuffers(1, &this->pickDepthRbo);
    this->pickFbo = this->pickColorTex = this->pickDepthRbo = 0;
    this->pickFboW = this->pickFboH = 0;
}

// Re-renders the same frame into an offscreen float FBO with DebugView
// forced to 3 (UV output — see relief.frag's main()), then reads back the
// single clicked texel. Never touches the default framebuffer, so the
// visible frame is unaffected. Alpha (always 1 where relief.frag writes a
// fragment, 0 where the FBO was only cleared) distinguishes a real hit from
// a click that missed all geometry.
void ReliefView::performPick(const QPoint &widgetPos)
{
    if (!this->vao.isCreated() || this->indexCount == 0 || !hasTextures())
        return;

    int w = width(), h = height();
    ensurePickFbo(w, h);
    if (this->pickFbo == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, this->pickFbo);
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (this->cullFace)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    this->prog.bind();
    this->prog.setUniformValue("projection", projMatrix());
    this->prog.setUniformValue("view", viewMatrix());
    this->prog.setUniformValue("model", modelMatrix());

    float radX = qDegreesToRadians(this->rotX);
    float radY = qDegreesToRadians(this->rotY);
    QVector3D camPos(
        this->zoom * sinf(radY) * cosf(radX),
        this->zoom * sinf(radX),
        this->zoom * cosf(radY) * cosf(radX));
    this->prog.setUniformValue("viewPosWorld", camPos);

    this->prog.setUniformValue("ReliefEnabled", this->reliefEnabled);
    this->prog.setUniformValue("UseAtlas", this->useAtlas);
    this->prog.setUniformValue("ReliefTextureType", this->reliefTextureType);
    this->prog.setUniformValue("LinearSteps", this->steps);
    this->prog.setUniformValue("DepthScale", this->depthScale);
    float lastMip = std::log2((float)std::max(1, this->reliefTex->width()));
    this->prog.setUniformValue("LastMip", lastMip);
    this->prog.setUniformValue("DebugView", 3);

    this->colorTex->bind(0);
    this->prog.setUniformValue("Color_Map", 0);
    this->reliefTex->bind(1);
    this->prog.setUniformValue("Relief_Map", 1);
    this->offsetTex->bind(2);
    this->prog.setUniformValue("Offset_Map", 2);
    this->normalTex->bind(3);
    this->prog.setUniformValue("Normal_Map", 3);

    this->vao.bind();
    glDrawElements(GL_TRIANGLES, this->indexCount, GL_UNSIGNED_INT, nullptr);
    this->vao.release();
    this->prog.release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    float pixel[4] = {0.f, 0.f, 0.f, 0.f};
    int px = widgetPos.x();
    int py = h - 1 - widgetPos.y(); // GL reads bottom-up, Qt widget coords are top-down
    if (px >= 0 && px < w && py >= 0 && py < h)
        glReadPixels(px, py, 1, 1, GL_RGBA, GL_FLOAT, pixel);

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, w, h);
    glClearColor(0.12f, 0.12f, 0.12f, 1.0f);

    emit pixelPicked(QPointF(pixel[0], pixel[1]), pixel[3] > 0.5f);
}

// ─── Buffer / texture helpers ─────────────────────────────────────────────────

void ReliefView::buildMeshBuffers()
{
    if (!this->mesh || this->mesh->vertices.empty())
        return;

    Eigen::Vector3d bmin(1e18, 1e18, 1e18);
    Eigen::Vector3d bmax(-1e18, -1e18, -1e18);
    for (const auto &v : this->mesh->vertices)
    {
        if (v.removed)
            continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radius = (bmax - bmin).norm() * 0.5;
    if (radius < 1e-9)
        radius = 1.0;
    this->meshCenter = QVector3D((float)center.x(), (float)center.y(), (float)center.z());
    this->meshNormScale = 1.0f / (float)radius;

    std::vector<float> verts;
    std::vector<unsigned int> idxs;
    buildMeshVerts(this->mesh, verts, idxs);

    this->indexCount = (int)idxs.size();
    if (!this->vao.isCreated())
        this->vao.create();
    if (!this->vbo.isCreated())
        this->vbo.create();
    if (!this->ebo.isCreated())
        this->ebo.create();

    this->vao.bind();
    this->vbo.bind();
    this->vbo.allocate(verts.data(), (int)(verts.size() * sizeof(float)));
    this->ebo.bind();
    this->ebo.allocate(idxs.data(), (int)(idxs.size() * sizeof(unsigned int)));

    constexpr GLsizei stride = 12 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));

    this->vao.release();
}

void ReliefView::uploadTexture(QOpenGLTexture *&tex, const MipPyramid &pyr,
                                QOpenGLTexture::TextureFormat format,
                                QOpenGLTexture::PixelFormat pixelFormat,
                                QOpenGLTexture::Filter minFilter,
                                QOpenGLTexture::Filter magFilter)
{
    delete tex;
    tex = nullptr;
    if (pyr.mips.empty())
        return;

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

void ReliefView::deleteTextures()
{
    delete colorTex;  colorTex  = nullptr;
    delete reliefTex; reliefTex = nullptr;
    delete normalTex; normalTex = nullptr;
    delete offsetTex; offsetTex = nullptr;
}

// ─── Camera matrices ──────────────────────────────────────────────────────────

QMatrix4x4 ReliefView::projMatrix() const
{
    QMatrix4x4 m;
    m.perspective(45.0f, (float)width() / std::max(1, height()), 0.1f, 100.0f);
    return m;
}

QMatrix4x4 ReliefView::viewMatrix() const
{
    float radX = qDegreesToRadians(this->rotX);
    float radY = qDegreesToRadians(this->rotY);
    QVector3D pos(
        this->zoom * sinf(radY) * cosf(radX),
        this->zoom * sinf(radX),
        this->zoom * cosf(radY) * cosf(radX));
    QMatrix4x4 m;
    m.lookAt(pos, QVector3D(0.f, 0.f, 0.f), QVector3D(0.f, 1.f, 0.f));
    return m;
}

QMatrix4x4 ReliefView::modelMatrix() const
{
    QMatrix4x4 m;
    m.scale(this->meshNormScale);
    m.translate(-this->meshCenter);
    return m;
}

// ─── Mouse / camera ───────────────────────────────────────────────────────────

void ReliefView::mousePressEvent(QMouseEvent *e)
{
    this->lastMouse = e->pos();
    this->pressPos = e->pos();
}

void ReliefView::mouseReleaseEvent(QMouseEvent *e)
{
    // A left click that didn't turn into a drag (camera rotation) is
    // treated as a pick request instead.
    if (e->button() == Qt::LeftButton && (e->pos() - this->pressPos).manhattanLength() < 4)
    {
        this->pickPos = e->pos();
        this->pickPending = true;
        update();
    }
}

void ReliefView::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton))
        return;
    int dx = e->pos().x() - this->lastMouse.x();
    int dy = e->pos().y() - this->lastMouse.y();
    this->rotY += dx * 0.5f;
    this->rotX += dy * 0.5f;
    this->lastMouse = e->pos();
    update();
    emit cameraChanged(this->rotX, this->rotY, this->zoom);
}

void ReliefView::wheelEvent(QWheelEvent *e)
{
    this->zoom -= e->angleDelta().y() * 0.001f;
    this->zoom = std::clamp(this->zoom, 0.1f, 20.0f);
    update();
    emit cameraChanged(this->rotX, this->rotY, this->zoom);
}
