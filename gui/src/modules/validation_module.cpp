/**
 * @file validation_module.cpp
 * @brief ValidationModule implementation: pipeline configuration UI, PSNR
 *        scoring via offscreen rendering, and the auto-optimize search.
 */
#include "gui/modules/validation_module.h"

#include <QCoreApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <algorithm>
#include <cmath>
#include <limits>

#include "relief/metrics.h"

// ─── buildUI ─────────────────────────────────────────────────────────────────

void ValidationModule::buildUI() {
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Viewports: original + one per candidate slot ─────────────────────────
    QWidget* viewportArea = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportArea);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);
    viewportArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    std::vector<Orbital3DView*> allViews;

    QWidget* originalGroup = new QWidget();
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* originalLayout = new QVBoxLayout(originalGroup);
    originalLayout->setContentsMargins(0, 0, 0, 0);
    this->originalView_ = new Orbital3DView(RenderMode::Solid, "Original");
    this->originalView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    originalLayout->addWidget(this->originalView_, 1);
    viewportsLayout->addWidget(originalGroup);
    allViews.push_back(this->originalView_);

    for (int i = 0; i < kMaxSlots; i++) {
        Slot& slot = this->slots_[i];

        QWidget* group = new QWidget();
        group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        QVBoxLayout* groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);

        QHBoxLayout* header = new QHBoxLayout();
        header->addWidget(new QLabel(QString("Candidate %1").arg(i + 1)));
        slot.visibleCheck = new QCheckBox("Visible");
        slot.visibleCheck->setChecked(true);
        header->addWidget(slot.visibleCheck);
        header->addStretch(1);
        groupLayout->addLayout(header);

        slot.psnrLabel = new QLabel("PSNR: —");
        groupLayout->addWidget(slot.psnrLabel);

        slot.view = new Orbital3DView(RenderMode::Solid, QString("Candidate %1").arg(i + 1));
        slot.view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        groupLayout->addWidget(slot.view, 1);

        connect(slot.visibleCheck, &QCheckBox::toggled, slot.view, &QWidget::setVisible);

        viewportsLayout->addWidget(group);
        allViews.push_back(slot.view);
    }

    for (auto* a : allViews)
        for (auto* b : allViews)
            if (a != b) connect(a, &Orbital3DView::cameraChanged, b, &Orbital3DView::syncCamera);

    splitter->addWidget(viewportArea);

    // ── Controls: manual pipelines + auto-optimize tabs ──────────────────────
    QTabWidget* tabs = new QTabWidget();
    tabs->setMinimumWidth(360);

    QWidget* manualTab = new QWidget();
    QVBoxLayout* manualLayout = new QVBoxLayout(manualTab);
    for (int i = 0; i < kMaxSlots; i++) buildSlotUI(manualLayout, i);
    manualLayout->addStretch(1);
    QScrollArea* manualScroll = new QScrollArea();
    manualScroll->setWidgetResizable(true);
    manualScroll->setWidget(manualTab);
    tabs->addTab(manualScroll, "Manual pipelines");

    QWidget* autoTab = new QWidget();
    QVBoxLayout* autoLayout = new QVBoxLayout(autoTab);
    buildAutoOptimizeUI(autoLayout);
    tabs->addTab(autoTab, "Auto-optimize");

    splitter->addWidget(tabs);
}

