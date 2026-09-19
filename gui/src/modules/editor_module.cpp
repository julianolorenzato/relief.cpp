/**
 * @file editor_module.cpp
 * @brief EditorModule implementation: mesh loading, driving
 *        Mesh, and the inflate/deflate preview.
 */
#include "gui/modules/editor_module.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "relief/mesh/edgesel.h"

// ─── buildUI ─────────────────────────────────────────────────────────────────

void EditorModule::buildUI() {
    QHBoxLayout *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: viewport area (3 Orbital3DViews side by side) ──────────────────
    QWidget *viewportArea = new QWidget();
    QHBoxLayout *viewportsLayout = new QHBoxLayout(viewportArea);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    QWidget *originalGroup = new QWidget();
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *leftLayout = new QVBoxLayout(originalGroup);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    this->glWidgetOriginal = new Orbital3DView(RenderMode::Solid, "Original Mesh");
    this->glWidgetOriginal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(this->glWidgetOriginal, 1);
    viewportsLayout->addWidget(originalGroup);

    QWidget *simplifiedGroup = new QWidget();
    simplifiedGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *rightLayout = new QVBoxLayout(simplifiedGroup);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    this->glWidgetSimplified = new Orbital3DView(RenderMode::Solid, "Simplified Mesh");
    this->glWidgetSimplified->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(this->glWidgetSimplified, 1);
    viewportsLayout->addWidget(simplifiedGroup);

    this->glWidgetOverlay = new Orbital3DView(RenderMode::Overlay, "Overlay");
    this->glWidgetOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(this->glWidgetOverlay);

    viewportArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(this->glWidgetOriginal, &Orbital3DView::cameraChanged, this->glWidgetSimplified,
            &Orbital3DView::syncCamera);
    connect(this->glWidgetOriginal, &Orbital3DView::cameraChanged, this->glWidgetOverlay,
            &Orbital3DView::syncCamera);
    connect(this->glWidgetSimplified, &Orbital3DView::cameraChanged, this->glWidgetOriginal,
            &Orbital3DView::syncCamera);
    connect(this->glWidgetSimplified, &Orbital3DView::cameraChanged, this->glWidgetOverlay,
            &Orbital3DView::syncCamera);
    connect(this->glWidgetOverlay, &Orbital3DView::cameraChanged, this->glWidgetOriginal,
            &Orbital3DView::syncCamera);
    connect(this->glWidgetOverlay, &Orbital3DView::cameraChanged, this->glWidgetSimplified,
            &Orbital3DView::syncCamera);

    splitter->addWidget(viewportArea);

    // ── Right: controls in a QScrollArea ─────────────────────────────────────
    QWidget *controlsWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(controlsWidget);
    layout->setContentsMargins(4, 4, 4, 4);

    // ── Simplification Controls ────────────────────────────────────────────
    QGroupBox *controlsGroup = new QGroupBox("Simplification");
    QVBoxLayout *controlsRows = new QVBoxLayout(controlsGroup);
    controlsRows->setSpacing(4);

    this->simplificationPercentLabel = new QLabel("Reduction: 50%");
    controlsRows->addWidget(this->simplificationPercentLabel);

    this->simplificationSlider = new QSlider(Qt::Horizontal);
    this->simplificationSlider->setMinimum(1);
    this->simplificationSlider->setMaximum(100);
    this->simplificationSlider->setValue(50);
    controlsRows->addWidget(this->simplificationSlider);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *resetBtn = new QPushButton("Reset");
    connect(resetBtn, &QPushButton::clicked, this, &EditorModule::onReset);
    btnRow->addWidget(resetBtn);
    QPushButton *simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &EditorModule::onSimplify);
    btnRow->addWidget(simplifyBtn);
    QPushButton *resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &EditorModule::onResetCameras);
    btnRow->addWidget(resetCamBtn);
    controlsRows->addLayout(btnRow);

    this->wireframeCheck = new QCheckBox("Wireframe");
    connect(this->wireframeCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setWireframe);
    connect(this->wireframeCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setWireframe);
    controlsRows->addWidget(this->wireframeCheck);

    this->texturedCheck = new QCheckBox("Textured");
    this->texturedCheck->setEnabled(false);
    connect(this->texturedCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setTextured);
    connect(this->texturedCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setTextured);
    controlsRows->addWidget(this->texturedCheck);

    this->cullFaceCheck = new QCheckBox("Backface Cull");
    this->cullFaceCheck->setChecked(true);
    connect(this->cullFaceCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setCullFace);
    connect(this->cullFaceCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setCullFace);
    controlsRows->addWidget(this->cullFaceCheck);

    this->uvViewCheck = new QCheckBox("UV View");
    this->uvViewCheck->setEnabled(false);
    connect(this->uvViewCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setUVMode);
    connect(this->uvViewCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setUVMode);
    controlsRows->addWidget(this->uvViewCheck);

    QHBoxLayout *boundaryRow = new QHBoxLayout();
    boundaryRow->addWidget(new QLabel("Boundary:"));
    this->boundaryModeCombo = new QComboBox();
    this->boundaryModeCombo->addItem("No constraint", (int)op::simplification::BoundaryMode::None);
    this->boundaryModeCombo->addItem("Constraint", (int)op::simplification::BoundaryMode::Constraint);
    this->boundaryModeCombo->addItem("Constrain seams",
                                     (int)op::simplification::BoundaryMode::ConstrainSeams);
    this->boundaryModeCombo->addItem("Lock seam edges",
                                     (int)op::simplification::BoundaryMode::LockSeamVertices);
    this->boundaryModeCombo->setCurrentIndex(1);
    boundaryRow->addWidget(this->boundaryModeCombo, 1);
    controlsRows->addLayout(boundaryRow);

    this->useOptimalCandidateCheck = new QCheckBox("Use Optimal Candidate");
    this->useOptimalCandidateCheck->setToolTip(
        "Soma o otimo irrestrito da quadrica como mais um candidato de posicao\n"
        "de colapso, alem de v1, v2 e ponto medio.");
    controlsRows->addWidget(this->useOptimalCandidateCheck);

    this->showInternalEdgesCheck = new QCheckBox("Show Internal Edges");
    connect(this->showInternalEdgesCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setShowInternalEdges);
    connect(this->showInternalEdgesCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setShowInternalEdges);
    controlsRows->addWidget(this->showInternalEdgesCheck);

    this->showSeamEdgesCheck = new QCheckBox("Show Seam Edges");
    connect(this->showSeamEdgesCheck, &QCheckBox::toggled, this->glWidgetOriginal,
            &Orbital3DView::setShowSeamEdges);
    connect(this->showSeamEdgesCheck, &QCheckBox::toggled, this->glWidgetSimplified,
            &Orbital3DView::setShowSeamEdges);
    controlsRows->addWidget(this->showSeamEdgesCheck);

    layout->addWidget(controlsGroup);

    // ── Feature Edge Lock (brush selection) ──────────────────────────────────
    QGroupBox *brushGroup = new QGroupBox("Feature Edge Lock");
    QVBoxLayout *brushRows = new QVBoxLayout(brushGroup);
    brushRows->setSpacing(4);

    this->brushModeToggleBtn = new QPushButton("Brush Select Edges");
    this->brushModeToggleBtn->setCheckable(true);
    connect(this->brushModeToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        this->glWidgetOriginal->setInteractionMode(checked ? InteractionMode::BrushSelect
                                                           : InteractionMode::Orbit);
    });
    brushRows->addWidget(this->brushModeToggleBtn);

    QHBoxLayout *radiusRow = new QHBoxLayout();
    radiusRow->addWidget(new QLabel("Radius:"));
    this->brushRadiusSpin = new QDoubleSpinBox();
    this->brushRadiusSpin->setRange(0.001, 1.0);
    this->brushRadiusSpin->setSingleStep(0.005);
    this->brushRadiusSpin->setDecimals(3);
    this->brushRadiusSpin->setValue(0.05);
    connect(this->brushRadiusSpin, &QDoubleSpinBox::valueChanged, this->glWidgetOriginal,
            &Orbital3DView::setBrushRadius);
    radiusRow->addWidget(this->brushRadiusSpin, 1);
    brushRows->addLayout(radiusRow);

    QHBoxLayout *angleRow = new QHBoxLayout();
    angleRow->addWidget(new QLabel("Angle Threshold (deg):"));
    this->brushAngleSpin = new QDoubleSpinBox();
    this->brushAngleSpin->setRange(1.0, 180.0);
    this->brushAngleSpin->setSingleStep(1.0);
    this->brushAngleSpin->setValue(35.0);
    connect(this->brushAngleSpin, &QDoubleSpinBox::valueChanged, this->glWidgetOriginal,
            &Orbital3DView::setBrushAngleThresholdDeg);
    angleRow->addWidget(this->brushAngleSpin, 1);
    brushRows->addLayout(angleRow);

    QHBoxLayout *propagationRow = new QHBoxLayout();
    propagationRow->addWidget(new QLabel("Propagation:"));
    this->brushPropagationCombo = new QComboBox();
    this->brushPropagationCombo->addItem("Chained (follow curvature)",
                                         (int)mesh::edgesel::PropagationMode::Chained);
    this->brushPropagationCombo->addItem("Anchored to seed (strict)",
                                         (int)mesh::edgesel::PropagationMode::AnchoredToSeed);
    connect(this->brushPropagationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                auto mode =
                    (mesh::edgesel::PropagationMode)this->brushPropagationCombo->currentData().toInt();
                this->glWidgetOriginal->setBrushPropagationMode(mode);
            });
    propagationRow->addWidget(this->brushPropagationCombo, 1);
    brushRows->addLayout(propagationRow);

    this->clearSelectionBtn = new QPushButton("Clear Selection");
    connect(this->clearSelectionBtn, &QPushButton::clicked, this->glWidgetOriginal,
            &Orbital3DView::clearBrushSelection);
    brushRows->addWidget(this->clearSelectionBtn);

    this->selectedEdgeCountLabel = new QLabel("Locked edges: 0");
    brushRows->addWidget(this->selectedEdgeCountLabel);

    connect(this->glWidgetOriginal, &Orbital3DView::selectionChanged, this,
            &EditorModule::onSelectionChanged);

    layout->addWidget(brushGroup);

    // ── Inflate / Deflate ──────────────────────────────────────────────────
    QGroupBox *inflateGroup = new QGroupBox("Inflate / Deflate");
    QVBoxLayout *inflateLayout = new QVBoxLayout(inflateGroup);
    inflateLayout->setSpacing(4);

    QHBoxLayout *inflateValRow = new QHBoxLayout();
    inflateValRow->addWidget(new QLabel("Offset:"));
    this->inflateSpin = new QDoubleSpinBox();
    this->inflateSpin->setMinimum(-1e6);
    this->inflateSpin->setMaximum(1e6);
    this->inflateSpin->setValue(0.0);
    this->inflateSpin->setDecimals(5);
    this->inflateSpin->setSingleStep(0.001);
    inflateValRow->addWidget(this->inflateSpin, 1);
    inflateLayout->addLayout(inflateValRow);

    this->applyInflateBtn = new QPushButton("Apply Inflate");
    connect(this->applyInflateBtn, &QPushButton::clicked, this, &EditorModule::onApplyInflate);
    inflateLayout->addWidget(this->applyInflateBtn);

    layout->addWidget(inflateGroup);

    // ── Smooth ──────────────────────────────────────────────────────────────
    QGroupBox *smoothGroup = new QGroupBox("Smooth");
    QVBoxLayout *smoothLayout = new QVBoxLayout(smoothGroup);
    smoothLayout->setSpacing(4);

    QHBoxLayout *smoothIterRow = new QHBoxLayout();
    smoothIterRow->addWidget(new QLabel("Iterations:"));
    this->smoothIterationsSpin = new QSpinBox();
    this->smoothIterationsSpin->setMinimum(1);
    this->smoothIterationsSpin->setMaximum(50);
    this->smoothIterationsSpin->setValue(1);
    smoothIterRow->addWidget(this->smoothIterationsSpin, 1);
    smoothLayout->addLayout(smoothIterRow);

    QHBoxLayout *smoothStrengthRow = new QHBoxLayout();
    smoothStrengthRow->addWidget(new QLabel("Strength:"));
    this->smoothStrengthSpin = new QDoubleSpinBox();
    this->smoothStrengthSpin->setMinimum(0.0);
    this->smoothStrengthSpin->setMaximum(1.0);
    this->smoothStrengthSpin->setSingleStep(0.05);
    this->smoothStrengthSpin->setValue(0.5);
    smoothStrengthRow->addWidget(this->smoothStrengthSpin, 1);
    smoothLayout->addLayout(smoothStrengthRow);

    this->smoothBtn = new QPushButton("Smooth");
    connect(this->smoothBtn, &QPushButton::clicked, this, &EditorModule::onSmooth);
    smoothLayout->addWidget(this->smoothBtn);

    layout->addWidget(smoothGroup);

    // ── Signals ───────────────────────────────────────────────────────────
    connect(this->simplificationSlider, &QSlider::valueChanged, this,
            &EditorModule::onReductionPercentageChanged);

    layout->addStretch();

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}

