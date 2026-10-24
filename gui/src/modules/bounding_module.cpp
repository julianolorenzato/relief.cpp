/**
 * @file bounding_module.cpp
 * @brief BoundingModule implementation: builds the side-by-side orbital/relief
 *        preview content, and a controls pane for listing pipeline steps and
 *        applying/undoing bounding-volume operations.
 */
#include "gui/modules/bounding_module.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

/// Operations offered by boundingOpCombo_, in the same order as the
/// corresponding pages of boundingParamsStack_.
enum class BoundingOpKind {
    BoundingVolume,
    BBoxProjection,
};

}  // namespace

// ─── buildContent / buildControls ──────────────────────────────────────────

QWidget* BoundingModule::buildContent()
{
    // ── Orbital3DView + ReliefView side by side ──────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    boundingOrbitalWidget_ = new Orbital3DView(RenderMode::Textured, "Bounding");
    boundingOrbitalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(boundingOrbitalWidget_);

    boundingReliefWidget_ = new ReliefView();
    viewportsLayout->addWidget(boundingReliefWidget_);

    // Cameras stay in sync across the two viewports so they can be compared directly.
    connect(boundingReliefWidget_,  &ReliefView::cameraChanged,    boundingOrbitalWidget_, &Orbital3DView::syncCamera);
    connect(boundingOrbitalWidget_, &Orbital3DView::cameraChanged, boundingReliefWidget_,  &ReliefView::syncCamera);

    return viewportsWidget;
}