void ValidationModule::buildStepUI(QVBoxLayout* slotLayout, int slotIndex, int stepIndex) {
    StepUI& step = this->slots_[slotIndex].steps[stepIndex];

    QHBoxLayout* row = new QHBoxLayout();
    row->addWidget(new QLabel(QString("Step %1:").arg(stepIndex + 1)));

    step.opCombo = new QComboBox();
    step.opCombo->addItem("None");
    step.opCombo->addItem("Simplify");
    step.opCombo->addItem("Inflate");
    step.opCombo->addItem("Smooth");
    row->addWidget(step.opCombo);

    step.paramsStack = new QStackedWidget();

    step.paramsStack->addWidget(new QWidget()); // None: no params

    QWidget* simplifyPage = new QWidget();
    QHBoxLayout* simplifyLayout = new QHBoxLayout(simplifyPage);
    simplifyLayout->setContentsMargins(0, 0, 0, 0);
    step.simplifyLabel = new QLabel("50%");
    step.simplifySlider = new QSlider(Qt::Horizontal);
    step.simplifySlider->setMinimum(1);
    step.simplifySlider->setMaximum(100);
    step.simplifySlider->setValue(50);
    connect(step.simplifySlider, &QSlider::valueChanged, step.simplifyLabel,
            [label = step.simplifyLabel](int v) { label->setText(QString("%1%").arg(v)); });
    simplifyLayout->addWidget(step.simplifySlider, 1);
    simplifyLayout->addWidget(step.simplifyLabel);
    step.paramsStack->addWidget(simplifyPage);

    QWidget* inflatePage = new QWidget();
    QHBoxLayout* inflateLayout = new QHBoxLayout(inflatePage);
    inflateLayout->setContentsMargins(0, 0, 0, 0);
    step.inflateSpin = new QDoubleSpinBox();
    step.inflateSpin->setRange(-1e6, 1e6);
    step.inflateSpin->setDecimals(5);
    step.inflateSpin->setSingleStep(0.001);
    inflateLayout->addWidget(new QLabel("Offset:"));
    inflateLayout->addWidget(step.inflateSpin, 1);
    step.paramsStack->addWidget(inflatePage);

    QWidget* smoothPage = new QWidget();
    QHBoxLayout* smoothLayout = new QHBoxLayout(smoothPage);
    smoothLayout->setContentsMargins(0, 0, 0, 0);
    step.smoothIterSpin = new QSpinBox();
    step.smoothIterSpin->setRange(1, 50);
    step.smoothIterSpin->setValue(2);
    step.smoothLambdaSpin = new QDoubleSpinBox();
    step.smoothLambdaSpin->setRange(0.0, 1.0);
    step.smoothLambdaSpin->setSingleStep(0.05);
    step.smoothLambdaSpin->setValue(0.5);
    smoothLayout->addWidget(new QLabel("Iter:"));
    smoothLayout->addWidget(step.smoothIterSpin);
    smoothLayout->addWidget(new QLabel("Lambda:"));
    smoothLayout->addWidget(step.smoothLambdaSpin);
    step.paramsStack->addWidget(smoothPage);

    connect(step.opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), step.paramsStack,
            &QStackedWidget::setCurrentIndex);

    row->addWidget(step.paramsStack, 1);
    slotLayout->addLayout(row);
}

void ValidationModule::buildSlotUI(QVBoxLayout* manualTabLayout, int slotIndex) {
    QGroupBox* group = new QGroupBox(QString("Candidate %1 pipeline").arg(slotIndex + 1));
    QVBoxLayout* groupLayout = new QVBoxLayout(group);

    for (int s = 0; s < kStepsPerSlot; s++) buildStepUI(groupLayout, slotIndex, s);

    QPushButton* runBtn = new QPushButton("Run");
    connect(runBtn, &QPushButton::clicked, this, [this, slotIndex]() { runSlot(slotIndex); });
    groupLayout->addWidget(runBtn);

    manualTabLayout->addWidget(group);
}

void ValidationModule::buildAutoOptimizeUI(QVBoxLayout* layout) {
    QGroupBox* opsGroup = new QGroupBox("Operations to include");
    QVBoxLayout* opsLayout = new QVBoxLayout(opsGroup);
    this->includeSimplifyCheck_ = new QCheckBox("Simplify");
    this->includeSimplifyCheck_->setChecked(true);
    this->includeInflateCheck_ = new QCheckBox("Inflate");
    this->includeInflateCheck_->setChecked(true);
    this->includeSmoothCheck_ = new QCheckBox("Smooth");
    this->includeSmoothCheck_->setChecked(true);
    opsLayout->addWidget(this->includeSimplifyCheck_);
    opsLayout->addWidget(this->includeInflateCheck_);
    opsLayout->addWidget(this->includeSmoothCheck_);
    layout->addWidget(opsGroup);

    QGroupBox* targetGroup = new QGroupBox("Target reduction (used when Simplify is included)");
    QHBoxLayout* targetLayout = new QHBoxLayout(targetGroup);
    this->targetReductionSlider_ = new QSlider(Qt::Horizontal);
    this->targetReductionSlider_->setMinimum(1);
    this->targetReductionSlider_->setMaximum(100);
    this->targetReductionSlider_->setValue(40);
    this->targetReductionLabel_ = new QLabel("40%");
    connect(this->targetReductionSlider_, &QSlider::valueChanged, this->targetReductionLabel_,
            [label = this->targetReductionLabel_](int v) { label->setText(QString("%1%").arg(v)); });
    targetLayout->addWidget(this->targetReductionSlider_, 1);
    targetLayout->addWidget(this->targetReductionLabel_);
    layout->addWidget(targetGroup);

    this->runSearchBtn_ = new QPushButton("Run Auto-Optimize");
    connect(this->runSearchBtn_, &QPushButton::clicked, this, &ValidationModule::onRunAutoOptimize);
    layout->addWidget(this->runSearchBtn_);

    this->cancelSearchBtn_ = new QPushButton("Cancel");
    this->cancelSearchBtn_->setEnabled(false);
    connect(this->cancelSearchBtn_, &QPushButton::clicked, this,
            [this]() { this->searchCancelRequested_ = true; });
    layout->addWidget(this->cancelSearchBtn_);

    this->searchProgress_ = new QProgressBar();
    this->searchProgress_->setRange(0, 1);
    this->searchProgress_->setValue(0);
    layout->addWidget(this->searchProgress_);

    this->resultsTable_ = new QTableWidget(0, 4);
    this->resultsTable_->setHorizontalHeaderLabels({"Rank", "Pipeline", "PSNR (dB)", "Apply"});
    this->resultsTable_->horizontalHeader()->setStretchLastSection(false);
    this->resultsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    this->resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(this->resultsTable_, 1);
}

