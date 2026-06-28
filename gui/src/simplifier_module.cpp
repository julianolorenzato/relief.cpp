#include "gui/simplifier_module.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QMessageBox>
#include <algorithm>
#include <map>
#include <tuple>
#include <cmath>

// ─── Constructor ─────────────────────────────────────────────────────────────

SimplifierModule::SimplifierModule(QWidget* parent)
    : QWidget(parent)
{
    originalMesh_   = std::make_unique<QEMSimplifier>();
    simplifiedMesh_ = std::make_unique<QEMSimplifier>();
    buildUI();
}

// ─── buildUI ─────────────────────────────────────────────────────────────────

void SimplifierModule::buildUI()
{
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: viewport area (3 Orbital3DViews side by side) ──────────────────
    QWidget* viewportArea = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportArea);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    QWidget* originalGroup = new QWidget();
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* leftLayout = new QVBoxLayout(originalGroup);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    glWidgetOriginal_ = new Orbital3DView(RenderMode::Solid, "Original Mesh");
    glWidgetOriginal_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(glWidgetOriginal_, 1);
    viewportsLayout->addWidget(originalGroup);

    QWidget* simplifiedGroup = new QWidget();
    simplifiedGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* rightLayout = new QVBoxLayout(simplifiedGroup);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    glWidgetSimplified_ = new Orbital3DView(RenderMode::Solid, "Simplified Mesh");
    glWidgetSimplified_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(glWidgetSimplified_, 1);
    viewportsLayout->addWidget(simplifiedGroup);

    glWidgetOverlay_ = new Orbital3DView(RenderMode::Overlay, "Overlay");
    glWidgetOverlay_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(glWidgetOverlay_);

    viewportArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    connect(glWidgetOriginal_,  &Orbital3DView::cameraChanged, glWidgetSimplified_, &Orbital3DView::syncCamera);
    connect(glWidgetOriginal_,  &Orbital3DView::cameraChanged, glWidgetOverlay_,    &Orbital3DView::syncCamera);
    connect(glWidgetSimplified_,&Orbital3DView::cameraChanged, glWidgetOriginal_,   &Orbital3DView::syncCamera);
    connect(glWidgetSimplified_,&Orbital3DView::cameraChanged, glWidgetOverlay_,    &Orbital3DView::syncCamera);
    connect(glWidgetOverlay_,   &Orbital3DView::cameraChanged, glWidgetOriginal_,   &Orbital3DView::syncCamera);
    connect(glWidgetOverlay_,   &Orbital3DView::cameraChanged, glWidgetSimplified_, &Orbital3DView::syncCamera);

    splitter->addWidget(viewportArea);

    // ── Right: controls in a QScrollArea ─────────────────────────────────────
    QWidget* controlsWidget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(controlsWidget);
    layout->setContentsMargins(4, 4, 4, 4);

    // ── Simplification Controls ────────────────────────────────────────────
    QGroupBox* controlsGroup = new QGroupBox("Simplification");
    QVBoxLayout* controlsRows = new QVBoxLayout(controlsGroup);
    controlsRows->setSpacing(4);

    QHBoxLayout* facesRow = new QHBoxLayout();
    facesRow->addWidget(new QLabel("Target Faces:"));
    targetFacesSpinBox_ = new QSpinBox();
    targetFacesSpinBox_->setMinimum(4);
    targetFacesSpinBox_->setMaximum(1000000);
    targetFacesSpinBox_->setValue(1000);
    facesRow->addWidget(targetFacesSpinBox_, 1);
    controlsRows->addLayout(facesRow);

    simplificationSlider_ = new QSlider(Qt::Horizontal);
    simplificationSlider_->setMinimum(1);
    simplificationSlider_->setMaximum(100);
    simplificationSlider_->setValue(50);
    controlsRows->addWidget(simplificationSlider_);

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &SimplifierModule::onSimplify);
    btnRow->addWidget(simplifyBtn);
    QPushButton* resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &SimplifierModule::onResetCameras);
    btnRow->addWidget(resetCamBtn);
    controlsRows->addLayout(btnRow);

    wireframeCheck_ = new QCheckBox("Wireframe");
    connect(wireframeCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setWireframe);
    connect(wireframeCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setWireframe);
    controlsRows->addWidget(wireframeCheck_);

    texturedCheck_ = new QCheckBox("Textured");
    texturedCheck_->setEnabled(false);
    connect(texturedCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setTextured);
    connect(texturedCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setTextured);
    controlsRows->addWidget(texturedCheck_);

    cullFaceCheck_ = new QCheckBox("Backface Cull");
    cullFaceCheck_->setChecked(true);
    connect(cullFaceCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setCullFace);
    connect(cullFaceCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setCullFace);
    controlsRows->addWidget(cullFaceCheck_);

    uvViewCheck_ = new QCheckBox("UV View");
    uvViewCheck_->setEnabled(false);
    connect(uvViewCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setUVMode);
    connect(uvViewCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setUVMode);
    controlsRows->addWidget(uvViewCheck_);

    QHBoxLayout* boundaryRow = new QHBoxLayout();
    boundaryRow->addWidget(new QLabel("Boundary:"));
    boundaryModeCombo_ = new QComboBox();
    boundaryModeCombo_->addItem("No constraint",       (int)BoundaryMode::None);
    boundaryModeCombo_->addItem("Constraint",          (int)BoundaryMode::Constraint);
    boundaryModeCombo_->addItem("Lock seam edges",     (int)BoundaryMode::LockSeamVertices);
    boundaryModeCombo_->addItem("Sync seam twins",     (int)BoundaryMode::SyncSeamTwins);
    boundaryModeCombo_->setCurrentIndex(1);
    boundaryRow->addWidget(boundaryModeCombo_, 1);
    controlsRows->addLayout(boundaryRow);

    envelopeConstraintCheck_ = new QCheckBox("Envelope Constraint");
    envelopeConstraintCheck_->setToolTip(
        "Garante que a malha simplificada fique sempre do lado de fora (ou sobre)\n"
        "a malha original. Pode travar colapsos em regioes muito concavas, entao\n"
        "a malha final pode nao atingir a contagem de faces alvo.");
    controlsRows->addWidget(envelopeConstraintCheck_);

    useOptimalCandidateCheck_ = new QCheckBox("Use Optimal Candidate");
    useOptimalCandidateCheck_->setToolTip(
        "Soma o otimo irrestrito da quadrica como mais um candidato de posicao\n"
        "de colapso, alem de v1, v2 e ponto medio.");
    controlsRows->addWidget(useOptimalCandidateCheck_);

    showBoundaryEdgesCheck_ = new QCheckBox("Show Boundary Edges");
    connect(showBoundaryEdgesCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setShowBoundaryEdges);
    connect(showBoundaryEdgesCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setShowBoundaryEdges);
    controlsRows->addWidget(showBoundaryEdgesCheck_);

    showInternalEdgesCheck_ = new QCheckBox("Show Internal Edges");
    connect(showInternalEdgesCheck_, &QCheckBox::toggled, glWidgetOriginal_,   &Orbital3DView::setShowInternalEdges);
    connect(showInternalEdgesCheck_, &QCheckBox::toggled, glWidgetSimplified_, &Orbital3DView::setShowInternalEdges);
    controlsRows->addWidget(showInternalEdgesCheck_);

    layout->addWidget(controlsGroup);

    // ── Inflate / Deflate ──────────────────────────────────────────────────
    QGroupBox* inflateGroup = new QGroupBox("Inflate / Deflate");
    QVBoxLayout* inflateLayout = new QVBoxLayout(inflateGroup);
    inflateLayout->setSpacing(4);

    QHBoxLayout* inflateValRow = new QHBoxLayout();
    inflateValRow->addWidget(new QLabel("Offset:"));
    inflateSpin_ = new QDoubleSpinBox();
    inflateSpin_->setMinimum(-1e6);
    inflateSpin_->setMaximum(1e6);
    inflateSpin_->setValue(0.0);
    inflateSpin_->setDecimals(5);
    inflateSpin_->setSingleStep(0.001);
    inflateSpin_->setEnabled(false);
    inflateValRow->addWidget(inflateSpin_, 1);
    inflateLayout->addLayout(inflateValRow);

    inflateSlider_ = new QSlider(Qt::Horizontal);
    inflateSlider_->setMinimum(-1000);
    inflateSlider_->setMaximum(1000);
    inflateSlider_->setValue(0);
    inflateSlider_->setEnabled(false);
    inflateLayout->addWidget(inflateSlider_);

    connect(inflateSlider_, &QSlider::valueChanged, this, [this](int val) {
        double offset = (inflateScale_ > 1e-10) ? val / 1000.0 * inflateScale_ : 0.0;
        inflateSpin_->blockSignals(true);
        inflateSpin_->setValue(offset);
        inflateSpin_->blockSignals(false);
        applyInflate(offset);
    });
    connect(inflateSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        int sliderVal = (inflateScale_ > 1e-10) ? (int)(val / inflateScale_ * 1000.0) : 0;
        inflateSlider_->blockSignals(true);
        inflateSlider_->setValue(std::max(-1000, std::min(1000, sliderVal)));
        inflateSlider_->blockSignals(false);
        applyInflate(val);
    });

    layout->addWidget(inflateGroup);

    // ── Signals ───────────────────────────────────────────────────────────
    connect(targetFacesSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SimplifierModule::onTargetFacesChanged);
    connect(simplificationSlider_, &QSlider::valueChanged, this, [this](int val) {
        int targetFaces = std::max(4, (int)(originalFaceCount_ * val / 100.0));
        targetFacesSpinBox_->blockSignals(true);
        targetFacesSpinBox_->setValue(targetFaces);
        targetFacesSpinBox_->blockSignals(false);
    });

    layout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}

