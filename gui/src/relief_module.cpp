#include "gui/relief_module.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include <QLabel>

// ─── Constructor ─────────────────────────────────────────────────────────────

ReliefModule::ReliefModule(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void ReliefModule::buildUI()
{
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: 3 Orbital3DViews side by side ───────────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    reliefWidget_ = new Orbital3DView(RenderMode::Relief, "Relief Mapping");
    reliefWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefWidget_);

    reliefCompareWidget_ = new Orbital3DView(RenderMode::Textured, "Simplified Mesh (no relief)");
    reliefCompareWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefCompareWidget_);

    reliefOriginalWidget_ = new Orbital3DView(RenderMode::Textured, "Original Model");
    reliefOriginalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefOriginalWidget_);

    // Cameras stay in sync across the three viewports so they can be compared directly.
    connect(reliefWidget_,        &Orbital3DView::cameraChanged, reliefCompareWidget_,  &Orbital3DView::syncCamera);
    connect(reliefWidget_,        &Orbital3DView::cameraChanged, reliefOriginalWidget_, &Orbital3DView::syncCamera);
    connect(reliefCompareWidget_, &Orbital3DView::cameraChanged, reliefWidget_,         &Orbital3DView::syncCamera);
    connect(reliefCompareWidget_, &Orbital3DView::cameraChanged, reliefOriginalWidget_, &Orbital3DView::syncCamera);
    connect(reliefOriginalWidget_,&Orbital3DView::cameraChanged, reliefWidget_,         &Orbital3DView::syncCamera);
    connect(reliefOriginalWidget_,&Orbital3DView::cameraChanged, reliefCompareWidget_,  &Orbital3DView::syncCamera);

    splitter->addWidget(viewportsWidget);

    // ── Right: controls in a QScrollArea ─────────────────────────────────────
    // Called after viewports are set up, so reliefWidget_/Compare_/Original_ are valid.
    QGroupBox* ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout* ctrlLayout = new QVBoxLayout(ctrlGroup);

    reliefEnabledCheck_ = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck_->setChecked(true);
    connect(reliefEnabledCheck_, &QCheckBox::toggled, reliefWidget_, &Orbital3DView::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck_);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    reliefStepsSpin_ = new QSpinBox();
    reliefStepsSpin_->setRange(1, 256);
    reliefStepsSpin_->setValue(64);
    connect(reliefStepsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget_, &Orbital3DView::setSteps);
    stepsRow->addWidget(reliefStepsSpin_, 1);
    ctrlLayout->addLayout(stepsRow);

    QHBoxLayout* binRow = new QHBoxLayout();
    binRow->addWidget(new QLabel("Binary Steps:"));
    reliefBinaryStepsSpin_ = new QSpinBox();
    reliefBinaryStepsSpin_->setRange(0, 16);
    reliefBinaryStepsSpin_->setValue(5);
    connect(reliefBinaryStepsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget_, &Orbital3DView::setBinarySteps);
    binRow->addWidget(reliefBinaryStepsSpin_, 1);
    ctrlLayout->addLayout(binRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    reliefDepthScaleSpin_ = new QDoubleSpinBox();
    reliefDepthScaleSpin_->setRange(0.0, 2.0);
    reliefDepthScaleSpin_->setSingleStep(0.01);
    reliefDepthScaleSpin_->setDecimals(4);
    reliefDepthScaleSpin_->setValue(0.05);
    connect(reliefDepthScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), reliefWidget_, &Orbital3DView::setDepthScale);
    depthRow->addWidget(reliefDepthScaleSpin_, 1);
    ctrlLayout->addLayout(depthRow);

    reliefUseAtlasCheck_ = new QCheckBox("Use Atlas (Island Leaping)");
    reliefUseAtlasCheck_->setChecked(true);
    connect(reliefUseAtlasCheck_, &QCheckBox::toggled, reliefWidget_, &Orbital3DView::setUseAtlas);
    ctrlLayout->addWidget(reliefUseAtlasCheck_);

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
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefWidget_,        &Orbital3DView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &Orbital3DView::setWireframe);
    connect(reliefWireframeCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &Orbital3DView::setWireframe);
    viewRow->addWidget(reliefWireframeCheck_);
    reliefCullFaceCheck_ = new QCheckBox("Backface Cull");
    reliefCullFaceCheck_->setChecked(true);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefWidget_,        &Orbital3DView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefCompareWidget_,  &Orbital3DView::setCullFace);
    connect(reliefCullFaceCheck_, &QCheckBox::toggled, reliefOriginalWidget_, &Orbital3DView::setCullFace);
    viewRow->addWidget(reliefCullFaceCheck_);
    ctrlLayout->addLayout(viewRow);

    reliefResetCamBtn_ = new QPushButton("Reset Camera");
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefWidget_,        &Orbital3DView::resetCamera);
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefCompareWidget_,  &Orbital3DView::resetCamera);
    connect(reliefResetCamBtn_, &QPushButton::clicked, reliefOriginalWidget_, &Orbital3DView::resetCamera);
    ctrlLayout->addWidget(reliefResetCamBtn_);

    ctrlLayout->addStretch();

    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(ctrlGroup);
    containerLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}

// ─── Public slots ─────────────────────────────────────────────────────────────

void ReliefModule::setMeshes(QEMSimplifier* original, QEMSimplifier* simplified)
{
    originalMesh_   = original;
    simplifiedMesh_ = simplified;
    meshPending_    = true;
}

void ReliefModule::onTexturesReady(const TexturePrepResult& result)
{
    tpResult_        = result;
    texturesPending_ = true;
}

void ReliefModule::onActivated()
{
    syncIfReady();
}

// ─── Private methods ──────────────────────────────────────────────────────────

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
    if (texturesPending_ && tpResult_.valid)
    {
        reliefWidget_->setTextures(tpResult_);
        reliefCompareWidget_->setColorTexture(tpResult_.colorMap);
        reliefOriginalWidget_->setColorTexture(tpResult_.colorMap);
        texturesPending_ = false;
    }
}
