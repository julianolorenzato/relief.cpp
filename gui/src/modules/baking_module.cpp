/**
 * @file baking_module.cpp
 * @brief BakingModule implementation: common and relief views side by side, and
 *        a Bake button feeding the relief view.
 */
#include "gui/modules/baking_module.h"
#include "gui/relief_controls.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include "relief/op/bboxproj.h"

namespace {

/// Atlas grid: one column per axis (X, Y, Z), one row per side (0 = +, 1 = -).
constexpr int kAtlasCols = 3;
constexpr int kAtlasRows = 2;

/**
 * @brief Composes the box's color and height textures from a baked mesh's planes maps.
 *
 * The result is a kAtlasCols x kAtlasRows atlas with one tile per box quad. A quad's color
 * tile shows its planes map as RGB = (u, v, 0), black where the mesh does not cover the
 * texel (so the mesh's silhouette on that plane is visible). Its height tile holds the
 * planes depth map as R = G = B, 0 where uncovered. Both stay empty if `baked` has no
 * planes maps.
 *
 * @param baked  Mesh on which Mesh::bake() has been run.
 * @param color  Receives the RGBA8 color atlas.
 * @param height Receives the RGBA8 height atlas.
 */
void makePlanesAtlas(const mesh::Mesh& baked, mesh::Texture& color, mesh::Texture& height)
{
    const mesh::Texture16* maps[3] = {&baked.xPlanesMap(), &baked.yPlanesMap(), &baked.zPlanesMap()};
    const mesh::Texture16* depths[3] = {&baked.xPlanesDepth(), &baked.yPlanesDepth(),
                                        &baked.zPlanesDepth()};
    const int size = maps[0]->width;
    if (size <= 0 || maps[0]->data.empty()) return;

    const int atlasW = kAtlasCols * size;
    const int atlasH = kAtlasRows * size;
    color.width = height.width = atlasW;
    color.height = height.height = atlasH;
    color.data.assign((size_t)atlasW * atlasH * 4, 0);
    height.data.assign((size_t)atlasW * atlasH * 4, 0);

    auto to8 = [](uint16_t v) { return (uint8_t)((v + 128) / 257); };

    for (int axis = 0; axis < 3; axis++) {
        for (int row = 0; row < kAtlasRows; row++) {
            const int uvChannel = row == 0 ? 0 : 2;     // +axis in RG, -axis in BA
            const int depthChannel = row == 0 ? 0 : 1;  // +axis in R, -axis in G
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    const size_t src = ((size_t)y * size + x) * 4;
                    const size_t dst = ((size_t)(row * size + y) * atlasW + axis * size + x) * 4;

                    color.data[dst + 0] = to8(maps[axis]->data[src + uvChannel]);
                    color.data[dst + 1] = to8(maps[axis]->data[src + uvChannel + 1]);
                    color.data[dst + 3] = 255;

                    const uint16_t d = depths[axis]->data[src + depthChannel];
                    const uint8_t value = d == 0 ? 0 : to8((uint16_t)(d - 1));
                    height.data[dst + 0] = height.data[dst + 1] = height.data[dst + 2] = value;
                    height.data[dst + 3] = 255;
                }
            }
        }
    }
}

/**
 * @brief Builds a copy of `src` whose geometry is its axis-aligned bounding box
 *        as 12 triangles (2 per face, unwelded across faces).
 *
 * Each face maps to its own tile of the atlas made by makePlanesAtlas(), and the
 * copy's color and height textures are that atlas (empty if `src` has no planes
 * maps). Its normal map and planes maps are dropped.
 *
 * @param src Mesh to bound, possibly baked.
 * @return The box mesh.
 */
