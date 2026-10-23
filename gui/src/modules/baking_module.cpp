/**
 * @file baking_module.cpp
 * @brief BakingModule implementation: common and relief views side by side, and
 *        a Bake button feeding the relief view.
 */
#include "gui/modules/baking_module.h"
#include "gui/relief_controls.h"
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include "relief/op/bboxproj.h"

namespace {

/**
 * @brief Builds a copy of `src` whose geometry is its axis-aligned bounding box
 *        as 12 triangles (2 per face, unwelded across faces).
 *
 * The copy keeps `src`'s color texture and baked planes (empty if `src` has not been
 * baked), which ReliefView ray-marches inside the box. Its normal and height textures
 * are dropped.
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
            wedge.uv = Eigen::Vector2d(cornerX[i], cornerY[i]);
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

    mesh::Mesh box(src);
    box.replaceGeometry(std::move(vertices), std::move(wedges), std::move(faces));
    box.setNormalTexture({});
    box.setHeightTexture({});
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

    QCheckBox* wireframeCheck = new QCheckBox("Wireframe", controlsContainer);
    connect(wireframeCheck, &QCheckBox::toggled, orbitalWidget_, &Orbital3DView::setWireframe);
    connect(wireframeCheck, &QCheckBox::toggled, reliefWidget_, &ReliefView::setWireframe);
    containerLayout->addWidget(wireframeCheck);

    QCheckBox* mipSearchCheck = new QCheckBox("Mip Search", controlsContainer);
    mipSearchCheck->setChecked(true);
    mipSearchCheck->setToolTip("Mip-hierarchical ray search (off: uniform linear steps)");
    connect(mipSearchCheck, &QCheckBox::toggled, reliefWidget_, &ReliefView::setMipSearch);
    containerLayout->addWidget(mipSearchCheck);

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
