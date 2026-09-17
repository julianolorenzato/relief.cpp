/**
 * @file simplifier_module.cpp
 * @brief SimplifierModule implementation: mesh loading, driving
 *        Mesh, and the inflate/deflate preview.
 */
#include "gui/modules/simplifier_module.h"

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

#include "relief/edge_selection.h"

// ─── Constructor ─────────────────────────────────────────────────────────────

SimplifierModule::SimplifierModule(GlobalContext *context, QWidget *parent) : Module(context, parent) {
    this->simplifiedMesh = std::make_unique<mesh::Mesh>();
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void SimplifierModule::buildUI() {
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

    QHBoxLayout *facesRow = new QHBoxLayout();
    facesRow->addWidget(new QLabel("Target Faces:"));
    this->targetFacesSpinBox = new QSpinBox();
    this->targetFacesSpinBox->setMinimum(4);
    this->targetFacesSpinBox->setMaximum(1000000);
    this->targetFacesSpinBox->setValue(1000);
    facesRow->addWidget(this->targetFacesSpinBox, 1);
    controlsRows->addLayout(facesRow);

    this->simplificationSlider = new QSlider(Qt::Horizontal);
    this->simplificationSlider->setMinimum(1);
    this->simplificationSlider->setMaximum(100);
    this->simplificationSlider->setValue(50);
    controlsRows->addWidget(this->simplificationSlider);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &SimplifierModule::onSimplify);
    btnRow->addWidget(simplifyBtn);
    QPushButton *resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &SimplifierModule::onResetCameras);
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
    this->boundaryModeCombo->addItem("No constraint", (int)simplification::BoundaryMode::None);
    this->boundaryModeCombo->addItem("Constraint", (int)simplification::BoundaryMode::Constraint);
    this->boundaryModeCombo->addItem("Lock seam edges",
                                     (int)simplification::BoundaryMode::LockSeamVertices);
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
                                         (int)edgesel::PropagationMode::Chained);
    this->brushPropagationCombo->addItem("Anchored to seed (strict)",
                                         (int)edgesel::PropagationMode::AnchoredToSeed);
    connect(this->brushPropagationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                auto mode =
                    (edgesel::PropagationMode)this->brushPropagationCombo->currentData().toInt();
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
            &SimplifierModule::onSelectionChanged);

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
    this->inflateSpin->setEnabled(false);
    inflateValRow->addWidget(this->inflateSpin, 1);
    inflateLayout->addLayout(inflateValRow);

    this->inflateSlider = new QSlider(Qt::Horizontal);
    this->inflateSlider->setMinimum(-1000);
    this->inflateSlider->setMaximum(1000);
    this->inflateSlider->setValue(0);
    this->inflateSlider->setEnabled(false);
    inflateLayout->addWidget(this->inflateSlider);

    this->simplifyInflatedBtn = new QPushButton("Simplify Inflated Mesh");
    this->simplifyInflatedBtn->setEnabled(false);
    connect(this->simplifyInflatedBtn, &QPushButton::clicked, this,
            &SimplifierModule::onSimplifyInflated);
    inflateLayout->addWidget(this->simplifyInflatedBtn);

    connect(this->inflateSlider, &QSlider::valueChanged, this, [this](int val) {
        double offset = (this->inflateScale > 1e-10) ? val / 1000.0 * this->inflateScale : 0.0;
        this->inflateSpin->blockSignals(true);
        this->inflateSpin->setValue(offset);
        this->inflateSpin->blockSignals(false);
        applyInflate(offset);
    });
    connect(this->inflateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double val) {
                int sliderVal =
                    (this->inflateScale > 1e-10) ? (int)(val / this->inflateScale * 1000.0) : 0;
                this->inflateSlider->blockSignals(true);
                this->inflateSlider->setValue(std::max(-1000, std::min(1000, sliderVal)));
                this->inflateSlider->blockSignals(false);
                applyInflate(val);
            });

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
    connect(this->smoothBtn, &QPushButton::clicked, this, &SimplifierModule::onSmooth);
    smoothLayout->addWidget(this->smoothBtn);

    layout->addWidget(smoothGroup);

    // ── Signals ───────────────────────────────────────────────────────────
    connect(this->targetFacesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &SimplifierModule::onTargetFacesChanged);
    connect(this->simplificationSlider, &QSlider::valueChanged, this, [this](int val) {
        int targetFaces = std::max(4, (int)(this->originalFaceCount * val / 100.0));
        this->targetFacesSpinBox->blockSignals(true);
        this->targetFacesSpinBox->setValue(targetFaces);
        this->targetFacesSpinBox->blockSignals(false);
    });

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