// ─── Public methods ───────────────────────────────────────────────────────────

bool SimplifierModule::loadModel(const QString& path)
{
    originalMesh_   = std::make_unique<QEMSimplifier>();
    simplifiedMesh_ = std::make_unique<QEMSimplifier>();

    bool success = false;
    if (path.endsWith(".obj", Qt::CaseInsensitive))
        success = originalMesh_->loadOBJ(path.toStdString());
    else
        success = originalMesh_->loadGLTF(path.toStdString());

    if (!success)
        return false;

    // Start "simplified" as a copy of the original so Textures Preparation /
    // Relief Mapping work even before the user runs Simplify.
    *simplifiedMesh_ = *originalMesh_;

    originalFaceCount_ = originalMesh_->faceCount();
    targetFaceCount_   = std::max(4, originalFaceCount_ / 4);

    targetFacesSpinBox_->blockSignals(true);
    targetFacesSpinBox_->setMaximum(originalFaceCount_);
    targetFacesSpinBox_->setValue(targetFaceCount_);
    simplificationSlider_->setValue(75);
    targetFacesSpinBox_->blockSignals(false);

    glWidgetOriginal_->setMesh(originalMesh_.get());
    glWidgetSimplified_->setMesh(originalMesh_.get());
    glWidgetOverlay_->setMeshes(originalMesh_.get(), originalMesh_.get());

    bool hasTexture = !originalMesh_->textureData.empty();
    texturedCheck_->setEnabled(hasTexture);
    if (!hasTexture)
        texturedCheck_->setChecked(false);

    bool hasUVs = false;
    for (const auto& v : originalMesh_->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    uvViewCheck_->setEnabled(hasUVs);
    if (!hasUVs)
        uvViewCheck_->setChecked(false);

    // Reset inflate/deflate controls
    baseSimplifiedPositions_.clear();
    simplifiedVertexNormals_.clear();
    simplifiedVertexGroup_.clear();
    simplifiedVertexGroupCount_ = 0;
    inflateSlider_->blockSignals(true);
    inflateSlider_->setValue(0);
    inflateSlider_->blockSignals(false);
    inflateSlider_->setEnabled(false);
    inflateSpin_->blockSignals(true);
    inflateSpin_->setValue(0.0);
    inflateSpin_->blockSignals(false);
    inflateSpin_->setEnabled(false);

    updateStats();
    emit modelLoaded(originalMesh_.get(), simplifiedMesh_.get());
    return true;
}

bool SimplifierModule::saveSimplified(const QString& path)
{
    if (!simplifiedMesh_ || simplifiedMesh_->faceCount() == 0)
        return false;

    bool success = false;
    if (path.endsWith(".obj", Qt::CaseInsensitive))
        success = simplifiedMesh_->saveOBJ(path.toStdString());
    else
        success = simplifiedMesh_->saveGLTF(path.toStdString());

    return success;
}

// ─── Private slots ────────────────────────────────────────────────────────────

void SimplifierModule::onSimplify()
{
    if (!originalMesh_ || originalMesh_->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    int targetFaces = targetFacesSpinBox_->value();
    *simplifiedMesh_ = *originalMesh_;
    simplifiedMesh_->boundaryMode      = (BoundaryMode)boundaryModeCombo_->currentData().toInt();
    simplifiedMesh_->envelopeConstraint = envelopeConstraintCheck_->isChecked();
    simplifiedMesh_->useOptimalCandidate = useOptimalCandidateCheck_->isChecked();

    emit statusMessage("Simplifying...");

    simplifiedMesh_->simplify(targetFaces);

    // Capture base positions and compute vertex normals for inflate/deflate
    baseSimplifiedPositions_.resize(simplifiedMesh_->vertices.size());
    for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
        baseSimplifiedPositions_[i] = simplifiedMesh_->vertices[i].pos;

    simplifiedVertexNormals_.assign(simplifiedMesh_->vertices.size(), Eigen::Vector3d::Zero());
    for (const auto& f : simplifiedMesh_->faces)
    {
        if (f.removed)
            continue;
        const auto& p0 = baseSimplifiedPositions_[f.v[0]];
        const auto& p1 = baseSimplifiedPositions_[f.v[1]];
        const auto& p2 = baseSimplifiedPositions_[f.v[2]];
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        simplifiedVertexNormals_[f.v[0]] += n;
        simplifiedVertexNormals_[f.v[1]] += n;
        simplifiedVertexNormals_[f.v[2]] += n;
    }

    // Vértices duplicados na mesma posição 3D (ex.: costuras de UV, separadas
    // em vertices distintos no loadOBJ) devem inflar juntos. Caso contrário,
    // cada cópia usa só suas próprias faces incidentes, as normais divergem,
    // e a costura abre um buraco ao inflar mesmo com seam vertices travados.
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto& p : baseSimplifiedPositions_)
        {
            bmin = bmin.cwiseMin(p);
            bmax = bmax.cwiseMax(p);
        }
        double cell = std::max((bmax - bmin).norm() * 1e-7, 1e-9);

        auto quantize = [cell](const Eigen::Vector3d& p) {
            return std::make_tuple(
                (long long)std::llround(p.x() / cell),
                (long long)std::llround(p.y() / cell),
                (long long)std::llround(p.z() / cell));
        };

        std::map<std::tuple<long long, long long, long long>, int> groupId;
        simplifiedVertexGroup_.assign(simplifiedMesh_->vertices.size(), -1);
        for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
        {
            if (simplifiedMesh_->vertices[i].removed)
                continue;
            auto key = quantize(baseSimplifiedPositions_[i]);
            auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
            simplifiedVertexGroup_[i] = it->second;
        }
        simplifiedVertexGroupCount_ = (int)groupId.size();

        std::vector<Eigen::Vector3d> groupNormal(simplifiedVertexGroupCount_, Eigen::Vector3d::Zero());
        for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
        {
            if (simplifiedMesh_->vertices[i].removed)
                continue;
            groupNormal[simplifiedVertexGroup_[i]] += simplifiedVertexNormals_[i];
        }
        for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
        {
            if (simplifiedMesh_->vertices[i].removed)
                continue;
            simplifiedVertexNormals_[i] = groupNormal[simplifiedVertexGroup_[i]];
        }
    }

    for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
    {
        double len = simplifiedVertexNormals_[i].norm();
        if (len > 1e-10)
            simplifiedVertexNormals_[i] /= len;
    }

    // Set inflate range based on original mesh bounding box diagonal
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto& v : originalMesh_->vertices)
        {
            if (!v.removed)
            {
                bmin = bmin.cwiseMin(v.pos);
                bmax = bmax.cwiseMax(v.pos);
            }
        }
        inflateScale_ = std::max((bmax - bmin).norm() * 0.5, 1e-6);
    }

    inflateSpin_->blockSignals(true);
    inflateSpin_->setMinimum(-inflateScale_);
    inflateSpin_->setMaximum(inflateScale_);
    inflateSpin_->setSingleStep(inflateScale_ / 1000.0);
    inflateSpin_->setValue(0.0);
    inflateSpin_->blockSignals(false);
    inflateSlider_->blockSignals(true);
    inflateSlider_->setValue(0);
    inflateSlider_->blockSignals(false);
    inflateSlider_->setEnabled(true);
    inflateSpin_->setEnabled(true);

    glWidgetSimplified_->setMesh(simplifiedMesh_.get());
    glWidgetOverlay_->setMeshes(originalMesh_.get(), simplifiedMesh_.get());
    updateStats();

    emit simplificationDone(originalMesh_.get(), simplifiedMesh_.get());
}

