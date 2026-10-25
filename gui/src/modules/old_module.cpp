/**
 * @file old_module.cpp
 * @brief OldModule implementation: builds the relief-preview UI and
 *        pushes pending mesh/texture data into the viewports once ready.
 */
#include "gui/modules/old_module.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <functional>

using namespace mesh;

// ─── buildContent / buildControls ──────────────────────────────────────────

QWidget* OldModule::buildContent()
{
    // ── 3 MeshViews side by side ─────────────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    reliefOriginalWidget_ = new MeshView(RenderMode::Textured, "Original Model");
    reliefOriginalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefOriginalWidget_);

    reliefCompareWidget_ = new MeshView(RenderMode::Textured, "Simplified Mesh (no relief)");
    reliefCompareWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefCompareWidget_);

    reliefWidget_ = new OldReliefView("Relief");
    viewportsLayout->addWidget(reliefWidget_);

    // Cameras stay in sync across the three viewports so they can be compared directly.
    connect(reliefWidget_,        &OldReliefView::cameraChanged,    reliefCompareWidget_,  &MeshView::syncCamera);
    connect(reliefWidget_,        &OldReliefView::cameraChanged,    reliefOriginalWidget_, &MeshView::syncCamera);
    connect(reliefCompareWidget_, &MeshView::cameraChanged, reliefWidget_,         &OldReliefView::syncCamera);
    connect(reliefCompareWidget_, &MeshView::cameraChanged, reliefOriginalWidget_, &MeshView::syncCamera);
    connect(reliefOriginalWidget_,&MeshView::cameraChanged, reliefWidget_,         &OldReliefView::syncCamera);
    connect(reliefOriginalWidget_,&MeshView::cameraChanged, reliefCompareWidget_,  &MeshView::syncCamera);

    return viewportsWidget;
}

QWidget* OldModule::buildControls()
{
    // Called after buildContent(), so reliefWidget_/Compare_/Original_ are valid.
    QGroupBox* ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout* ctrlLayout = new QVBoxLayout(ctrlGroup);

    reliefEnabledCheck_ = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck_->setChecked(true);
    connect(reliefEnabledCheck_, &QCheckBox::toggled, reliefWidget_, &OldReliefView::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck_);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    reliefStepsSpin_ = new QSpinBox();
    reliefStepsSpin_->setRange(1, 4096);
    reliefStepsSpin_->setValue(64);
    connect(reliefStepsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget_, &OldReliefView::setSteps);
    stepsRow->addWidget(reliefStepsSpin_, 1);
    ctrlLayout->addLayout(stepsRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    reliefDepthScaleSpin_ = new QDoubleSpinBox();
    reliefDepthScaleSpin_->setRange(0.0, 2.0);
    reliefDepthScaleSpin_->setSingleStep(0.01);
    reliefDepthScaleSpin_->setDecimals(4);
    reliefDepthScaleSpin_->setValue(0.05);
    connect(reliefDepthScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), reliefWidget_, &OldReliefView::setDepthScale);
    depthRow->addWidget(reliefDepthScaleSpin_, 1);
    ctrlLayout->addLayout(depthRow);

    reliefUseAtlasCheck_ = new QCheckBox("Use Atlas (Island Leaping)");
    reliefUseAtlasCheck_->setChecked(true);
    connect(reliefUseAtlasCheck_, &QCheckBox::toggled, reliefWidget_, &OldReliefView::setUseAtlas);
    ctrlLayout->addWidget(reliefUseAtlasCheck_);

    QHBoxLayout* texTypeRow = new QHBoxLayout();
    texTypeRow->addWidget(new QLabel("Texture Type:"));
    reliefTextureTypeCombo_ = new QComboBox();
    reliefTextureTypeCombo_->addItem("Depth Map", 0);
    reliefTextureTypeCombo_->addItem("Height Map", 1);
    connect(reliefTextureTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        reliefWidget_->setReliefTextureType(reliefTextureTypeCombo_->currentData().toInt());
    });
    texTypeRow->addWidget(reliefTextureTypeCombo_, 1);
    ctrlLayout->addLayout(texTypeRow);

    QHBoxLayout* debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    reliefDebugViewCombo_ = new QComboBox();
    reliefDebugViewCombo_->addItem("Shaded",          0);
    reliefDebugViewCombo_->addItem("Step Count",      1);
    reliefDebugViewCombo_->addItem("Leap Count",      2);
    reliefDebugViewCombo_->addItem("UV After Relief", 3);
    connect(reliefDebugViewCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        reliefWidget_->setDebugView(reliefDebugViewCombo_->currentData().toInt());
    });
    debugRow->addWidget(reliefDebugViewCombo_, 1);
    ctrlLayout->addLayout(debugRow);

    QHBoxLayout* viewRow = new QHBoxLayout();
    reliefWireframeCheck_ = new QCheckBox("Wireframe");
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefWidget_,        &OldReliefView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &MeshView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &MeshView::setWireframe);
    viewRow->addWidget(reliefWireframeCheck_);
    reliefCullFaceCheck_ = new QCheckBox("Backface Cull");
    reliefCullFaceCheck_->setChecked(true);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefWidget_,        &OldReliefView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &MeshView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &MeshView::setCullFace);
    viewRow->addWidget(reliefCullFaceCheck_);
    ctrlLayout->addLayout(viewRow);

    reliefResetCamBtn_ = new QPushButton("Reset Camera");
    connect(reliefResetCamBtn_, &QPushButton::clicked, this, [this]{ reliefWidget_->resetCamera(); });
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefCompareWidget_,  &MeshView::resetCamera);
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefOriginalWidget_, &MeshView::resetCamera);
    ctrlLayout->addWidget(reliefResetCamBtn_);

    ctrlLayout->addStretch();

    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(ctrlGroup);
    buildLightingGroup(controlsContainer);
    containerLayout->addStretch();

    return controlsContainer;
}

