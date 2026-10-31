/**
 * @file relief_view.cpp
 * @brief ReliefView implementation: baked plane array construction and upload.
 */
#include "gui/views/relief_view.h"
#include "gui/gl_format.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace mesh;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ReliefView::~ReliefView()
{
    makeCurrent();
    deleteTextures();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void ReliefView::uploadColorTexture()
{
    const auto &color = this->mesh_->colorTexture();

    // The relief is baked from the mesh's color texture; there is no fallback.
    if (!color || !color->valid())
        throw std::runtime_error("ReliefView: mesh has no valid color texture");

    delete this->colorTex;
    this->colorTex = nullptr;

    QOpenGLPixelTransferOptions opts;
    opts.setAlignment(1);

    this->colorTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    this->colorTex->setFormat(static_cast<QOpenGLTexture::TextureFormat>(glfmt::internalFormat(color->format)));
    this->colorTex->setSize(color->width(), color->height());
    this->colorTex->setMipLevels(color->mipCount());
    this->colorTex->allocateStorage();
    for (int level = 0; level < color->mipCount(); level++)
        this->colorTex->setData(level, static_cast<QOpenGLTexture::PixelFormat>(glfmt::pixelFormat(color->format)),
                                static_cast<QOpenGLTexture::PixelType>(glfmt::pixelType(color->format)),
                                color->mips[level].pixels.data(), &opts);
    this->colorTex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    this->colorTex->setMagnificationFilter(QOpenGLTexture::Linear);
    this->colorTex->setWrapMode(QOpenGLTexture::Repeat);
}

void ReliefView::uploadPlanesTextures()
{
    const auto &planes = this->mesh_->planes();

    delete this->planesTex;
    this->planesTex = nullptr;
    delete this->planeNormalsTex;
    this->planeNormalsTex = nullptr;
    if (!planes[0])
        return;
    const int w = planes[0]->width(), h = planes[0]->height();
    if (w <= 0 || h <= 0)
        return;

    // A single level, nearest filtering: the mask and depth must not be blended across the silhouette.
    this->planesTex = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
    this->planesTex->setFormat(QOpenGLTexture::RGBA16_UNorm);
    this->planesTex->setSize(w, h);
    this->planesTex->setLayers(6);
    this->planesTex->setMipLevels(1);
    this->planesTex->allocateStorage();
    for (int layer = 0; layer < 6; layer++)
    {
        if (!planes[layer] || planes[layer]->width() != w || planes[layer]->height() != h)
            continue;
        this->planesTex->setData(0, layer, QOpenGLTexture::RGBA, QOpenGLTexture::UInt16,
                                 reinterpret_cast<const uint16_t *>(planes[layer]->data()));
    }
    this->planesTex->setMinificationFilter(QOpenGLTexture::Nearest);
    this->planesTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    this->planesTex->setWrapMode(QOpenGLTexture::ClampToEdge);

    // Smooth normals: same single level, nearest filtering.
    const auto &normals = this->mesh_->normalPlanes();
    this->planeNormalsTex = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
    this->planeNormalsTex->setFormat(QOpenGLTexture::RGBA16_UNorm);
    this->planeNormalsTex->setSize(w, h);
    this->planeNormalsTex->setLayers(6);
    this->planeNormalsTex->setMipLevels(1);
    this->planeNormalsTex->allocateStorage();
    for (int layer = 0; layer < 6; layer++)
    {
        if (!normals[layer] || normals[layer]->width() != w || normals[layer]->height() != h)
            continue;
        this->planeNormalsTex->setData(0, layer, QOpenGLTexture::RGBA, QOpenGLTexture::UInt16,
                                       reinterpret_cast<const uint16_t *>(normals[layer]->data()));
    }
    this->planeNormalsTex->setMinificationFilter(QOpenGLTexture::Nearest);
    this->planeNormalsTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    this->planeNormalsTex->setWrapMode(QOpenGLTexture::ClampToEdge);
}

QOpenGLShaderProgram *ReliefView::bindSurfaceProgram(int debugView)
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

    p.setUniformValue("BoxMin", QVector3D(meshBoundsMin_.x, meshBoundsMin_.y, meshBoundsMin_.z));
    p.setUniformValue("BoxMax", QVector3D(meshBoundsMax_.x, meshBoundsMax_.y, meshBoundsMax_.z));
    this->planesTex->bind(1);
    p.setUniformValue("Plane_Maps", 1);
    this->planeNormalsTex->bind(2);
    p.setUniformValue("Normal_Maps", 2);
    return &p;
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void ReliefView::setReliefEnabled(bool v)
{
    this->reliefEnabled = v;
    update();
}
void ReliefView::setSteps(int v)
{
    this->steps = std::max(1, v);
    update();
}
void ReliefView::setDebugView(int v)
{
    this->debugView = v;
    update();
}

// ─── GL lifecycle ─────────────────────────────────────────────────────────────

void ReliefView::initPrograms()
{
    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/relief.vert"))
        std::cerr << "ReliefView vert error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/relief.frag"))
        std::cerr << "ReliefView frag error: " << this->prog.log().toStdString() << "\n";
    if (!this->prog.link())
        std::cerr << "ReliefView link error: " << this->prog.log().toStdString() << "\n";
}

void ReliefView::onMeshChanged()
{
    if (this->mesh_->planes()[0])
    {
        uploadColorTexture();
        uploadPlanesTextures();
        return;
    }

    // Not baked: drop the previous mesh's textures so the "not ready" message shows.
    deleteTextures();
}

QString ReliefView::notReadyMessage() const
{
    return "Run Textures Preparation first to view relief mapping.";
}

void ReliefView::paintScene()
{
    if (this->cullFace_)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);

    // The 6-plane shader ray-marches per fragment, so it can't be drawn as lines itself:
    // its wireframe is an overlay of the box edges over the shaded frame (see below).
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    QOpenGLShaderProgram *surfaceProg = bindSurfaceProgram(this->debugView);

    this->meshVao_.bind();
    glDrawElements(GL_TRIANGLES, this->meshIndexCount_, GL_UNSIGNED_INT, nullptr);
    if (this->wireframe_)
    {
        // DebugView 4 makes relief.frag output a flat color without ray marching.
        surfaceProg->setUniformValue("DebugView", 4);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDepthFunc(GL_LEQUAL);
        glDrawElements(GL_TRIANGLES, this->meshIndexCount_, GL_UNSIGNED_INT, nullptr);
        glDepthFunc(GL_LESS);
    }
    this->meshVao_.release();

    surfaceProg->release();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void ReliefView::deleteTextures()
{
    delete colorTex;  colorTex  = nullptr;
    delete planesTex; planesTex = nullptr;
    delete planeNormalsTex; planeNormalsTex = nullptr;
}
