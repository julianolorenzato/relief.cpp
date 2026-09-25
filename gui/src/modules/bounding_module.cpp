/**
 * @file bounding_module.cpp
 * @brief BoundingModule implementation: builds the side-by-side orbital/relief
 *        preview content, and a controls pane for listing pipeline steps and
 *        applying/undoing bounding-volume operations.
 */
#include "gui/modules/bounding_module.h"
#include <QCheckBox>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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