// ─── buildLightingGroup ────────────────────────────────────────────────────────

void OldModule::buildLightingGroup(QWidget* outerControls)
{
    QGroupBox* group = new QGroupBox("Lighting");
    QVBoxLayout* lightLayout = new QVBoxLayout(group);

    // Builds one "<label>: <value> [slider]" row driving all three viewports'
    // shared point light. Slider stores hundredths internally for 0.01 resolution.
    auto addAxisRow = [&](const char* label, int minHundredths, int maxHundredths,
                           int defaultHundredths, std::function<void(double)> apply) {
        QLabel* valueLbl = new QLabel(
            QString("%1: %2").arg(label).arg(defaultHundredths / 100.0, 0, 'f', 2));
        lightLayout->addWidget(valueLbl);

        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setMinimum(minHundredths);
        slider->setMaximum(maxHundredths);
        slider->setValue(defaultHundredths);
        lightLayout->addWidget(slider);

        connect(slider, &QSlider::valueChanged, this, [valueLbl, label, apply](int v) {
            double value = v / 100.0;
            valueLbl->setText(QString("%1: %2").arg(label).arg(value, 0, 'f', 2));
            apply(value);
        });
    };

    // Ranges/defaults match Orbital3DView's default lightPos_{0.f, 2.f, 1.5f}.
    addAxisRow("X", -300, 300, 0, [this](double v) {
        reliefWidget_->setLightX(v);
        reliefOriginalWidget_->setLightX(v);
        reliefCompareWidget_->setLightX(v);
    });
    addAxisRow("Y", 50, 400, 200, [this](double v) {
        reliefWidget_->setLightY(v);
        reliefOriginalWidget_->setLightY(v);
        reliefCompareWidget_->setLightY(v);
    });
    addAxisRow("Z", -300, 300, 150, [this](double v) {
        reliefWidget_->setLightZ(v);
        reliefOriginalWidget_->setLightZ(v);
        reliefCompareWidget_->setLightZ(v);
    });

    outerControls->layout()->addWidget(group);
}

// ─── Public slots ─────────────────────────────────────────────────────────────

void OldModule::onMeshLoaded(Mesh* original, Mesh* simplified)
{
    Module::onMeshLoaded(original, simplified);
    meshPending_ = true;
    syncIfReady();
}

void OldModule::onMeshUpdated()
{
    meshPending_ = true;
    syncIfReady();
}

// ─── Private methods ──────────────────────────────────────────────────────────

void OldModule::showEvent(QShowEvent* event)
{
    Module::showEvent(event);
    syncIfReady();
}

void OldModule::syncIfReady()
{
    if (meshPending_ && simplifiedMesh_ && simplifiedMesh_->faceCount() > 0)
    {
        reliefWidget_->setMesh(simplifiedMesh_);
        reliefCompareWidget_->setMesh(simplifiedMesh_);
        if (originalMesh_)
            reliefOriginalWidget_->setMesh(originalMesh_);
        meshPending_ = false;
    }
}
