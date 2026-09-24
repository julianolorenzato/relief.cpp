/**
 * @file relief_module.cpp
 * @brief ReliefModule implementation: builds the relief-preview UI and
 *        pushes pending mesh/texture data into the viewports once ready.
 */
#include "gui/modules/relief_module.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <functional>

using namespace mesh;

// ─── buildContent / buildControls ──────────────────────────────────────────

QWidget* ReliefModule::buildContent()
{
    // ── 3 Orbital3DViews side by side ─────────────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    reliefOriginalWidget_ = new Orbital3DView(RenderMode::Textured, "Original Model");
    reliefOriginalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefOriginalWidget_);

    reliefCompareWidget_ = new Orbital3DView(RenderMode::Textured, "Simplified Mesh (no relief)");
    reliefCompareWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefCompareWidget_);

    reliefWidget_ = new ReliefView();
    viewportsLayout->addWidget(reliefWidget_);

    // Cameras stay in sync across the three viewports so they can be compared directly.
    connect(reliefWidget_,        &ReliefView::cameraChanged,    reliefCompareWidget_,  &Orbital3DView::syncCamera);
    connect(reliefWidget_,        &ReliefView::cameraChanged,    reliefOriginalWidget_, &Orbital3DView::syncCamera);
    connect(reliefCompareWidget_, &Orbital3DView::cameraChanged, reliefWidget_,         &ReliefView::syncCamera);
    connect(reliefCompareWidget_, &Orbital3DView::cameraChanged, reliefOriginalWidget_, &Orbital3DView::syncCamera);
    connect(reliefOriginalWidget_,&Orbital3DView::cameraChanged, reliefWidget_,         &ReliefView::syncCamera);
    connect(reliefOriginalWidget_,&Orbital3DView::cameraChanged, reliefCompareWidget_,  &Orbital3DView::syncCamera);

    return viewportsWidget;
}

QWidget* ReliefModule::buildControls()
{
    // Called after buildContent(), so reliefWidget_/Compare_/Original_ are valid.
    QGroupBox* ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout* ctrlLayout = new QVBoxLayout(ctrlGroup);

    reliefEnabledCheck_ = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck_->setChecked(true);
    connect(reliefEnabledCheck_, &QCheckBox::toggled, reliefWidget_, &ReliefView::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck_);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    reliefStepsSpin_ = new QSpinBox();
    reliefStepsSpin_->setRange(1, 4096);
    reliefStepsSpin_->setValue(64);
    connect(reliefStepsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget_, &ReliefView::setSteps);
    stepsRow->addWidget(reliefStepsSpin_, 1);
    ctrlLayout->addLayout(stepsRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    reliefDepthScaleSpin_ = new QDoubleSpinBox();
    reliefDepthScaleSpin_->setRange(0.0, 2.0);
    reliefDepthScaleSpin_->setSingleStep(0.01);
    reliefDepthScaleSpin_->setDecimals(4);
    reliefDepthScaleSpin_->setValue(0.05);
    connect(reliefDepthScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), reliefWidget_, &ReliefView::setDepthScale);
    depthRow->addWidget(reliefDepthScaleSpin_, 1);
    ctrlLayout->addLayout(depthRow);

    reliefUseAtlasCheck_ = new QCheckBox("Use Atlas (Island Leaping)");
    reliefUseAtlasCheck_->setChecked(true);
    connect(reliefUseAtlasCheck_, &QCheckBox::toggled, reliefWidget_, &ReliefView::setUseAtlas);
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
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefWidget_,        &ReliefView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &Orbital3DView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &Orbital3DView::setWireframe);
    viewRow->addWidget(reliefWireframeCheck_);
    reliefCullFaceCheck_ = new QCheckBox("Backface Cull");
    reliefCullFaceCheck_->setChecked(true);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefWidget_,        &ReliefView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &Orbital3DView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &Orbital3DView::setCullFace);
    viewRow->addWidget(reliefCullFaceCheck_);
    ctrlLayout->addLayout(viewRow);

    reliefResetCamBtn_ = new QPushButton("Reset Camera");
    connect(reliefResetCamBtn_, &QPushButton::clicked, this, [this]{ reliefWidget_->resetCamera(); });
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefCompareWidget_,  &Orbital3DView::resetCamera);
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefOriginalWidget_, &Orbital3DView::resetCamera);
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

void ReliefModule::buildLightingGroup(QWidget* outerControls)
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

    // Ranges/defaults match ReliefView's default lightPos{0.f, 2.f, 1.5f}.
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

void ReliefModule::onMeshLoaded(Mesh* original, Mesh* simplified)
{
    Module::onMeshLoaded(original, simplified);
    meshPending_ = true;
    syncIfReady();
}

void ReliefModule::onMeshUpdated()
{
    meshPending_ = true;
    syncIfReady();
}

void ReliefModule::onTexturesReady(TexturePrepModule* source)
{
    texturePrepSource_ = source;
    texturesPending_    = true;
    syncIfReady();
}

// ─── Private methods ──────────────────────────────────────────────────────────

void ReliefModule::showEvent(QShowEvent* event)
{
    Module::showEvent(event);
    syncIfReady();
}

void ReliefModule::syncIfReady()
{
    if (meshPending_ && simplifiedMesh_ && simplifiedMesh_->faceCount() > 0)
    {
        reliefWidget_->setMesh(simplifiedMesh_);
        reliefCompareWidget_->setMesh(simplifiedMesh_);
        if (originalMesh_)
            reliefOriginalWidget_->setMesh(originalMesh_);
        meshPending_ = false;
    }
    if (texturesPending_ && texturePrepSource_ && texturePrepSource_->hasTextures())
    {
        reliefWidget_->setColorMap(texturePrepSource_->colorMap());
        reliefWidget_->setReliefMap(texturePrepSource_->reliefMap());
        reliefWidget_->setNormalMap(texturePrepSource_->normalMap());
        reliefWidget_->setOffsetMap(texturePrepSource_->offsetMap());
        texturesPending_ = false;
    }
}
