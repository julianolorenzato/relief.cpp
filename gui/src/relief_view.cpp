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

    void buildMeshVerts(const QEMSimplifier *mesh,
                        std::vector<float> &verts,
                        std::vector<unsigned int> &idxs)
    {
        if (!mesh || mesh->vertices.empty())
            return;

        std::vector<Eigen::Vector3d> normals(mesh->vertices.size(), Eigen::Vector3d::Zero());
        std::vector<Eigen::Vector3d> tangents(mesh->vertices.size(), Eigen::Vector3d::Zero());

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
                tangents[f.v[0]] += T;
                tangents[f.v[1]] += T;
                tangents[f.v[2]] += T;
            }
        }

        for (auto &n : normals)
            n = n.normalized();
        for (size_t i = 0; i < tangents.size(); i++)
        {
            const Eigen::Vector3d &n = normals[i];
            Eigen::Vector3d t = tangents[i] - n * n.dot(tangents[i]);
            double len = t.norm();
            tangents[i] = len > 1e-8 ? t / len : Eigen::Vector3d(1.0, 0.0, 0.0);
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
    if (this->samplerPoint)
        glDeleteSamplers(1, &this->samplerPoint);
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
    uploadPyramid(this->colorTex, pyr);
    doneCurrent();
    update();
}

void ReliefView::setReliefMap(const MipPyramid& pyr)
{
    int res = std::max(1, pyr.width);
    this->lastMip   = std::log2((float)res);
    this->texelSize = 1.0f / (float)res;
    makeCurrent();
    uploadPyramid(this->reliefTex, pyr);
    doneCurrent();
    update();
}

void ReliefView::setNormalMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadPyramid(this->normalTex, pyr);
    doneCurrent();
    update();
}

void ReliefView::setOffsetMap(const OffsetMapResult& off)
{
    makeCurrent();
    uploadOffsetMap(this->offsetTex, off);
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

    glGenSamplers(1, &this->samplerPoint);
    glSamplerParameteri(this->samplerPoint, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(this->samplerPoint, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(this->samplerPoint, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(this->samplerPoint, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
    this->prog.setUniformValue("LastMip", this->lastMip);
    this->prog.setUniformValue("TexelSize", this->texelSize);
    this->prog.setUniformValue("DebugView", this->debugView);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->colorTex);
    glBindSampler(0, 0);
    this->prog.setUniformValue("Color_Map", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, this->reliefTex);
    glBindSampler(1, this->samplerPoint);
    this->prog.setUniformValue("Relief_Map", 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, this->offsetTex);
    glBindSampler(2, 0);
    this->prog.setUniformValue("Offset_Map", 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, this->normalTex);
    glBindSampler(3, 0);
    this->prog.setUniformValue("Normal_Map", 3);

    this->vao.bind();
    glDrawElements(GL_TRIANGLES, this->indexCount, GL_UNSIGNED_INT, nullptr);
    this->vao.release();

    glBindSampler(1, 0);
    this->prog.release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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

    constexpr GLsizei stride = 11 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void *)(8 * sizeof(float)));

    this->vao.release();
}

void ReliefView::uploadPyramid(GLuint &texId, const MipPyramid &pyr)
{
    if (texId)
    {
        glDeleteTextures(1, &texId);
        texId = 0;
    }
    if (pyr.mips.empty())
        return;

    GLenum internalFmt = pyr.channels == 3 ? GL_RGB32F : GL_RGBA32F;
    GLenum extFmt = pyr.channels == 3 ? GL_RGB : GL_RGBA;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    int w = pyr.width, h = pyr.height;
    for (int lvl = 0; lvl < pyr.levelCount(); lvl++)
    {
        glTexImage2D(GL_TEXTURE_2D, lvl, internalFmt, w, h, 0, extFmt, GL_FLOAT, pyr.mips[lvl].data());
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

void ReliefView::uploadOffsetMap(GLuint &texId, const OffsetMapResult &off)
{
    if (texId)
    {
        glDeleteTextures(1, &texId);
        texId = 0;
    }
    if (off.data.empty())
        return;

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, off.width, off.height,
                 0, GL_RGBA, GL_FLOAT, off.data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ReliefView::deleteTextures()
{
    if (this->colorTex)
    {
        glDeleteTextures(1, &this->colorTex);
        this->colorTex = 0;
    }
    if (this->reliefTex)
    {
        glDeleteTextures(1, &this->reliefTex);
        this->reliefTex = 0;
    }
    if (this->normalTex)
    {
        glDeleteTextures(1, &this->normalTex);
        this->normalTex = 0;
    }
    if (this->offsetTex)
    {
        glDeleteTextures(1, &this->offsetTex);
        this->offsetTex = 0;
    }
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