QWidget* BoundingModule::buildControls()
{
    // ── Step queue, operation picker/params, apply/undo buttons ───────────
    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(4, 4, 4, 4);
    containerLayout->setSpacing(8);

    // ── View features ─────────────────────────────────────────────────────────
    QHBoxLayout* viewRow = new QHBoxLayout();

    boundingWireframeCheck_ = new QCheckBox("Wireframe");
    connect(boundingWireframeCheck_, &QCheckBox::toggled, boundingOrbitalWidget_, &Orbital3DView::setWireframe);
    connect(boundingWireframeCheck_, &QCheckBox::toggled, boundingReliefWidget_,  &ReliefView::setWireframe);
    viewRow->addWidget(boundingWireframeCheck_);

    boundingCullFaceCheck_ = new QCheckBox("Backface Cull");
    boundingCullFaceCheck_->setChecked(true);
    connect(boundingCullFaceCheck_, &QCheckBox::toggled, boundingOrbitalWidget_, &Orbital3DView::setCullFace);
    connect(boundingCullFaceCheck_, &QCheckBox::toggled, boundingReliefWidget_,  &ReliefView::setCullFace);
    viewRow->addWidget(boundingCullFaceCheck_);

    boundingSeamEdgesCheck_ = new QCheckBox("Seam Edges");
    connect(boundingSeamEdgesCheck_, &QCheckBox::toggled, boundingOrbitalWidget_, &Orbital3DView::setShowSeamEdges);
    viewRow->addWidget(boundingSeamEdgesCheck_);

    containerLayout->addLayout(viewRow);

    boundingQueueList_ = new QListWidget();
    containerLayout->addWidget(boundingQueueList_, 1);

    // ── Operation ───────────────────────────────────────────────────────────
    QGroupBox* opGroup = new QGroupBox("Operation");
    QVBoxLayout* opLayout = new QVBoxLayout(opGroup);
    opLayout->setSpacing(6);

    boundingOpCombo_ = new QComboBox();
    boundingOpCombo_->addItem("Bounding Volume", (int)BoundingOpKind::BoundingVolume);
    boundingOpCombo_->addItem("BBox Projection", (int)BoundingOpKind::BBoxProjection);
    opLayout->addWidget(boundingOpCombo_);

    // One parameter page per boundingOpCombo_ entry, same order/index.
    boundingParamsStack_ = new QStackedWidget();
    boundingParamsStack_->addWidget(buildBoundingVolumeParams());
    boundingParamsStack_->addWidget(buildBBoxProjectionParams());
    opLayout->addWidget(boundingParamsStack_);
    connect(boundingOpCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), boundingParamsStack_,
            &QStackedWidget::setCurrentIndex);

    containerLayout->addWidget(opGroup);

    // ── Apply / Undo ────────────────────────────────────────────────────────
    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);

    boundingAddStepBtn_ = new QPushButton("Apply");
    boundingAddStepBtn_->setMinimumHeight(40);
    QFont applyFont = boundingAddStepBtn_->font();
    applyFont.setBold(true);
    boundingAddStepBtn_->setFont(applyFont);
    connect(boundingAddStepBtn_, &QPushButton::clicked, this, &BoundingModule::onAddStep);
    btnRow->addWidget(boundingAddStepBtn_, 1);

    boundingPopStepBtn_ = new QPushButton("Undo");
    boundingPopStepBtn_->setFixedWidth(56);
    connect(boundingPopStepBtn_, &QPushButton::clicked, this, &BoundingModule::onPopStep);
    btnRow->addWidget(boundingPopStepBtn_);

    containerLayout->addLayout(btnRow);

    // ── Relief mapping controls (same as the relief sandbox) ────────────────
    QGroupBox* paramsGroup = new QGroupBox("Relief Mapping Parameters", controlsContainer);
    QVBoxLayout* paramsLayout = new QVBoxLayout(paramsGroup);

    QCheckBox* enabledCheck = new QCheckBox("Enable Relief Mapping");
    enabledCheck->setChecked(true);
    connect(enabledCheck, &QCheckBox::toggled, boundingReliefWidget_, &ReliefView::setReliefEnabled);
    paramsLayout->addWidget(enabledCheck);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    QSpinBox* stepsSpin = new QSpinBox();
    stepsSpin->setRange(1, 512);
    stepsSpin->setValue(256);
    connect(stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), boundingReliefWidget_, &ReliefView::setSteps);
    stepsRow->addWidget(stepsSpin, 1);
    paramsLayout->addLayout(stepsRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    QDoubleSpinBox* depthScaleSpin = new QDoubleSpinBox();
    depthScaleSpin->setRange(0.0, 2.0);
    depthScaleSpin->setSingleStep(0.01);
    depthScaleSpin->setDecimals(4);
    depthScaleSpin->setValue(0.05);
    connect(depthScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), boundingReliefWidget_,
            &ReliefView::setDepthScale);
    depthRow->addWidget(depthScaleSpin, 1);
    paramsLayout->addLayout(depthRow);

    QCheckBox* useAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    useAtlasCheck->setChecked(true);
    connect(useAtlasCheck, &QCheckBox::toggled, boundingReliefWidget_, &ReliefView::setUseAtlas);
    paramsLayout->addWidget(useAtlasCheck);

    QHBoxLayout* texTypeRow = new QHBoxLayout();
    texTypeRow->addWidget(new QLabel("Texture Type:"));
    QComboBox* textureTypeCombo = new QComboBox();
    textureTypeCombo->addItem("Depth Map", 0);
    textureTypeCombo->addItem("Height Map", 1);
    connect(textureTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), boundingReliefWidget_,
            [this, textureTypeCombo](int) { boundingReliefWidget_->setReliefTextureType(textureTypeCombo->currentData().toInt()); });
    texTypeRow->addWidget(textureTypeCombo, 1);
    paramsLayout->addLayout(texTypeRow);

    QHBoxLayout* debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    QComboBox* debugViewCombo = new QComboBox();
    debugViewCombo->addItem("Shaded", 0);
    debugViewCombo->addItem("Step Count", 1);
    debugViewCombo->addItem("Leap Count", 2);
    debugViewCombo->addItem("UV After Relief", 3);
    connect(debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), boundingReliefWidget_,
            [this, debugViewCombo](int) { boundingReliefWidget_->setDebugView(debugViewCombo->currentData().toInt()); });
    debugRow->addWidget(debugViewCombo, 1);
    paramsLayout->addLayout(debugRow);

    QPushButton* resetCamBtn = new QPushButton("Reset Camera");
    connect(resetCamBtn, &QPushButton::clicked, boundingReliefWidget_, &ReliefView::resetCamera);
    paramsLayout->addWidget(resetCamBtn);

    containerLayout->addWidget(paramsGroup);

    // ── Lighting ────────────────────────────────────────────────────────────
    QGroupBox* lightingGroup = new QGroupBox("Lighting", controlsContainer);
    QVBoxLayout* lightingLayout = new QVBoxLayout(lightingGroup);

    // Each slider stores hundredths so it can offer 0.01 resolution over a
    // floating-point range; the slot receives the descaled value.
    auto addLightSlider = [this, lightingLayout](const char* label, int minHundredths, int maxHundredths,
                                                 int defaultHundredths, void (ReliefView::*slot)(double)) {
        QLabel* valueLbl = new QLabel(QString("%1: %2").arg(label).arg(defaultHundredths / 100.0, 0, 'f', 2));
        lightingLayout->addWidget(valueLbl);

        QSlider* slider = new QSlider(Qt::Horizontal);
        slider->setMinimum(minHundredths);
        slider->setMaximum(maxHundredths);
        slider->setValue(defaultHundredths);
        lightingLayout->addWidget(slider);

        connect(slider, &QSlider::valueChanged, boundingReliefWidget_, [this, valueLbl, label, slot](int v) {
            double value = v / 100.0;
            valueLbl->setText(QString("%1: %2").arg(label).arg(value, 0, 'f', 2));
            (boundingReliefWidget_->*slot)(value);
        });
    };
    // Matches ReliefView's default lightPos{0.f, 2.f, 1.5f}.
    addLightSlider("X", -300, 300, 0, &ReliefView::setLightX);
    addLightSlider("Y", 50, 400, 200, &ReliefView::setLightY);
    addLightSlider("Z", -300, 300, 150, &ReliefView::setLightZ);

    containerLayout->addWidget(lightingGroup);

    return controlsContainer;
}

