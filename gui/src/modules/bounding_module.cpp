/**
 * @file bounding_module.cpp
 * @brief BoundingModule implementation: builds the side-by-side orbital/relief
 *        preview content, and a controls pane for listing/adding/popping
 *        bounding-volume pipeline steps.
 */
#include "gui/modules/bounding_module.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

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
    // ── Step queue: list, type picker, add/pop buttons ────────────────────
    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    boundingQueueList_ = new QListWidget();
    containerLayout->addWidget(boundingQueueList_);

    QHBoxLayout* typeRow = new QHBoxLayout();
    typeRow->addWidget(new QLabel("Type:"));
    boundingVolumeTypeCombo_ = new QComboBox();
    boundingVolumeTypeCombo_->addItem("AABB", (int)op::bvol::BoundingVolumeType::AABB);
    boundingVolumeTypeCombo_->addItem("OBB",  (int)op::bvol::BoundingVolumeType::OBB);
    typeRow->addWidget(boundingVolumeTypeCombo_, 1);
    containerLayout->addLayout(typeRow);

    boundingAddStepBtn_ = new QPushButton("Add Step");
    connect(boundingAddStepBtn_, &QPushButton::clicked, this, &BoundingModule::onAddStep);
    containerLayout->addWidget(boundingAddStepBtn_);

    boundingPopStepBtn_ = new QPushButton("Pop Step");
    connect(boundingPopStepBtn_, &QPushButton::clicked, this, &BoundingModule::onPopStep);
    containerLayout->addWidget(boundingPopStepBtn_);

    containerLayout->addStretch();

    return controlsContainer;
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

    auto type = (op::bvol::BoundingVolumeType)boundingVolumeTypeCombo_->currentData().toInt();
    auto stepOp = std::make_shared<op::bvol::BoundingVolumeOp>(type);

    auto stepMesh = std::make_shared<mesh::Mesh>(*boundingQueue_.last().mesh);
    stepOp->apply(*stepMesh);

    boundingQueue_.enqueue({
        stepOp,
        stepMesh,
        boundingVolumeTypeCombo_->currentText(),
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