// ─── Mesh loading ──────────────────────────────────────────────────────────────

void ValidationModule::onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) {
    Module::onMeshLoaded(original, simplified);
    if (!original) return;

    this->sourceMesh_ = *original;
    this->originalFaceCount_ = this->sourceMesh_.faceCount();
    this->originalView_->setMesh(&this->sourceMesh_);
    this->originalView_->resetCamera();

    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    for (const auto& v : this->sourceMesh_.vertices) {
        if (!v.removed) {
            bmin = bmin.cwiseMin(v.pos);
            bmax = bmax.cwiseMax(v.pos);
        }
    }
    this->inflateScale_ = std::max((bmax - bmin).norm() * 0.5, 1e-6);
    for (auto& slot : this->slots_)
        for (auto& step : slot.steps) step.inflateSpin->setRange(-this->inflateScale_, this->inflateScale_);

    emit statusMessage("Validation: source mesh captured");
}

// ─── Pipeline construction & running ─────────────────────────────────────────

std::optional<pipeline::Op> ValidationModule::stepOp(int slotIndex, int stepIndex) const {
    const StepUI& step = this->slots_[slotIndex].steps[stepIndex];
    pipeline::Op op;
    switch (step.opCombo->currentIndex()) {
        case 0: // None
            return std::nullopt;
        case 1: // Simplify
            op.type = pipeline::OpType::Simplify;
            op.params.targetFaces =
                std::max(4, (int)(this->originalFaceCount_ * step.simplifySlider->value() / 100.0));
            op.params.boundaryMode = simplification::BoundaryMode::Constraint;
            op.params.useOptimalCandidate = true;
            return op;
        case 2: // Inflate
            op.type = pipeline::OpType::Inflate;
            op.params.inflateOffset = step.inflateSpin->value();
            return op;
        case 3: // Smooth
            op.type = pipeline::OpType::Smooth;
            op.params.smoothIterations = step.smoothIterSpin->value();
            op.params.smoothLambda = step.smoothLambdaSpin->value();
            return op;
        default:
            return std::nullopt;
    }
}

pipeline::Pipeline ValidationModule::buildSlotPipeline(int slotIndex) const {
    pipeline::Pipeline p;
    for (int s = 0; s < kStepsPerSlot; s++) {
        if (auto op = stepOp(slotIndex, s)) p.push_back(*op);
    }
    return p;
}

double ValidationModule::scorePipeline(const mesh::Mesh& source, const pipeline::Pipeline& p) {
    mesh::Mesh candidate = source;
    pipeline::applyPipeline(candidate, p);

    std::vector<QImage> originalViews = this->offscreenRenderer_.renderViews(source);
    std::vector<QImage> candidateViews = this->offscreenRenderer_.renderViews(candidate);

    std::vector<std::pair<const uint8_t*, const uint8_t*>> pairs;
    pairs.reserve(originalViews.size());
    for (size_t i = 0; i < originalViews.size() && i < candidateViews.size(); i++)
        pairs.push_back({originalViews[i].constBits(), candidateViews[i].constBits()});

    if (pairs.empty()) return 0.0;
    int width = originalViews.front().width();
    int height = originalViews.front().height();
    return metrics::averagePSNR(pairs, width, height, 4);
}