// ─── Public methods ───────────────────────────────────────────────────────────

void EditorModule::onMeshLoaded(mesh::Mesh *original, mesh::Mesh *simplified) {
    Module::onMeshLoaded(original, simplified);

    this->originalFaceCount = this->originalMesh_->faceCount();

    this->glWidgetOriginal->setMesh(this->originalMesh_);
    this->glWidgetSimplified->setMesh(this->simplifiedMesh_);
    this->glWidgetOverlay->setMeshes(this->originalMesh_, this->simplifiedMesh_);

    bool hasTexture = !this->originalMesh_->textureData.empty();
    this->texturedCheck->setEnabled(hasTexture);
    if (!hasTexture) this->texturedCheck->setChecked(false);

    bool hasUVs = false;
    for (const auto &wg : this->originalMesh_->wedges)
        if (wg.uv.squaredNorm() > 1e-12) {
            hasUVs = true;
            break;
        }
    this->uvViewCheck->setEnabled(hasUVs);
    if (!hasUVs) this->uvViewCheck->setChecked(false);
}

void EditorModule::onMeshUpdated() {
    this->glWidgetSimplified->setMesh(this->simplifiedMesh_);
    this->glWidgetOverlay->setMeshes(this->originalMesh_, this->simplifiedMesh_);
}