void SimplifierModule::onTargetFacesChanged(int value)
{
    targetFaceCount_ = value;
}

void SimplifierModule::onResetCameras()
{
    if (glWidgetOriginal_)   glWidgetOriginal_->resetCamera();
    if (glWidgetSimplified_) glWidgetSimplified_->resetCamera();
    if (glWidgetOverlay_)    glWidgetOverlay_->resetCamera();
}

// ─── Private methods ──────────────────────────────────────────────────────────

void SimplifierModule::applyInflate(double offset)
{
    if (baseSimplifiedPositions_.empty())
        return;
    for (size_t i = 0; i < simplifiedMesh_->vertices.size(); i++)
    {
        if (!simplifiedMesh_->vertices[i].removed)
            simplifiedMesh_->vertices[i].pos =
                baseSimplifiedPositions_[i] + offset * simplifiedVertexNormals_[i];
    }
    glWidgetSimplified_->updateMeshData();
    glWidgetOverlay_->updateSecondaryMesh();
}

void SimplifierModule::updateStats()
{
    if (!originalMesh_ || !simplifiedMesh_)
        return;

    glWidgetOriginal_->setStats(originalMesh_->faceCount(), originalMesh_->vertexCount());
    glWidgetSimplified_->setStats(simplifiedMesh_->faceCount(), simplifiedMesh_->vertexCount());

    if (simplifiedMesh_->faceCount() > 0 && originalMesh_->faceCount() > 0)
    {
        double reduction = 100.0 *
            (1.0 - (double)simplifiedMesh_->faceCount() / originalMesh_->faceCount());
        emit statusMessage(QString("Reduction: %1%").arg(reduction, 0, 'f', 1));
    }
    else
    {
        emit statusMessage("Ready");
    }
}
