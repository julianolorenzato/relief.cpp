/**
 * @file relief_module.cpp
 * @brief ReliefModule implementation: common and relief views side by side, and
 *        a Bake button feeding the relief view.
 */
#include "gui/modules/relief_module.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
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

QWidget* ReliefModule::buildContent()
{
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    orbitalWidget_ = new MeshView(RenderMode::Textured, "Original");
    orbitalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(orbitalWidget_);

    reliefWidget_ = new ReliefView("Relief");
    viewportsLayout->addWidget(reliefWidget_);

    // Cameras stay in sync across the two viewports so they can be compared directly.
    connect(reliefWidget_, &ReliefView::cameraChanged, orbitalWidget_, &MeshView::syncCamera);
    connect(orbitalWidget_, &MeshView::cameraChanged, reliefWidget_, &ReliefView::syncCamera);

    return viewportsWidget;
}

QWidget* ReliefModule::buildControls()
{
    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(4, 4, 4, 4);
    containerLayout->setSpacing(8);

    QCheckBox* wireframeCheck = new QCheckBox("Wireframe", controlsContainer);
    connect(wireframeCheck, &QCheckBox::toggled, orbitalWidget_, &MeshView::setWireframe);
    connect(wireframeCheck, &QCheckBox::toggled, reliefWidget_, &ReliefView::setWireframe);
    containerLayout->addWidget(wireframeCheck);


    // ── Relief mapping parameters ───────────────────────────────────────────
    QGroupBox* paramsGroup = new QGroupBox("Relief Mapping Parameters", controlsContainer);
    QVBoxLayout* paramsLayout = new QVBoxLayout(paramsGroup);

    QCheckBox* enabledCheck = new QCheckBox("Enable Relief Mapping");
    enabledCheck->setChecked(true);
    connect(enabledCheck, &QCheckBox::toggled, reliefWidget_, &ReliefView::setReliefEnabled);
    paramsLayout->addWidget(enabledCheck);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    QSpinBox* stepsSpin = new QSpinBox();
    stepsSpin->setRange(1, 512);
    stepsSpin->setValue(256);
    connect(stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget_, &ReliefView::setSteps);
    stepsRow->addWidget(stepsSpin, 1);
    paramsLayout->addLayout(stepsRow);

    QHBoxLayout* debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    QComboBox* debugViewCombo = new QComboBox();
    debugViewCombo->addItem("Shaded", 0);
    debugViewCombo->addItem("Step Count", 1);
    debugViewCombo->addItem("Leap Count", 2);
    debugViewCombo->addItem("UV After Relief", 3);
    connect(debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), reliefWidget_,
            [this, debugViewCombo](int) { reliefWidget_->setDebugView(debugViewCombo->currentData().toInt()); });
    debugRow->addWidget(debugViewCombo, 1);
    paramsLayout->addLayout(debugRow);

    QPushButton* resetCamBtn = new QPushButton("Reset Camera");
    connect(resetCamBtn, &QPushButton::clicked, reliefWidget_, &ReliefView::resetCamera);
    paramsLayout->addWidget(resetCamBtn);

    containerLayout->addWidget(paramsGroup);

    // ── Lighting ────────────────────────────────────────────────────────────
    QGroupBox* lightingGroup = new QGroupBox("Lighting", controlsContainer);
    QVBoxLayout* lightingLayout = new QVBoxLayout(lightingGroup);

    // Each slider stores hundredths so it can offer 0.01 resolution over a
    // floating-point range; the slot receives the descaled value.
    auto addLightSlider = [this, lightingLayout](const char* label, int minHundredths, int maxHundredths,
                                                 int defaultHundredths, void (Orbital3DView::*slot)(double)) {
        QLabel* valueLbl = new QLabel(QString("%1: %2").arg(label).arg(defaultHundredths / 100.0, 0, 'f', 2));
        lightingLayout->addWidget(valueLbl);

        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setMinimum(minHundredths);
        slider->setMaximum(maxHundredths);
        slider->setValue(defaultHundredths);
        lightingLayout->addWidget(slider);

        connect(slider, &QSlider::valueChanged, reliefWidget_, [this, valueLbl, label, slot](int v) {
            double value = v / 100.0;
            valueLbl->setText(QString("%1: %2").arg(label).arg(value, 0, 'f', 2));
            (reliefWidget_->*slot)(value);
            (orbitalWidget_->*slot)(value);
        });
    };
    // Matches Orbital3DView's default lightPos_{0.f, 2.f, 1.5f}.
    addLightSlider("X", -300, 300, 0, &ReliefView::setLightX);
    addLightSlider("Y", 50, 400, 200, &ReliefView::setLightY);
    addLightSlider("Z", -300, 300, 150, &ReliefView::setLightZ);

    containerLayout->addWidget(lightingGroup);
    containerLayout->addStretch();

    return controlsContainer;
}

// ─── Slots ──────────────────────────────────────────────────────────────────

void ReliefModule::onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified)
{
    Module::onMeshLoaded(original, simplified);

    orbitalWidget_->setMesh(originalMesh_);

    boxMesh_ = std::make_unique<mesh::Mesh>(makeBoxMesh(*originalMesh_));
    reliefWidget_->setMesh(boxMesh_.get());
}

void ReliefModule::onMeshUpdated()
{
    if (!originalMesh_) return;

    boxMesh_ = std::make_unique<mesh::Mesh>(makeBoxMesh(*originalMesh_));
    reliefWidget_->setMesh(boxMesh_.get());
}