mesh::Mesh makeBoxMesh(const mesh::Mesh& src)
{
    Eigen::Vector3d min = Eigen::Vector3d::Constant(std::numeric_limits<double>::max());
    Eigen::Vector3d max = Eigen::Vector3d::Constant(std::numeric_limits<double>::lowest());
    for (const mesh::Vertex& v : src.vertices()) {
        if (v.removed) continue;
        min = min.cwiseMin(v.pos);
        max = max.cwiseMax(v.pos);
    }
    const Eigen::Vector3d center = 0.5 * (min + max);
    const Eigen::Vector3d half = 0.5 * (max - min);

    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;

    for (const op::bboxproj::QuadInfo& quad : op::bboxproj::QUADS) {
        const int axis = quad.axis;
        const int tileRow = quad.sign > 0 ? 0 : 1;
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        const Eigen::Vector3d origin = center + quad.sign * half[axis] * Eigen::Vector3d::Unit(axis);

        // Corners in CCW order in (u, v), whose normal is +axis.
        const int cornerX[4] = {0, 1, 1, 0};
        const int cornerY[4] = {0, 0, 1, 1};
        const int base = (int)wedges.size();
        for (int i = 0; i < 4; i++) {
            mesh::Vertex vertex;
            vertex.pos = origin + (cornerX[i] ? half[u] : -half[u]) * Eigen::Vector3d::Unit(u) +
                         (cornerY[i] ? half[v] : -half[v]) * Eigen::Vector3d::Unit(v);
            vertices.push_back(vertex);

            mesh::Wedge wedge;
            wedge.vertex = (int)vertices.size() - 1;
            wedge.uv = Eigen::Vector2d((axis + cornerX[i]) / (double)kAtlasCols,
                                       (tileRow + cornerY[i]) / (double)kAtlasRows);
            wedges.push_back(wedge);
        }

        const int tris[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (const auto& t : tris) {
            mesh::Face f;
            f.w[0] = base + t[0];
            f.w[1] = base + (quad.sign < 0 ? t[2] : t[1]);
            f.w[2] = base + (quad.sign < 0 ? t[1] : t[2]);
            faces.push_back(f);
        }
    }

    mesh::Texture color, height;
    makePlanesAtlas(src, color, height);

    mesh::Mesh box(src);
    box.replaceGeometry(std::move(vertices), std::move(wedges), std::move(faces));
    box.setColorTexture(std::move(color));
    box.setHeightTexture(std::move(height));
    box.setNormalTexture({});
    for (auto set : {&mesh::Mesh::setXPlanesMap, &mesh::Mesh::setYPlanesMap, &mesh::Mesh::setZPlanesMap,
                     &mesh::Mesh::setXPlanesDepth, &mesh::Mesh::setYPlanesDepth,
                     &mesh::Mesh::setZPlanesDepth}) {
        (box.*set)({});
    }
    return box;
}

}  // namespace

// ─── buildContent / buildControls ──────────────────────────────────────────

QWidget* BakingModule::buildContent()
{
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    orbitalWidget_ = new Orbital3DView(RenderMode::Textured, "Baking");
    orbitalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(orbitalWidget_);

    reliefWidget_ = new ReliefView();
    viewportsLayout->addWidget(reliefWidget_);

    // Cameras stay in sync across the two viewports so they can be compared directly.
    connect(reliefWidget_, &ReliefView::cameraChanged, orbitalWidget_, &Orbital3DView::syncCamera);
    connect(orbitalWidget_, &Orbital3DView::cameraChanged, reliefWidget_, &ReliefView::syncCamera);

    return viewportsWidget;
}

QWidget* BakingModule::buildControls()
{
    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(4, 4, 4, 4);
    containerLayout->setSpacing(8);

    containerLayout->addWidget(relief_controls::makeParamsGroup(reliefWidget_, controlsContainer));
    containerLayout->addWidget(relief_controls::makeLightingGroup(reliefWidget_, controlsContainer));
    containerLayout->addStretch();

    return controlsContainer;
}

// ─── Slots ──────────────────────────────────────────────────────────────────

void BakingModule::onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified)
{
    Module::onMeshLoaded(original, simplified);

    orbitalWidget_->setMesh(originalMesh_);

    boxMesh_ = std::make_unique<mesh::Mesh>(makeBoxMesh(*originalMesh_));
    reliefWidget_->setMesh(boxMesh_.get());
}

void BakingModule::onMeshUpdated()
{
    if (!originalMesh_) return;

    boxMesh_ = std::make_unique<mesh::Mesh>(makeBoxMesh(*originalMesh_));
    reliefWidget_->setMesh(boxMesh_.get());
}
