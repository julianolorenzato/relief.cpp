/**
 * @file relief_view.cpp
 * @brief ReliefView implementation: baked plane array upload.
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

QOpenGLTexture *ReliefView::uploadPlanes(const std::array<std::optional<tex::Texture>, 6> &planes,
                                         QOpenGLTexture::Filter filter)
{
    if (!planes[0] || !planes[0]->valid())
        return nullptr;
    const tex::PixelFormat format = planes[0]->format;
    const int w = planes[0]->width(), h = planes[0]->height();

    QOpenGLPixelTransferOptions opts;
    opts.setAlignment(1);

    // A single level: the planes are sampled at level 0.
    QOpenGLTexture *texture = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
    texture->setFormat(static_cast<QOpenGLTexture::TextureFormat>(glfmt::internalFormat(format)));
    texture->setSize(w, h);
    texture->setLayers(6);
    texture->setMipLevels(1);
    texture->allocateStorage();
    for (int layer = 0; layer < 6; layer++)
    {
        if (!planes[layer] || planes[layer]->format != format || planes[layer]->width() != w ||
            planes[layer]->height() != h)
            continue;
        texture->setData(0, layer, static_cast<QOpenGLTexture::PixelFormat>(glfmt::pixelFormat(format)),
                         static_cast<QOpenGLTexture::PixelType>(glfmt::pixelType(format)),
                         planes[layer]->data(), &opts);
    }
    texture->setMinificationFilter(filter);
    texture->setMagnificationFilter(filter);
    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    return texture;
}

void ReliefView::uploadPlanesTextures()
{
    deleteTextures();
    this->depthTex = uploadPlanes(this->mesh_->depthPlanes(), QOpenGLTexture::Nearest);
    this->colorTex = uploadPlanes(this->mesh_->colorPlanes(), QOpenGLTexture::Linear);
    this->normalTex = uploadPlanes(this->mesh_->normalPlanes(), QOpenGLTexture::Linear);
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

    p.setUniformValue("BoxMin", QVector3D(meshBoundsMin_.x, meshBoundsMin_.y, meshBoundsMin_.z));
    p.setUniformValue("BoxMax", QVector3D(meshBoundsMax_.x, meshBoundsMax_.y, meshBoundsMax_.z));
    this->depthTex->bind(0);
    p.setUniformValue("Depth_Maps", 0);
    this->colorTex->bind(1);
    p.setUniformValue("Color_Maps", 1);
    this->normalTex->bind(2);
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
    if (this->mesh_->depthPlanes()[0])
    {
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
    delete depthTex;  depthTex  = nullptr;
    delete colorTex;  colorTex  = nullptr;
    delete normalTex; normalTex = nullptr;
}