void SimplifierModule::onModelLoaded(mesh::Mesh *original) {
    this->originalMesh = original;

    // Start "simplified" as a copy of the original so Textures Preparation /
    // Relief Mapping work even before the user runs Simplify.
    this->simplifiedMesh = std::make_unique<mesh::Mesh>(*this->originalMesh);

    this->originalFaceCount = this->originalMesh->faceCount();
    this->targetFaceCount = std::max(4, this->originalFaceCount / 4);

    this->targetFacesSpinBox->blockSignals(true);
    this->targetFacesSpinBox->setMaximum(this->originalFaceCount);
    this->targetFacesSpinBox->setValue(this->targetFaceCount);
    this->simplificationSlider->setValue(75);
    this->targetFacesSpinBox->blockSignals(false);

    this->glWidgetOriginal->setMesh(this->originalMesh);
    this->glWidgetSimplified->setMesh(this->simplifiedMesh.get());
    this->glWidgetOverlay->setMeshes(this->originalMesh, this->simplifiedMesh.get());

    bool hasTexture = !this->originalMesh->textureData.empty();
    this->texturedCheck->setEnabled(hasTexture);
    if (!hasTexture) this->texturedCheck->setChecked(false);

    bool hasUVs = false;
    for (const auto &wg : this->originalMesh->wedges)
        if (wg.uv.squaredNorm() > 1e-12) {
            hasUVs = true;
            break;
        }
    this->uvViewCheck->setEnabled(hasUVs);
    if (!hasUVs) this->uvViewCheck->setChecked(false);

    // Baseline the inflate/deflate controls on the freshly loaded mesh (still a full-resolution
    // copy of originalMesh at this point) so the user can inflate before ever running
    // Simplify.
    captureInflateBaseline();

    updateStats();
    emit modelLoaded(this->originalMesh, this->simplifiedMesh.get());
}

bool SimplifierModule::saveSimplified(const QString &path) {
    if (!this->simplifiedMesh || this->simplifiedMesh->faceCount() == 0) return false;

    bool success = mesh::io::saveMesh(*this->simplifiedMesh, path.toStdString());

    return success;
}

// ─── Private slots ────────────────────────────────────────────────────────────

