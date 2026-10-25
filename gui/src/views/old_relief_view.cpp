/**
 * @file old_relief_view.cpp
 * @brief OldReliefView implementation: relief map construction and upload, and
 *        the pixel-picking pass.
 */
#include "gui/views/old_relief_view.h"
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace mesh;
using namespace textures;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

OldReliefView::OldReliefView(QWidget *parent)
    : Orbital3DView(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

OldReliefView::~OldReliefView()
{
    makeCurrent();
    deleteTextures();
    deletePickFbo();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void OldReliefView::buildMapsFromMesh()
{
    const mesh::Texture<uint8_t> &color = this->mesh_->colorTexture();
    const mesh::Texture<uint8_t> &normal = this->mesh_->normalTexture();
    const mesh::Texture<uint8_t> &height = this->mesh_->heightTexture();

    // The maps are float pyramids padded to a square power of two, so their size grows
    // fast (an 8192 map is ~1 GB per texture and fails to allocate): cap it, and let
    // larger sources be resampled down.
    constexpr int kMaxMapRes = 2048;
    const int kRes = std::min(
        kMaxMapRes,
        textures::nextPowerOfTwo(std::max(color.data.empty() ? height.width : color.width,
                                          color.data.empty() ? height.height : color.height)));

    // Without a color texture, fall back to a flat gray.
    static const uint8_t kGray[4] = {128, 128, 128, 255};
    const textures::RawImage rawColor =
        color.data.empty() ? textures::RawImage{kGray, 1, 1, 4}
                           : textures::RawImage{color.data.data(), color.width, color.height, 4};
    const textures::RawImage rawHeight{height.data.data(), height.width, height.height, 4};

    const MipPyramid colorMap = textures::buildColorMap(rawColor, kRes, kRes);
    const MipPyramid normalMap =
        normal.data.empty()
            ? textures::buildNormalMapFromHeight(rawHeight, kRes, kRes, 0.1f, 1.0f)
            : textures::buildNormalMap({normal.data.data(), normal.width, normal.height, 4}, kRes, kRes);
    const MipPyramid offsetMap = this->mesh_->buildOffsetMap(kRes, kRes, 16);
    const MipPyramid reliefMap = textures::buildReliefMap(rawHeight, kRes, kRes, offsetMap);

    uploadTexture(this->colorTex, colorMap, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    uploadTexture(this->reliefTex, reliefMap, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::NearestMipMapNearest, QOpenGLTexture::Nearest);
    uploadTexture(this->normalTex, normalMap, QOpenGLTexture::RGB32F, QOpenGLTexture::RGB,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    uploadTexture(this->offsetTex, offsetMap, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
}

QOpenGLShaderProgram *OldReliefView::bindSurfaceProgram(int debugView)
{
    const glm::vec3 cam = cameraPosition();

    QOpenGLShaderProgram &p = this->prog;
    p.bind();
    p.setUniformValue("projection", toQt(projMatrix()));
    p.setUniformValue("view", toQt(viewMatrix()));
    p.setUniformValue("model", toQt(modelMatrix()));
    p.setUniformValue("viewPosWorld", QVector3D(cam.x, cam.y, cam.z));
    p.setUniformValue("LightPosWorld", QVector3D(lightPos_.x, lightPos_.y, lightPos_.z));
    p.setUniformValue("ReliefEnabled", this->reliefEnabled);
    p.setUniformValue("LinearSteps", this->steps);
    p.setUniformValue("DebugView", debugView);

    this->colorTex->bind(0);
    p.setUniformValue("Color_Map", 0);

    p.setUniformValue("UseAtlas", this->useAtlas);
    p.setUniformValue("ReliefTextureType", this->reliefTextureType);
    p.setUniformValue("DepthScale", this->depthScale);
    float lastMip = std::log2((float)std::max(1, this->reliefTex->width()));
    p.setUniformValue("LastMip", lastMip);
    this->reliefTex->bind(1);
    p.setUniformValue("Relief_Map", 1);
    this->offsetTex->bind(2);
    p.setUniformValue("Offset_Map", 2);
    this->normalTex->bind(3);
    p.setUniformValue("Normal_Map", 3);
    return &p;
}

void OldReliefView::setColorMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->colorTex, pyr, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    doneCurrent();
    update();
}

void OldReliefView::setReliefMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->reliefTex, pyr, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::NearestMipMapNearest, QOpenGLTexture::Nearest);
    doneCurrent();
    update();
}

void OldReliefView::setNormalMap(const MipPyramid& pyr)
{
    makeCurrent();
    uploadTexture(this->normalTex, pyr, QOpenGLTexture::RGB32F, QOpenGLTexture::RGB,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    doneCurrent();
    update();
}

void OldReliefView::setOffsetMap(const MipPyramid& off)
{
    makeCurrent();
    uploadTexture(this->offsetTex, off, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
    doneCurrent();
    update();
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void OldReliefView::setReliefEnabled(bool v)
{
    this->reliefEnabled = v;
    update();
}
void OldReliefView::setUseAtlas(bool v)
{
    this->useAtlas = v;
    update();
}
void OldReliefView::setReliefTextureType(int v)
{
    this->reliefTextureType = v;
    update();
}
void OldReliefView::setSteps(int v)
{
    this->steps = std::max(1, v);
    update();
}
void OldReliefView::setDepthScale(double v)
{
    this->depthScale = (float)v;
    update();
}
void OldReliefView::setDebugView(int v)
{
    this->debugView = v;
    update();
}

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void OldReliefView::initPrograms()
{
    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/old_relief.vert"))
        std::cerr << "OldReliefView vert error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/old_relief.frag"))
        std::cerr << "OldReliefView frag error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.link())
        std::cerr << "OldReliefView link error: " << this->prog.log().toStdString() << "\n";
}

void OldReliefView::onMeshChanged()
{
    if (!this->mesh_->heightTexture().data.empty())
        buildMapsFromMesh();
}

QString OldReliefView::notReadyMessage() const
{
    return "Run Textures Preparation first to view relief mapping.";
}

void OldReliefView::paintScene()
{
    if (this->cullFace_)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    glPolygonMode(GL_FRONT_AND_BACK, this->wireframe_ ? GL_LINE : GL_FILL);

    QOpenGLShaderProgram *surfaceProg = bindSurfaceProgram(this->debugView);

    this->meshVao_.bind();
    glDrawElements(GL_TRIANGLES, this->meshIndexCount_, GL_UNSIGNED_INT, nullptr);
    this->meshVao_.release();

    surfaceProg->release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OldReliefView::afterFrame()
{
    if (this->pickPending)
    {
        this->pickPending = false;
        performPick(this->pickPos);
    }
}

// ─── Pixel picking ────────────────────────────────────────────────────────────

void OldReliefView::ensurePickFbo(int w, int h)
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

void OldReliefView::deletePickFbo()
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
// forced to 3 (UV output — see old_relief.frag's main()), then reads back the
// single clicked texel. Never touches the default framebuffer, so the
// visible frame is unaffected. Alpha (always 1 where old_relief.frag writes a
// fragment, 0 where the FBO was only cleared) distinguishes a real hit from
// a click that missed all geometry.
void OldReliefView::performPick(const QPoint &widgetPos)
{
    if (!this->meshVao_.isCreated() || this->meshIndexCount_ == 0 || !hasTextures())
        return;

    int w = width(), h = height();
    ensurePickFbo(w, h);
    if (this->pickFbo == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, this->pickFbo);
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (this->cullFace_)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    QOpenGLShaderProgram *surfaceProg = bindSurfaceProgram(3);

    this->meshVao_.bind();
    glDrawElements(GL_TRIANGLES, this->meshIndexCount_, GL_UNSIGNED_INT, nullptr);
    this->meshVao_.release();
    surfaceProg->release();
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

// ─── Texture helpers ──────────────────────────────────────────────────────────

void OldReliefView::deleteTextures()
{
    delete colorTex;  colorTex  = nullptr;
    delete reliefTex; reliefTex = nullptr;
    delete normalTex; normalTex = nullptr;
    delete offsetTex; offsetTex = nullptr;
}

// ─── Mouse / camera ───────────────────────────────────────────────────────────

void OldReliefView::mousePressEvent(QMouseEvent *e)
{
    Orbital3DView::mousePressEvent(e);
    this->pressPos = e->pos();
}

void OldReliefView::mouseReleaseEvent(QMouseEvent *e)
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