QWidget* BoundingModule::buildBoundingVolumeParams()
{
    QWidget* params = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(params);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addWidget(new QLabel("Type:"));
    boundingVolumeTypeCombo_ = new QComboBox();
    boundingVolumeTypeCombo_->addItem("AABB", (int)op::bvol::BoundingVolumeType::AABB);
    boundingVolumeTypeCombo_->addItem("OBB",  (int)op::bvol::BoundingVolumeType::OBB);
    layout->addWidget(boundingVolumeTypeCombo_, 1);

    return params;
}

QWidget* BoundingModule::buildBBoxProjectionParams()
{
    // BBoxProjectionOp takes no parameters.
    QWidget* params = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(params);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel* noParamsLabel = new QLabel("(no parameters)");
    noParamsLabel->setEnabled(false);
    layout->addWidget(noParamsLabel);

    return params;
}

// ─── Public slots ───────────────────────────────────────────────────────────

void BoundingModule::onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified)
{
    Module::onMeshLoaded(original, simplified);

    boundingQueue_.clear();
    boundingQueue_.enqueue({
        /*op=*/nullptr,
        /*mesh=*/std::make_shared<mesh::Mesh>(*simplifiedMesh_),
        /*label=*/"Seed",
    });

    refreshQueue();
}

// ─── Private slots ──────────────────────────────────────────────────────────

void BoundingModule::onAddStep()
{
    if (boundingQueue_.isEmpty())
        return;

    auto opKind = (BoundingOpKind)boundingOpCombo_->currentData().toInt();

    std::shared_ptr<op::Op> stepOp;
    QString label;
    switch (opKind) {
        case BoundingOpKind::BoundingVolume: {
            auto type = (op::bvol::BoundingVolumeType)boundingVolumeTypeCombo_->currentData().toInt();
            stepOp = std::make_shared<op::bvol::BoundingVolumeOp>(type);
            label = boundingVolumeTypeCombo_->currentText();
            break;
        }
        case BoundingOpKind::BBoxProjection:
            stepOp = std::make_shared<op::bboxproj::BBoxProjectionOp>();
            label = boundingOpCombo_->currentText();
            break;
    }

    auto stepMesh = std::make_shared<mesh::Mesh>(*boundingQueue_.last().mesh);
    stepOp->apply(*stepMesh);

    boundingQueue_.enqueue({
        stepOp,
        stepMesh,
        label,
    });

    refreshQueue();
}

void BoundingModule::onPopStep()
{
    if (boundingQueue_.size() <= 1)
        return;

    boundingQueue_.removeLast();
    refreshQueue();
}

// ─── Private methods ────────────────────────────────────────────────────────

void BoundingModule::refreshQueue()
{
    boundingQueueList_->clear();
    for (int i = 0; i < boundingQueue_.size(); i++)
        boundingQueueList_->addItem(QString("Step %1: %2").arg(i).arg(boundingQueue_[i].label));
    if (boundingQueueList_->count() > 0)
        boundingQueueList_->setCurrentRow(boundingQueueList_->count() - 1);

    boundingPopStepBtn_->setEnabled(boundingQueue_.size() > 1);

    if (boundingQueue_.isEmpty())
        return;

    mesh::Mesh* lastMesh = boundingQueue_.last().mesh.get();
    boundingOrbitalWidget_->setMesh(lastMesh, lastMesh);
    boundingReliefWidget_->setMesh(lastMesh);
}