void SimplifierModule::onSimplify() {
    if (!this->originalMesh || this->originalMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    int targetFaces = this->targetFacesSpinBox->value();
    *this->simplifiedMesh = *this->originalMesh;

    simplification::Simplifier simplifier(*this->simplifiedMesh);
    simplifier.boundaryMode =
        (simplification::BoundaryMode)this->boundaryModeCombo->currentData().toInt();
    simplifier.useOptimalCandidate = this->useOptimalCandidateCheck->isChecked();
    simplifier.setUserLockedEdges(this->glWidgetOriginal->selectedEdges());

    emit statusMessage("Simplifying...");

    simplifier.run(targetFaces);

    captureInflateBaseline();

    this->glWidgetSimplified->setMesh(this->simplifiedMesh.get());
    this->glWidgetOverlay->setMeshes(this->originalMesh, this->simplifiedMesh.get());
    updateStats();

    emit simplificationDone(this->originalMesh, this->simplifiedMesh.get());
}

/**
 * @brief Runs Simplifier again directly on simplifiedMesh, keeping its current (possibly
 *        inflated) vertex positions as the base instead of resetting from originalMesh.
 */
void SimplifierModule::onSimplifyInflated() {
    if (!this->simplifiedMesh || this->simplifiedMesh->faceCount() == 0) return;

    int targetFaces = this->targetFacesSpinBox->value();

    // No reset from originalMesh: keep simplifiedMesh's current (possibly
    // inflated) vertex positions as the base for this decimation pass.
    simplification::Simplifier simplifier(*this->simplifiedMesh);
    simplifier.boundaryMode =
        (simplification::BoundaryMode)this->boundaryModeCombo->currentData().toInt();
    simplifier.useOptimalCandidate = this->useOptimalCandidateCheck->isChecked();
    simplifier.setUserLockedEdges(this->glWidgetOriginal->selectedEdges());

    emit statusMessage("Simplifying inflated mesh...");

    simplifier.run(targetFaces);

    captureInflateBaseline();

    this->glWidgetSimplified->setMesh(this->simplifiedMesh.get());
    this->glWidgetOverlay->setMeshes(this->originalMesh, this->simplifiedMesh.get());
    updateStats();

    emit simplificationDone(this->originalMesh, this->simplifiedMesh.get());
}

void SimplifierModule::onTargetFacesChanged(int value) { this->targetFaceCount = value; }

void SimplifierModule::onResetCameras() {
    if (this->glWidgetOriginal) this->glWidgetOriginal->resetCamera();
    if (this->glWidgetSimplified) this->glWidgetSimplified->resetCamera();
    if (this->glWidgetOverlay) this->glWidgetOverlay->resetCamera();
}

void SimplifierModule::onSelectionChanged(int count) {
    if (this->selectedEdgeCountLabel)
        this->selectedEdgeCountLabel->setText(QString("Locked edges: %1").arg(count));
}

void SimplifierModule::onSmooth() {
    if (!this->simplifiedMesh || this->simplifiedMesh->faceCount() == 0) return;

    this->simplifiedMesh->smooth(this->smoothIterationsSpin->value(),
                                 this->smoothStrengthSpin->value());

    // Positions changed; rebase inflate offsets/normals so a later Inflate
    // doesn't jump back to the pre-smooth shape.
    captureInflateBaseline();
    this->glWidgetSimplified->updateMeshData();
    this->glWidgetOverlay->updateSecondaryMesh();
    emit statusMessage("Smoothed");
}

// ─── Private methods ──────────────────────────────────────────────────────────

/**
 * @brief Recomputes the inflate baseline from simplifiedMesh's current geometry and
 *        (re)enables the inflate/deflate and "Simplify Inflated Mesh" controls.
 */
void SimplifierModule::captureInflateBaseline() {
    // Capture base positions and compute vertex normals for inflate/deflate
    this->baseSimplifiedPositions.resize(this->simplifiedMesh->vertices.size());
    for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++)
        this->baseSimplifiedPositions[i] = this->simplifiedMesh->vertices[i].pos;

    this->simplifiedVertexNormals.assign(this->simplifiedMesh->vertices.size(),
                                         Eigen::Vector3d::Zero());
    for (const auto &f : this->simplifiedMesh->faces) {
        if (f.removed) continue;
        int v0 = this->simplifiedMesh->wedges[f.w[0]].vertex;
        int v1 = this->simplifiedMesh->wedges[f.w[1]].vertex;
        int v2 = this->simplifiedMesh->wedges[f.w[2]].vertex;
        const auto &p0 = this->baseSimplifiedPositions[v0];
        const auto &p1 = this->baseSimplifiedPositions[v1];
        const auto &p2 = this->baseSimplifiedPositions[v2];
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        this->simplifiedVertexNormals[v0] += n;
        this->simplifiedVertexNormals[v1] += n;
        this->simplifiedVertexNormals[v2] += n;
    }

    // Vértices duplicados na mesma posição 3D (ex.: costuras de UV, separadas
    // em vertices distintos no loadOBJ) devem inflar juntos. Caso contrário,
    // cada cópia usa só suas próprias faces incidentes, as normais divergem,
    // e a costura abre um buraco ao inflar mesmo com seam vertices travados.
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto &p : this->baseSimplifiedPositions) {
            bmin = bmin.cwiseMin(p);
            bmax = bmax.cwiseMax(p);
        }
        double cell = std::max((bmax - bmin).norm() * 1e-7, 1e-9);

        auto quantize = [cell](const Eigen::Vector3d &p) {
            return std::make_tuple((long long)std::llround(p.x() / cell),
                                   (long long)std::llround(p.y() / cell),
                                   (long long)std::llround(p.z() / cell));
        };

        std::map<std::tuple<long long, long long, long long>, int> groupId;
        this->simplifiedVertexGroup.assign(this->simplifiedMesh->vertices.size(), -1);
        for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++) {
            if (this->simplifiedMesh->vertices[i].removed) continue;
            auto key = quantize(this->baseSimplifiedPositions[i]);
            auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
            this->simplifiedVertexGroup[i] = it->second;
        }
        this->simplifiedVertexGroupCount = (int)groupId.size();

        std::vector<Eigen::Vector3d> groupNormal(this->simplifiedVertexGroupCount,
                                                 Eigen::Vector3d::Zero());
        for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++) {
            if (this->simplifiedMesh->vertices[i].removed) continue;
            groupNormal[this->simplifiedVertexGroup[i]] += this->simplifiedVertexNormals[i];
        }
        for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++) {
            if (this->simplifiedMesh->vertices[i].removed) continue;
            this->simplifiedVertexNormals[i] = groupNormal[this->simplifiedVertexGroup[i]];
        }
    }

    for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++) {
        double len = this->simplifiedVertexNormals[i].norm();
        if (len > 1e-10) this->simplifiedVertexNormals[i] /= len;
    }

    // Set inflate range based on original mesh bounding box diagonal
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto &v : this->originalMesh->vertices) {
            if (!v.removed) {
                bmin = bmin.cwiseMin(v.pos);
                bmax = bmax.cwiseMax(v.pos);
            }
        }
        this->inflateScale = std::max((bmax - bmin).norm() * 0.5, 1e-6);
    }

    this->inflateSpin->blockSignals(true);
    this->inflateSpin->setMinimum(-this->inflateScale);
    this->inflateSpin->setMaximum(this->inflateScale);
    this->inflateSpin->setSingleStep(this->inflateScale / 1000.0);
    this->inflateSpin->setValue(0.0);
    this->inflateSpin->blockSignals(false);
    this->inflateSlider->blockSignals(true);
    this->inflateSlider->setValue(0);
    this->inflateSlider->blockSignals(false);
    this->inflateSlider->setEnabled(true);
    this->inflateSpin->setEnabled(true);
    this->simplifyInflatedBtn->setEnabled(true);
}