void ValidationModule::runSlot(int slotIndex) {
    if (this->sourceMesh_.faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    Slot& slot = this->slots_[slotIndex];
    pipeline::Pipeline p = buildSlotPipeline(slotIndex);

    slot.resultMesh = this->sourceMesh_;
    pipeline::applyPipeline(slot.resultMesh, p);

    slot.view->setMesh(&slot.resultMesh);

    double psnr = scorePipeline(this->sourceMesh_, p);
    slot.psnrLabel->setText(psnr == std::numeric_limits<double>::infinity()
                                 ? "PSNR: identical"
                                 : QString("PSNR: %1 dB").arg(psnr, 0, 'f', 2));

    emit statusMessage(QString("Candidate %1: %2").arg(slotIndex + 1).arg(QString::fromStdString(pipeline::describe(p))));
}

// ─── Auto-optimize ────────────────────────────────────────────────────────────

void ValidationModule::onRunAutoOptimize() {
    if (this->sourceMesh_.faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    pipeline::SearchConfig cfg;
    if (this->includeSimplifyCheck_->isChecked()) {
        cfg.ops.push_back(pipeline::OpType::Simplify);
        int percent = this->targetReductionSlider_->value();
        cfg.fixedTargetFaces = std::max(4, (int)(this->originalFaceCount_ * percent / 100.0));
    }
    if (this->includeInflateCheck_->isChecked()) {
        cfg.ops.push_back(pipeline::OpType::Inflate);
        cfg.inflateRange = {-this->inflateScale_, this->inflateScale_};
        cfg.inflateSamples = 5;
    }
    if (this->includeSmoothCheck_->isChecked()) {
        cfg.ops.push_back(pipeline::OpType::Smooth);
    }
    if (cfg.ops.empty()) {
        QMessageBox::warning(this, "Warning", "Select at least one operation to include.");
        return;
    }

    this->searchCancelRequested_ = false;
    this->runSearchBtn_->setEnabled(false);
    this->cancelSearchBtn_->setEnabled(true);
    this->resultsTable_->setRowCount(0);
    this->lastSearchResults_.clear();

    auto scoreFn = [this](const mesh::Mesh& source, const pipeline::Pipeline& p) {
        return scorePipeline(source, p);
    };
    auto progressFn = [this](int done, int total) {
        this->searchProgress_->setRange(0, total);
        this->searchProgress_->setValue(done);
        QCoreApplication::processEvents();
        return !this->searchCancelRequested_;
    };

    std::vector<pipeline::SearchCandidate> results =
        pipeline::search(this->sourceMesh_, cfg, scoreFn, /*topN=*/5, progressFn);

    this->resultsTable_->setRowCount((int)results.size());
    for (size_t i = 0; i < results.size(); i++) {
        this->lastSearchResults_.push_back(results[i].pipeline);

        this->resultsTable_->setItem((int)i, 0, new QTableWidgetItem(QString::number(i + 1)));
        this->resultsTable_->setItem(
            (int)i, 1, new QTableWidgetItem(QString::fromStdString(pipeline::describe(results[i].pipeline))));
        QString psnrText = results[i].psnr == std::numeric_limits<double>::infinity()
                                ? "identical"
                                : QString::number(results[i].psnr, 'f', 2);
        this->resultsTable_->setItem((int)i, 2, new QTableWidgetItem(psnrText));

        QWidget* applyCell = new QWidget();
        QHBoxLayout* applyLayout = new QHBoxLayout(applyCell);
        applyLayout->setContentsMargins(0, 0, 0, 0);
        QComboBox* slotCombo = new QComboBox();
        for (int s = 0; s < kMaxSlots; s++) slotCombo->addItem(QString("Slot %1").arg(s + 1));
        QPushButton* applyBtn = new QPushButton("Apply");
        int row = (int)i;
        connect(applyBtn, &QPushButton::clicked, this,
                [this, row, slotCombo]() { applySearchResultToSlot(row, slotCombo->currentIndex()); });
        applyLayout->addWidget(slotCombo);
        applyLayout->addWidget(applyBtn);
        this->resultsTable_->setCellWidget((int)i, 3, applyCell);
    }

    this->runSearchBtn_->setEnabled(true);
    this->cancelSearchBtn_->setEnabled(false);
    emit statusMessage(QString("Auto-optimize: %1 candidates evaluated").arg((int)results.size()));
}

void ValidationModule::applySearchResultToSlot(int resultRow, int slotIndex) {
    if (resultRow < 0 || resultRow >= (int)this->lastSearchResults_.size()) return;
    if (slotIndex < 0 || slotIndex >= kMaxSlots) return;

    const pipeline::Pipeline& p = this->lastSearchResults_[resultRow];
    Slot& slot = this->slots_[slotIndex];

    slot.resultMesh = this->sourceMesh_;
    pipeline::applyPipeline(slot.resultMesh, p);
    slot.view->setMesh(&slot.resultMesh);

    double psnr = scorePipeline(this->sourceMesh_, p);
    slot.psnrLabel->setText(psnr == std::numeric_limits<double>::infinity()
                                 ? "PSNR: identical"
                                 : QString("PSNR: %1 dB").arg(psnr, 0, 'f', 2));

    emit statusMessage(QString("Applied search result to Candidate %1").arg(slotIndex + 1));
}
