/**
 * @file relief_view.cpp
 * @brief ReliefView implementation: baked plane array construction and upload.
 */
#include "gui/views/relief_view.h"
#include <algorithm>
#include <iostream>

using namespace mesh;
using namespace textures;

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ReliefView::ReliefView(QWidget *parent)
    : Orbital3DView(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ReliefView::~ReliefView()
{
    makeCurrent();
    deleteTextures();
    doneCurrent();
}

// ─── Public API ───────────────────────────────────────────────────────────────

void ReliefView::buildPlanesFromMesh()
{
    const mesh::Texture<uint8_t> &color = this->mesh_->colorTexture();
    const auto &planes = this->mesh_->planes();

    constexpr int kMaxMapRes = 2048;
    const int kRes = color.data.empty()
                         ? 1
                         : std::min(kMaxMapRes, textures::nextPowerOfTwo(std::max(color.width, color.height)));

    // Without a color texture, fall back to a flat gray.
    static const uint8_t kGray[4] = {128, 128, 128, 255};
    const textures::RawImage rawColor =
        color.data.empty() ? textures::RawImage{kGray, 1, 1, 4}
                           : textures::RawImage{color.data.data(), color.width, color.height, 4};
    const MipPyramid colorMap = textures::buildColorMap(rawColor, kRes, kRes);
    uploadTexture(this->colorTex, colorMap, QOpenGLTexture::RGBA32F, QOpenGLTexture::RGBA,
                  QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);

    delete this->planesTex;
    this->planesTex = nullptr;
    delete this->planeNormalsTex;
    this->planeNormalsTex = nullptr;
    const int w = planes[0].width, h = planes[0].height;
    if (w <= 0 || h <= 0)
        return;

    // Nearest filtering: the mask and depth must not be blended across the silhouette.
    // Level 0 is the baked plane; each coarser level keeps, per 2x2 block, the minimum depth
    // over the covered texels in B and whether any is covered in A (see relief.frag).
    int levels = 1;
    for (int size = std::max(w, h); size > 1; size >>= 1)
        levels++;
    this->planeMipCount = levels;

    this->planesTex = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
    this->planesTex->setFormat(QOpenGLTexture::RGBA16_UNorm);
    this->planesTex->setSize(w, h);
    this->planesTex->setLayers(6);
    this->planesTex->setMipLevels(levels);
    this->planesTex->allocateStorage();
    for (int layer = 0; layer < 6; layer++)
    {
        if (planes[layer].data.size() != (size_t)w * h * 4)
            continue;
        std::vector<uint16_t> prev = planes[layer].data;
        this->planesTex->setData(0, layer, QOpenGLTexture::RGBA, QOpenGLTexture::UInt16, prev.data());
        int pw = w, ph = h;
        for (int level = 1; level < levels; level++)
        {
            const int cw = std::max(1, pw >> 1), ch = std::max(1, ph >> 1);
            std::vector<uint16_t> cur((size_t)cw * ch * 4, 0);
            for (int y = 0; y < ch; y++)
            {
                for (int x = 0; x < cw; x++)
                {
                    uint16_t minDepth = 65535;
                    bool covered = false;
                    for (int dy = 0; dy < 2; dy++)
                    {
                        for (int dx = 0; dx < 2; dx++)
                        {
                            const int sx = std::min(2 * x + dx, pw - 1);
                            const int sy = std::min(2 * y + dy, ph - 1);
                            const uint16_t *src = &prev[((size_t)sy * pw + sx) * 4];
                            if (src[3] < 32768)
                                continue;
                            covered = true;
                            minDepth = std::min(minDepth, src[2]);
                        }
                    }
                    uint16_t *dst = &cur[((size_t)y * cw + x) * 4];
                    dst[2] = covered ? minDepth : 0;
                    dst[3] = covered ? 65535 : 0;
                }
            }
            this->planesTex->setData(level, layer, QOpenGLTexture::RGBA, QOpenGLTexture::UInt16, cur.data());
            prev = std::move(cur);
            pw = cw;
            ph = ch;
        }
    }
    this->planesTex->setMinificationFilter(QOpenGLTexture::NearestMipMapNearest);
    this->planesTex->setMagnificationFilter(QOpenGLTexture::Nearest);
    this->planesTex->setWrapMode(QOpenGLTexture::ClampToEdge);

    // Smooth normals: a single level, nearest filtering (the mask must not be blended).
    const auto &normals = this->mesh_->normalPlanes();
    this->planeNormalsTex = new QOpenGLTexture(QOpenGLTexture::Target2DArray);
    this->planeNormalsTex->setFormat(QOpenGLTexture::RGBA16_UNorm);
    this->planeNormalsTex->setSize(w, h);
    this->planeNormalsTex->setLayers(6);
    this->planeNormalsTex->setMipLevels(1);
    this->planeNormalsTex->allocateStorage();
    for (int layer = 0; layer < 6; layer++)
    {
        if (normals[layer].data.size() != (size_t)w * h * 4)
            continue;
        this->planeNormalsTex->setData(0, layer, QOpenGLTexture::RGBA, QOpenGLTexture::UInt16,
                                       normals[layer].data.data());
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
    p.setUniformValue("UseMipSearch", this->useMipSearch);
    p.setUniformValue("PlaneMips", this->planeMipCount);
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
void ReliefView::setMipSearch(bool v)
{
    this->useMipSearch = v;
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
    if (!this->mesh_->planes()[0].data.empty())
    {
        buildPlanesFromMesh();
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