void SimplifierModule::applyInflate(double offset) {
    if (this->baseSimplifiedPositions.empty()) return;
    for (size_t i = 0; i < this->simplifiedMesh->vertices.size(); i++) {
        if (!this->simplifiedMesh->vertices[i].removed)
            this->simplifiedMesh->vertices[i].pos =
                this->baseSimplifiedPositions[i] + offset * this->simplifiedVertexNormals[i];
    }
    this->glWidgetSimplified->updateMeshData();
    this->glWidgetOverlay->updateSecondaryMesh();
}

void SimplifierModule::updateStats() {
    if (!this->originalMesh || !this->simplifiedMesh) return;

    this->glWidgetOriginal->setStats(this->originalMesh->faceCount(),
                                     this->originalMesh->vertexCount());
    this->glWidgetSimplified->setStats(this->simplifiedMesh->faceCount(),
                                       this->simplifiedMesh->vertexCount());

    if (this->simplifiedMesh->faceCount() > 0 && this->originalMesh->faceCount() > 0) {
        double reduction = 100.0 * (1.0 - (double)this->simplifiedMesh->faceCount() /
                                              this->originalMesh->faceCount());
        emit statusMessage(QString("Reduction: %1%").arg(reduction, 0, 'f', 1));
    } else {
        emit statusMessage("Ready");
    }
}