bool EditorModule::saveSimplified(const QString &path) {
    if (!this->simplifiedMesh_ || this->simplifiedMesh_->faceCount() == 0) return false;

    bool success = mesh::io::saveMesh(*this->simplifiedMesh_, path.toStdString());

    return success;
}

// ─── Private slots ────────────────────────────────────────────────────────────

void EditorModule::onReset() {
    if (!this->originalMesh_ || this->originalMesh_->faceCount() == 0) return;

    *this->simplifiedMesh_ = *this->originalMesh_;

    emit notifyMeshUpdate();
}

void EditorModule::onSimplify() {
    if (!this->originalMesh_ || this->originalMesh_->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    int targetFaces =
        std::max(4, (int)(this->originalFaceCount * this->simplificationPercent / 100.0));

    this->simplifyOp = op::simplification::SimplifyOp(
        targetFaces,
        (op::simplification::BoundaryMode)this->boundaryModeCombo->currentData().toInt(),
        this->useOptimalCandidateCheck->isChecked(), this->glWidgetOriginal->selectedEdges());

    emit statusMessage("Simplifying...");

    this->simplifyOp.apply(*this->simplifiedMesh_);

    emit this->notifyMeshUpdate();
    emit this->statusMessage("Simplification finished!");
}

void EditorModule::onReductionPercentageChanged(int sliderValue) {
    this->simplificationPercent = (double)sliderValue;
    this->simplificationPercentLabel->setText(
        QString("Reduction: %1%").arg(this->simplificationPercent, 0, 'f', 0));
}

void EditorModule::onResetCameras() {
    if (this->glWidgetOriginal) this->glWidgetOriginal->resetCamera();
    if (this->glWidgetSimplified) this->glWidgetSimplified->resetCamera();
    if (this->glWidgetOverlay) this->glWidgetOverlay->resetCamera();
}

void EditorModule::onSelectionChanged(int count) {
    if (this->selectedEdgeCountLabel)
        this->selectedEdgeCountLabel->setText(QString("Locked edges: %1").arg(count));
}

void EditorModule::onSmooth() {
    if (!this->simplifiedMesh_ || this->simplifiedMesh_->faceCount() == 0) return;

    this->smoothOp = op::smoothing::SmoothOp(this->smoothIterationsSpin->value(),
                                             this->smoothStrengthSpin->value());
    this->smoothOp.apply(*this->simplifiedMesh_);

    emit notifyMeshUpdate();
    emit statusMessage("Smoothed");
}

void EditorModule::onApplyInflate() {
    if (!this->simplifiedMesh_ || this->simplifiedMesh_->faceCount() == 0) return;

    this->inflateOp = op::inflation::InflateOp(this->inflateSpin->value());
    this->inflateOp.apply(*this->simplifiedMesh_);

    emit notifyMeshUpdate();
    emit statusMessage("Inflated");
}

// ─── Private methods ──────────────────────────────────────────────────────────
