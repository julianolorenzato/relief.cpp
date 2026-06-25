#include "mainwindow.h"
#include "overlayglwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QScrollArea>
#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QStackedWidget>
#include <Qt>
#include <iostream>
#include <algorithm>
#include <map>
#include <tuple>
#include <cmath>

namespace {
QImage rgbaTextureToQImage(const std::vector<uint8_t>& data, int w, int h) {
    if (data.empty() || w <= 0 || h <= 0) return QImage();
    QImage img(data.data(), w, h, w * 4, QImage::Format_RGBA8888);
    return img.copy(); // detach from the mesh's buffer
}
} // namespace

// ─── Constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("QEM Mesh Simplifier");
    setGeometry(100, 100, 1600, 900);

    originalMesh   = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    setupUI();
    createMenuBar();
    updateStatusBar();
}

// ─── Tab builders ─────────────────────────────────────────────────────────────

QWidget* MainWindow::buildSimplifierTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(tab);

    // ── Viewports ────────────────────────────────────────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);

    QGroupBox* originalGroup = new QGroupBox("Original Mesh");
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* leftLayout = new QVBoxLayout(originalGroup);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    glWidgetOriginal = new GLWidget();
    glWidgetOriginal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(glWidgetOriginal);
    originalStatsLabel = new QLabel("Faces: 0 | Vertices: 0");
    originalStatsLabel->setFixedHeight(20);
    leftLayout->addWidget(originalStatsLabel);
    viewportsLayout->addWidget(originalGroup);

    QGroupBox* simplifiedGroup = new QGroupBox("Simplified Mesh");
    simplifiedGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* rightLayout = new QVBoxLayout(simplifiedGroup);
    rightLayout->setContentsMargins(4, 4, 4, 4);
    glWidgetSimplified = new GLWidget();
    glWidgetSimplified->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(glWidgetSimplified);
    simplifiedStatsLabel = new QLabel("Faces: 0 | Vertices: 0");
    simplifiedStatsLabel->setFixedHeight(20);
    rightLayout->addWidget(simplifiedStatsLabel);
    viewportsLayout->addWidget(simplifiedGroup);

    QGroupBox* overlayGroup = new QGroupBox("Overlay  (azul = original · laranja = simplificada)");
    overlayGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* overlayLayout = new QVBoxLayout(overlayGroup);
    overlayLayout->setContentsMargins(4, 4, 4, 4);
    glWidgetOverlay = new OverlayGLWidget();
    glWidgetOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    overlayLayout->addWidget(glWidgetOverlay);
    viewportsLayout->addWidget(overlayGroup);

    viewportsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewportsWidget, 1);

    // ── Controls ─────────────────────────────────────────────────────────────
    QGroupBox* controlsGroup = new QGroupBox("Simplification Controls");
    QVBoxLayout* controlsRows = new QVBoxLayout(controlsGroup);
    QHBoxLayout* controlsLayout = new QHBoxLayout();
    QHBoxLayout* controlsLayout2 = new QHBoxLayout();
    controlsRows->addLayout(controlsLayout);
    controlsRows->addLayout(controlsLayout2);

    controlsLayout->addWidget(new QLabel("Target Faces:"));

    targetFacesSpinBox = new QSpinBox();
    targetFacesSpinBox->setMinimum(4);
    targetFacesSpinBox->setMaximum(1000000);
    targetFacesSpinBox->setValue(1000);
    controlsLayout->addWidget(targetFacesSpinBox);

    simplificationSlider = new QSlider(Qt::Horizontal);
    simplificationSlider->setMinimum(1);
    simplificationSlider->setMaximum(100);
    simplificationSlider->setValue(50);
    controlsLayout->addWidget(simplificationSlider);

    QPushButton* simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &MainWindow::onSimplify);
    controlsLayout->addWidget(simplifyBtn);

    QPushButton* resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &MainWindow::onResetCameras);
    controlsLayout->addWidget(resetCamBtn);

    wireframeCheck = new QCheckBox("Wireframe");
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setWireframe);
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setWireframe);
    controlsLayout->addWidget(wireframeCheck);

    cullFaceCheck = new QCheckBox("Backface Culling");
    cullFaceCheck->setChecked(true);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setCullFace);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setCullFace);
    controlsLayout->addWidget(cullFaceCheck);

    texturedCheck = new QCheckBox("Textured");
    texturedCheck->setEnabled(false);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setTextured);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setTextured);
    controlsLayout->addWidget(texturedCheck);

    uvViewCheck = new QCheckBox("UV View");
    uvViewCheck->setEnabled(false);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setUVMode);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setUVMode);
    controlsLayout->addWidget(uvViewCheck);

    controlsLayout2->addWidget(new QLabel("Boundary:"));
    boundaryModeCombo = new QComboBox();
    boundaryModeCombo->addItem("No constraint",                (int)BoundaryMode::None);
    boundaryModeCombo->addItem("Boundary constraint",          (int)BoundaryMode::Constraint);
    boundaryModeCombo->addItem("Lock seam edges",               (int)BoundaryMode::LockSeamVertices);
    boundaryModeCombo->addItem("Sync seam twins",                (int)BoundaryMode::SyncSeamTwins);
    boundaryModeCombo->setCurrentIndex(1);
    controlsLayout2->addWidget(boundaryModeCombo);

    envelopeConstraintCheck = new QCheckBox("Envelope Constraint");
    envelopeConstraintCheck->setToolTip(
        "Garante que a malha simplificada fique sempre do lado de fora (ou sobre)\n"
        "a malha original. Pode travar colapsos em regioes muito concavas, entao\n"
        "a malha final pode nao atingir a contagem de faces alvo.");
    controlsLayout2->addWidget(envelopeConstraintCheck);

    useOptimalCandidateCheck = new QCheckBox("Use Optimal Candidate");
    useOptimalCandidateCheck->setToolTip(
        "Soma o otimo irrestrito da quadrica como mais um candidato de posicao\n"
        "de colapso, alem de v1, v2 e ponto medio.");
    controlsLayout2->addWidget(useOptimalCandidateCheck);

    showBoundaryEdgesCheck = new QCheckBox("Show Boundary Edges");
    connect(showBoundaryEdgesCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setShowBoundaryEdges);
    connect(showBoundaryEdgesCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setShowBoundaryEdges);
    controlsLayout2->addWidget(showBoundaryEdgesCheck);

    showInternalEdgesCheck = new QCheckBox("Show Internal Edges");
    connect(showInternalEdgesCheck, &QCheckBox::toggled, glWidgetOriginal,   &GLWidget::setShowInternalEdges);
    connect(showInternalEdgesCheck, &QCheckBox::toggled, glWidgetSimplified, &GLWidget::setShowInternalEdges);
    controlsLayout2->addWidget(showInternalEdgesCheck);

    controlsLayout2->addStretch();

    controlsGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(controlsGroup);

    // ── Inflate / Deflate Controls ────────────────────────────────────────────
    QGroupBox* inflateGroup = new QGroupBox("Inflate / Deflate (simplified mesh vertex positions)");
    QHBoxLayout* inflateLayout = new QHBoxLayout(inflateGroup);

    inflateLayout->addWidget(new QLabel("Offset:"));

    inflateSlider = new QSlider(Qt::Horizontal);
    inflateSlider->setMinimum(-1000);
    inflateSlider->setMaximum(1000);
    inflateSlider->setValue(0);
    inflateSlider->setEnabled(false);
    inflateLayout->addWidget(inflateSlider);

    inflateSpin = new QDoubleSpinBox();
    inflateSpin->setMinimum(-1e6);
    inflateSpin->setMaximum(1e6);
    inflateSpin->setValue(0.0);
    inflateSpin->setDecimals(5);
    inflateSpin->setSingleStep(0.001);
    inflateSpin->setFixedWidth(130);
    inflateSpin->setEnabled(false);
    inflateLayout->addWidget(inflateSpin);

    inflateLayout->addSpacing(16);

    smoothCoverBtn = new QPushButton("Smooth Cover");
    smoothCoverBtn->setEnabled(false);
    smoothCoverBtn->setToolTip(
        "Computes a smooth, per-vertex offset field so the inflated cage fully\n"
        "encloses the original mesh, instead of one uniform worst-case offset.\n"
        "The Offset slider/spin then adds a uniform extra margin on top.");
    inflateLayout->addWidget(smoothCoverBtn);

    connect(inflateSlider, &QSlider::valueChanged, this, [this](int val) {
        double offset = (inflateScale > 1e-10) ? val / 1000.0 * inflateScale : 0.0;
        inflateSpin->blockSignals(true);
        inflateSpin->setValue(offset);
        inflateSpin->blockSignals(false);
        applyInflate(offset);
    });
    connect(inflateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        int sliderVal = (inflateScale > 1e-10) ? (int)(val / inflateScale * 1000.0) : 0;
        inflateSlider->blockSignals(true);
        inflateSlider->setValue(std::max(-1000, std::min(1000, sliderVal)));
        inflateSlider->blockSignals(false);
        applyInflate(val);
    });
    connect(smoothCoverBtn, &QPushButton::clicked, this, &MainWindow::onSmoothCover);

    inflateGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(inflateGroup);

    // ── Signals ───────────────────────────────────────────────────────────────
    connect(targetFacesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onTargetFacesChanged);
    connect(simplificationSlider, &QSlider::valueChanged, this, [this](int val) {
        int targetFaces = std::max(4, (int)(originalFaceCount * val / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(targetFaces);
        targetFacesSpinBox->blockSignals(false);
    });

    // Sync cameras: any viewport drives the other two
    connect(glWidgetOriginal,   &GLWidget::cameraChanged,
            glWidgetSimplified, &GLWidget::syncCamera);
    connect(glWidgetOriginal,   &GLWidget::cameraChanged,
            glWidgetOverlay,    &OverlayGLWidget::syncCamera);
    connect(glWidgetSimplified, &GLWidget::cameraChanged,
            glWidgetOriginal,   &GLWidget::syncCamera);
    connect(glWidgetSimplified, &GLWidget::cameraChanged,
            glWidgetOverlay,    &OverlayGLWidget::syncCamera);
    connect(glWidgetOverlay,    &OverlayGLWidget::cameraChanged,
            glWidgetOriginal,   &GLWidget::syncCamera);
    connect(glWidgetOverlay,    &OverlayGLWidget::cameraChanged,
            glWidgetSimplified, &GLWidget::syncCamera);

    return tab;
}

QWidget* MainWindow::buildHeightmapTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(tab);

    // ── Top controls bar ─────────────────────────────────────────────────────
    QGroupBox* ctrlGroup = new QGroupBox("Baking Controls");
    QVBoxLayout* ctrlOuter = new QVBoxLayout(ctrlGroup);

    // Row 1: resolution + bake button
    QHBoxLayout* ctrlRow = new QHBoxLayout();

    ctrlRow->addWidget(new QLabel("Resolution:"));
    hmResCombo = new QComboBox();
    hmResCombo->addItem("128 × 128",   128);
    hmResCombo->addItem("256 × 256",   256);
    hmResCombo->addItem("512 × 512",   512);
    hmResCombo->addItem("1024 × 1024", 1024);
    hmResCombo->addItem("2048 × 2048", 2048);
    hmResCombo->addItem("4096 × 4096", 4096);
    hmResCombo->setCurrentIndex(2);
    ctrlRow->addWidget(hmResCombo);

    ctrlRow->addSpacing(16);
    hmBakeBtn = new QPushButton("Bake");
    hmBakeBtn->setMinimumWidth(120);
    connect(hmBakeBtn, &QPushButton::clicked, this, &MainWindow::onBake);
    ctrlRow->addWidget(hmBakeBtn);
    ctrlRow->addStretch();
    ctrlOuter->addLayout(ctrlRow);

    // Row 2: progress bar + label
    QHBoxLayout* progressRow = new QHBoxLayout();
    hmProgressBar = new QProgressBar();
    hmProgressBar->setRange(0, 100);
    hmProgressBar->setValue(0);
    hmProgressBar->setTextVisible(true);
    hmProgressBar->setFixedHeight(18);
    progressRow->addWidget(hmProgressBar, 1);

    hmProgressLabel = new QLabel("Ready");
    hmProgressLabel->setFixedWidth(280);
    progressRow->addWidget(hmProgressLabel);
    ctrlOuter->addLayout(progressRow);

    ctrlGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mainLayout->addWidget(ctrlGroup);

    // ── Heightmap panel ───────────────────────────────────────────────────────
    QGroupBox* panel = new QGroupBox("UV Correspondence");
    QVBoxLayout* pLayout = new QVBoxLayout(panel);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    hmPreview = new QLabel();
    hmPreview->setAlignment(Qt::AlignCenter);
    hmPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hmPreview->setMinimumSize(200, 200);
    hmPreview->setStyleSheet("background-color: #1e1e1e; border: 1px solid #555;");
    hmPreview->setText("(not baked)");
    pLayout->addWidget(hmPreview, 1);

    hmInfoLabel = new QLabel("Range: —");
    hmInfoLabel->setFixedHeight(18);
    hmInfoLabel->setAlignment(Qt::AlignCenter);
    pLayout->addWidget(hmInfoLabel);

    hmSaveBtn = new QPushButton("Save");
    hmSaveBtn->setEnabled(false);
    connect(hmSaveBtn, &QPushButton::clicked, this, &MainWindow::onSaveHeightmap);
    pLayout->addWidget(hmSaveBtn);

    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(panel, 1);

    return tab;
}

QWidget* MainWindow::buildTexturePrepTab() {
    QWidget* tab = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(tab);

    // ── Top controls ─────────────────────────────────────────────────────────
    QGroupBox* ctrlGroup = new QGroupBox("Input Textures && Baking Controls");
    QVBoxLayout* ctrlOuter = new QVBoxLayout(ctrlGroup);

    static const char* thumbCaptions[3] = {"Color (model)", "Depth (bake)", "Normal (model)"};
    QHBoxLayout* loadRow = new QHBoxLayout();
    for (int i = 0; i < 3; i++) {
        QVBoxLayout* col = new QVBoxLayout();
        tpThumb[i] = new QLabel();
        tpThumb[i]->setFixedSize(64, 64);
        tpThumb[i]->setAlignment(Qt::AlignCenter);
        tpThumb[i]->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        tpThumb[i]->setText("—");
        col->addWidget(tpThumb[i]);

        QLabel* caption = new QLabel(thumbCaptions[i]);
        caption->setAlignment(Qt::AlignCenter);
        col->addWidget(caption);
        loadRow->addLayout(col);
    }
    loadRow->addSpacing(16);

    QVBoxLayout* paramsCol = new QVBoxLayout();
    QHBoxLayout* resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel("Working Resolution:"));
    tpResCombo = new QComboBox();
    tpResCombo->addItem("128 × 128",   128);
    tpResCombo->addItem("256 × 256",   256);
    tpResCombo->addItem("512 × 512",   512);
    tpResCombo->addItem("1024 × 1024", 1024);
    tpResCombo->addItem("2048 × 2048", 2048);
    tpResCombo->setCurrentIndex(2);
    resRow->addWidget(tpResCombo);
    paramsCol->addLayout(resRow);

    QHBoxLayout* seamRow = new QHBoxLayout();
    seamRow->addWidget(new QLabel("Seam Band (texels):"));
    tpSeamBandSpin = new QSpinBox();
    tpSeamBandSpin->setRange(1, 32);
    tpSeamBandSpin->setValue(4);
    tpSeamBandSpin->setToolTip(
        "Width (in texels) of the atlas-leap band baked around UV seams.\n"
        "Wider bands tolerate longer relief-mapping rays crossing islands.");
    seamRow->addWidget(tpSeamBandSpin);
    paramsCol->addLayout(seamRow);
    loadRow->addLayout(paramsCol);
    loadRow->addStretch();

    tpGenerateBtn = new QPushButton("Generate");
    tpGenerateBtn->setMinimumWidth(120);
    tpGenerateBtn->setEnabled(false);
    connect(tpGenerateBtn, &QPushButton::clicked, this, &MainWindow::onTpGenerate);
    loadRow->addWidget(tpGenerateBtn);

    ctrlOuter->addLayout(loadRow);

    QHBoxLayout* progressRow = new QHBoxLayout();
    tpProgressBar = new QProgressBar();
    tpProgressBar->setRange(0, 100);
    tpProgressBar->setValue(0);
    tpProgressBar->setTextVisible(true);
    tpProgressBar->setFixedHeight(18);
    progressRow->addWidget(tpProgressBar, 1);

    tpProgressLabel = new QLabel("Ready");
    tpProgressLabel->setFixedWidth(280);
    progressRow->addWidget(tpProgressLabel);
    ctrlOuter->addLayout(progressRow);

    ctrlGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mainLayout->addWidget(ctrlGroup);

    // ── Preview panels ───────────────────────────────────────────────────────
    static const char* previewTitles[4] = {
        "Color Map", "Relief Map  (R=min G=max(mip-bound) B=offset mask A=—)", "Normal Map", "Offset Map  (atlas leap mask)"
    };

    QWidget* panelsWidget = new QWidget();
    QHBoxLayout* panelsLayout = new QHBoxLayout(panelsWidget);
    panelsLayout->setSpacing(12);

    for (int i = 0; i < 4; i++) {
        QGroupBox* panel = new QGroupBox(previewTitles[i]);
        QVBoxLayout* pLayout = new QVBoxLayout(panel);
        panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        tpPreview[i] = new QLabel();
        tpPreview[i]->setAlignment(Qt::AlignCenter);
        tpPreview[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        tpPreview[i]->setMinimumSize(180, 180);
        tpPreview[i]->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        tpPreview[i]->setText("(not generated)");
        pLayout->addWidget(tpPreview[i], 1);

        tpInfoLabel[i] = new QLabel("—");
        tpInfoLabel[i]->setFixedHeight(18);
        tpInfoLabel[i]->setAlignment(Qt::AlignCenter);
        pLayout->addWidget(tpInfoLabel[i]);

        if (i < 3) {
            static const char* chanLabel[4]   = {"R", "G", "B", "A"};
            static const char* chanTooltip[3][4] = {
                {"Red",                   "Green",                  "Blue",              "Alpha"},
                {"Min depth (mip bound)", "Max depth (mip bound)",  "Offset/seam mask",  "Reserved (always 0)"},
                {"X",                     "Y",                      "Z",                 ""},
            };
            QHBoxLayout* chanRow = new QHBoxLayout();
            chanRow->addWidget(new QLabel("Channels:"));
            for (int c = 0; c < 4; c++) {
                tpChannelCheck[i][c] = new QCheckBox(chanLabel[c]);
                tpChannelCheck[i][c]->setChecked(true);
                tpChannelCheck[i][c]->setToolTip(chanTooltip[i][c]);
                connect(tpChannelCheck[i][c], &QCheckBox::toggled, this, [this, idx = i](bool) { updateTpPreview(idx); });
                chanRow->addWidget(tpChannelCheck[i][c]);
            }
            // Normal map has no alpha channel.
            if (i == 2) {
                tpChannelCheck[i][3]->setChecked(false);
                tpChannelCheck[i][3]->setEnabled(false);
            }
            chanRow->addStretch();
            pLayout->addLayout(chanRow);
        }

        QHBoxLayout* btnRow = new QHBoxLayout();
        btnRow->addWidget(new QLabel("Mip:"));
        tpMipSpin[i] = new QSpinBox();
        tpMipSpin[i]->setRange(0, 0);
        tpMipSpin[i]->setEnabled(false);
        int idx = i;
        connect(tpMipSpin[i], QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, idx](int) { updateTpPreview(idx); });
        btnRow->addWidget(tpMipSpin[i]);

        tpSaveBtn[i] = new QPushButton("Save");
        tpSaveBtn[i]->setEnabled(false);
        connect(tpSaveBtn[i], &QPushButton::clicked, this, [this, idx]() { onTpSave(idx); });
        btnRow->addWidget(tpSaveBtn[i]);

        pLayout->addLayout(btnRow);
        panelsLayout->addWidget(panel);
    }

    panelsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(panelsWidget, 1);

    return tab;
}

QWidget* MainWindow::buildReliefMappingTab() {
    QWidget* tab = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout(tab);

    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* reliefGroup = new QGroupBox("Relief Mapping");
    reliefGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* reliefGroupLayout = new QVBoxLayout(reliefGroup);
    reliefGroupLayout->setContentsMargins(4, 4, 4, 4);

    reliefStack = new QStackedWidget();
    reliefStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    reliefPlaceholder = new QLabel("Run Textures Preparation first to view relief mapping.");
    reliefPlaceholder->setAlignment(Qt::AlignCenter);
    reliefPlaceholder->setStyleSheet("color: #888; font-size: 14px;");
    reliefStack->addWidget(reliefPlaceholder);

    reliefWidget = new ReliefGLWidget();
    reliefWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    reliefStack->addWidget(reliefWidget);
    reliefStack->setCurrentWidget(reliefPlaceholder);

    reliefGroupLayout->addWidget(reliefStack);
    viewportsLayout->addWidget(reliefGroup);

    QGroupBox* compareGroup = new QGroupBox("Simplified Mesh (no relief)");
    compareGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* compareGroupLayout = new QVBoxLayout(compareGroup);
    compareGroupLayout->setContentsMargins(4, 4, 4, 4);

    reliefCompareWidget = new GLWidget();
    reliefCompareWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    reliefCompareWidget->setTextured(true);
    compareGroupLayout->addWidget(reliefCompareWidget);
    viewportsLayout->addWidget(compareGroup);

    QGroupBox* originalGroup = new QGroupBox("Original Model");
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* originalGroupLayout = new QVBoxLayout(originalGroup);
    originalGroupLayout->setContentsMargins(4, 4, 4, 4);

    reliefOriginalWidget = new GLWidget();
    reliefOriginalWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    reliefOriginalWidget->setTextured(true);
    originalGroupLayout->addWidget(reliefOriginalWidget);
    viewportsLayout->addWidget(originalGroup);

    layout->addWidget(viewportsWidget, 1);

    // Cameras stay in sync across the three viewports so they can be compared directly.
    connect(reliefWidget, &ReliefGLWidget::cameraChanged,
            reliefCompareWidget, &GLWidget::syncCamera);
    connect(reliefWidget, &ReliefGLWidget::cameraChanged,
            reliefOriginalWidget, &GLWidget::syncCamera);
    connect(reliefCompareWidget, &GLWidget::cameraChanged,
            reliefWidget, &ReliefGLWidget::syncCamera);
    connect(reliefCompareWidget, &GLWidget::cameraChanged,
            reliefOriginalWidget, &GLWidget::syncCamera);
    connect(reliefOriginalWidget, &GLWidget::cameraChanged,
            reliefWidget, &ReliefGLWidget::syncCamera);
    connect(reliefOriginalWidget, &GLWidget::cameraChanged,
            reliefCompareWidget, &GLWidget::syncCamera);

    // ── Controls panel ───────────────────────────────────────────────────────
    QGroupBox* ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    ctrlGroup->setFixedWidth(280);
    QVBoxLayout* ctrlLayout = new QVBoxLayout(ctrlGroup);

    reliefEnabledCheck = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck->setChecked(true);
    connect(reliefEnabledCheck, &QCheckBox::toggled, reliefWidget, &ReliefGLWidget::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck);

    ctrlLayout->addWidget(new QLabel("Steps:"));
    reliefStepsSpin = new QSpinBox();
    reliefStepsSpin->setRange(1, 256);
    reliefStepsSpin->setValue(64);
    connect(reliefStepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget, &ReliefGLWidget::setSteps);
    ctrlLayout->addWidget(reliefStepsSpin);

    ctrlLayout->addWidget(new QLabel("Binary Search Steps:"));
    reliefBinaryStepsSpin = new QSpinBox();
    reliefBinaryStepsSpin->setRange(0, 16);
    reliefBinaryStepsSpin->setValue(5);
    connect(reliefBinaryStepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget, &ReliefGLWidget::setBinarySteps);
    ctrlLayout->addWidget(reliefBinaryStepsSpin);

    ctrlLayout->addWidget(new QLabel("Depth Scale:"));
    reliefDepthScaleSpin = new QDoubleSpinBox();
    reliefDepthScaleSpin->setRange(0.0, 2.0);
    reliefDepthScaleSpin->setSingleStep(0.01);
    reliefDepthScaleSpin->setDecimals(4);
    reliefDepthScaleSpin->setValue(0.05);
    connect(reliefDepthScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), reliefWidget, &ReliefGLWidget::setDepthScale);
    ctrlLayout->addWidget(reliefDepthScaleSpin);

    ctrlLayout->addSpacing(8);

    reliefUseAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    reliefUseAtlasCheck->setChecked(true);
    connect(reliefUseAtlasCheck, &QCheckBox::toggled, reliefWidget, &ReliefGLWidget::setUseAtlas);
    ctrlLayout->addWidget(reliefUseAtlasCheck);

    ctrlLayout->addSpacing(8);

    ctrlLayout->addWidget(new QLabel("Debug View:"));
    reliefDebugViewCombo = new QComboBox();
    reliefDebugViewCombo->addItem("Shaded", 0);
    reliefDebugViewCombo->addItem("Step Count", 1);
    reliefDebugViewCombo->addItem("Leap Count", 2);
    reliefDebugViewCombo->addItem("UV After Relief", 3);
    connect(reliefDebugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        reliefWidget->setDebugView(reliefDebugViewCombo->currentData().toInt());
    });
    ctrlLayout->addWidget(reliefDebugViewCombo);

    ctrlLayout->addSpacing(8);

    reliefWireframeCheck = new QCheckBox("Wireframe");
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefWidget, &ReliefGLWidget::setWireframe);
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefCompareWidget, &GLWidget::setWireframe);
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefOriginalWidget, &GLWidget::setWireframe);
    ctrlLayout->addWidget(reliefWireframeCheck);

    reliefCullFaceCheck = new QCheckBox("Backface Culling");
    reliefCullFaceCheck->setChecked(true);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefWidget, &ReliefGLWidget::setCullFace);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefCompareWidget, &GLWidget::setCullFace);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefOriginalWidget, &GLWidget::setCullFace);
    ctrlLayout->addWidget(reliefCullFaceCheck);

    reliefResetCamBtn = new QPushButton("Reset Camera");
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefWidget, &ReliefGLWidget::resetCamera);
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefCompareWidget, &GLWidget::resetCamera);
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefOriginalWidget, &GLWidget::resetCamera);
    ctrlLayout->addWidget(reliefResetCamBtn);

    ctrlLayout->addStretch();
    layout->addWidget(ctrlGroup);

    return tab;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUI() {
    QTabWidget* tabs = new QTabWidget(this);
    setCentralWidget(tabs);
    tabsWidget = tabs;

    tabs->addTab(buildSimplifierTab(), "Simplifier");
    tabs->addTab(buildHeightmapTab(), "Heightmap Baking");
    tabs->addTab(buildTexturePrepTab(), "Textures Preparation");
    reliefTabIndex = tabs->addTab(buildReliefMappingTab(), "Relief Mapping");

    connect(tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);

    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);
}

// ─── Menu ─────────────────────────────────────────────────────────────────────

void MainWindow::createMenuBar() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* loadAction = fileMenu->addAction("&Load Model...");
    connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadModel);

    QAction* saveAction = fileMenu->addAction("&Save Simplified...");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveSimplified);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About QEM Simplifier",
            "QEM Mesh Simplifier\n\n"
            "Quadric Error Metrics simplification with Qt GUI\n"
            "Mouse: Drag to rotate, Scroll to zoom\n"
            "Formats: OBJ, GLTF\n\n"
            "Heightmap tab: bakes displacement between simplified and original mesh\n"
            "via shared UV correspondence.");
    });
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void MainWindow::onLoadModel() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open Mesh File", "",
        "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf *.glb);;All Files (*)");

    if (fileName.isEmpty()) return;

    originalMesh   = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive))
        success = originalMesh->loadOBJ(fileName.toStdString());
    else
        success = originalMesh->loadGLTF(fileName.toStdString());

    if (!success) {
        QMessageBox::critical(this, "Error", "Failed to load mesh file!");
        return;
    }

    // Start "simplified" as a copy of the original so Textures Preparation / Relief
    // Mapping work even before the user runs Simplify (e.g. baking with an already
    // low-poly model and pre-existing textures).
    *simplifiedMesh = *originalMesh;

    currentFilePath  = fileName;
    originalFaceCount = originalMesh->faceCount();
    targetFaceCount   = std::max(4, originalFaceCount / 4);

    targetFacesSpinBox->blockSignals(true);
    targetFacesSpinBox->setMaximum(originalFaceCount);
    targetFacesSpinBox->setValue(targetFaceCount);
    simplificationSlider->setValue(75);
    targetFacesSpinBox->blockSignals(false);

    glWidgetOriginal->setMesh(originalMesh.get());
    glWidgetSimplified->setMesh(originalMesh.get());
    glWidgetOverlay->setMeshes(originalMesh.get(), originalMesh.get());

    bool hasTexture = !originalMesh->textureData.empty();
    texturedCheck->setEnabled(hasTexture);
    if (!hasTexture) texturedCheck->setChecked(false);

    bool hasUVs = false;
    for (const auto& v : originalMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    uvViewCheck->setEnabled(hasUVs);
    if (!hasUVs) uvViewCheck->setChecked(false);

    hmPreview->setText("(not baked)");
    hmPreview->setPixmap(QPixmap());
    hmInfoLabel->setText("Range: —");
    hmSaveBtn->setEnabled(false);
    hmResult = HeightmapResult{};

    // Reset inflate/deflate controls
    baseSimplifiedPositions.clear();
    simplifiedVertexNormals.clear();
    simplifiedVertexGroup.clear();
    simplifiedVertexGroupCount = 0;
    simplifiedCoverOffsets.clear();
    inflateSlider->blockSignals(true);
    inflateSlider->setValue(0);
    inflateSlider->blockSignals(false);
    inflateSlider->setEnabled(false);
    inflateSpin->blockSignals(true);
    inflateSpin->setValue(0.0);
    inflateSpin->blockSignals(false);
    inflateSpin->setEnabled(false);
    smoothCoverBtn->setEnabled(false);

    // Reset Textures Preparation / Relief Mapping state — the previous bake was for
    // the old mesh's UV layout and no longer applies. simplifiedMesh already holds a
    // copy of the freshly loaded mesh (see above), so the relief widget can pick it
    // up as soon as its tab is shown.
    tpResult = TexturePrepResult{};
    reliefMeshPending = true;
    reliefTexturesPending = false;
    updateTpThumbnails();
    updateTpGenerateEnabled();
    for (int i = 0; i < 4; i++) {
        tpPreview[i]->setText("(not generated)");
        tpPreview[i]->setPixmap(QPixmap());
        tpInfoLabel[i]->setText("—");
        tpSaveBtn[i]->setEnabled(false);
        tpMipSpin[i]->setEnabled(false);
        tpMipSpin[i]->setRange(0, 0);
    }
    if (reliefStack) reliefStack->setCurrentWidget(reliefPlaceholder);

    updateStatusBar();
}

void MainWindow::onSaveSimplified() {
    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning",
            "No simplified mesh to save!\nRun simplification first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Simplified Mesh", "",
        "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

    if (fileName.isEmpty()) return;

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive))
        success = simplifiedMesh->saveOBJ(fileName.toStdString());
    else
        success = simplifiedMesh->saveGLTF(fileName.toStdString());

    if (success) {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Success", "Mesh saved successfully!");
    } else {
        QMessageBox::critical(this, "Error", "Failed to save mesh!");
    }
}

void MainWindow::onSimplify() {
    if (!originalMesh || originalMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No mesh loaded!");
        return;
    }

    int targetFaces = targetFacesSpinBox->value();
    *simplifiedMesh = *originalMesh;
    simplifiedMesh->boundaryMode = (BoundaryMode)boundaryModeCombo->currentData().toInt();
    simplifiedMesh->envelopeConstraint = envelopeConstraintCheck->isChecked();
    simplifiedMesh->useOptimalCandidate = useOptimalCandidateCheck->isChecked();

    statusLabel->setText("Simplifying...");
    statusBar()->repaint();

    simplifiedMesh->simplify(targetFaces);

    // Capture base positions and compute vertex normals for inflate/deflate
    baseSimplifiedPositions.resize(simplifiedMesh->vertices.size());
    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        baseSimplifiedPositions[i] = simplifiedMesh->vertices[i].pos;

    simplifiedVertexNormals.assign(simplifiedMesh->vertices.size(), Eigen::Vector3d::Zero());
    for (const auto& f : simplifiedMesh->faces) {
        if (f.removed) continue;
        const auto& p0 = baseSimplifiedPositions[f.v[0]];
        const auto& p1 = baseSimplifiedPositions[f.v[1]];
        const auto& p2 = baseSimplifiedPositions[f.v[2]];
        Eigen::Vector3d n = (p1-p0).cross(p2-p0);
        simplifiedVertexNormals[f.v[0]] += n;
        simplifiedVertexNormals[f.v[1]] += n;
        simplifiedVertexNormals[f.v[2]] += n;
    }

    // Vértices duplicados na mesma posição 3D (ex.: costuras de UV, separadas
    // em vertices distintos no loadOBJ) devem inflar juntos. Caso contrário,
    // cada cópia usa só suas próprias faces incidentes, as normais divergem,
    // e a costura abre um buraco ao inflar mesmo com seam vertices travados.
    // O group id também é reaproveitado pelo Smooth Cover para manter o
    // campo de offset sincronizado entre as cópias.
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto& p : baseSimplifiedPositions) { bmin = bmin.cwiseMin(p); bmax = bmax.cwiseMax(p); }
        double cell = std::max((bmax - bmin).norm() * 1e-7, 1e-9);

        auto quantize = [cell](const Eigen::Vector3d& p) {
            return std::make_tuple(
                (long long)std::llround(p.x() / cell),
                (long long)std::llround(p.y() / cell),
                (long long)std::llround(p.z() / cell));
        };

        std::map<std::tuple<long long,long long,long long>, int> groupId;
        simplifiedVertexGroup.assign(simplifiedMesh->vertices.size(), -1);
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++) {
            if (simplifiedMesh->vertices[i].removed) continue;
            auto key = quantize(baseSimplifiedPositions[i]);
            auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
            simplifiedVertexGroup[i] = it->second;
        }
        simplifiedVertexGroupCount = (int)groupId.size();

        std::vector<Eigen::Vector3d> groupNormal(simplifiedVertexGroupCount, Eigen::Vector3d::Zero());
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++) {
            if (simplifiedMesh->vertices[i].removed) continue;
            groupNormal[simplifiedVertexGroup[i]] += simplifiedVertexNormals[i];
        }
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++) {
            if (simplifiedMesh->vertices[i].removed) continue;
            simplifiedVertexNormals[i] = groupNormal[simplifiedVertexGroup[i]];
        }
    }

    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++) {
        double len = simplifiedVertexNormals[i].norm();
        if (len > 1e-10) simplifiedVertexNormals[i] /= len;
    }

    simplifiedCoverOffsets.assign(simplifiedMesh->vertices.size(), 0.0);

    // Set inflate range based on original mesh bounding box diagonal
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto& v : originalMesh->vertices) {
            if (!v.removed) { bmin = bmin.cwiseMin(v.pos); bmax = bmax.cwiseMax(v.pos); }
        }
        inflateScale = std::max((bmax - bmin).norm() * 0.5, 1e-6);
    }

    inflateSpin->blockSignals(true);
    inflateSpin->setMinimum(-inflateScale);
    inflateSpin->setMaximum( inflateScale);
    inflateSpin->setSingleStep(inflateScale / 1000.0);
    inflateSpin->setValue(0.0);
    inflateSpin->blockSignals(false);
    inflateSlider->blockSignals(true);
    inflateSlider->setValue(0);
    inflateSlider->blockSignals(false);
    inflateSlider->setEnabled(true);
    inflateSpin->setEnabled(true);
    smoothCoverBtn->setEnabled(true);

    glWidgetSimplified->setMesh(simplifiedMesh.get());
    glWidgetOverlay->setMeshes(originalMesh.get(), simplifiedMesh.get());
    updateStatusBar();

    updateTpThumbnails();
    updateTpGenerateEnabled();

    reliefMeshPending = true;
    trySyncReliefWidget();
}

void MainWindow::onTargetFacesChanged(int value) {
    targetFaceCount = value;
}

void MainWindow::onResetCameras() {
    glWidgetOriginal->resetCamera();
    glWidgetSimplified->resetCamera();
    glWidgetOverlay->resetCamera();
}

// ─── Heightmap baking ────────────────────────────────────────────────────────

void MainWindow::setBakeButtonsEnabled(bool enabled) {
    hmBakeBtn->setEnabled(enabled);
}

void MainWindow::launchBake() {
    if (hmThread && hmThread->isRunning()) return;

    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning",
            "Simplify the mesh first before baking a heightmap.");
        return;
    }
    if (!originalMesh || originalMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "No original mesh loaded.");
        return;
    }

    bool hasUVs = false;
    for (const auto& v : simplifiedMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    if (!hasUVs) {
        QMessageBox::warning(this, "Warning",
            "The simplified mesh has no UV coordinates.\n"
            "Load a mesh with UVs (e.g. a GLTF with texture coordinates).");
        return;
    }

    int res = hmResCombo->currentData().toInt();

    setBakeButtonsEnabled(false);
    hmProgressBar->setValue(0);
    hmProgressLabel->setText("Starting…");

    hmPreview->setText("(baking…)");
    hmPreview->setPixmap(QPixmap());
    hmInfoLabel->setText("Range: —");
    hmSaveBtn->setEnabled(false);

    hmWorker = new HeightmapWorker();
    hmWorker->simplified = simplifiedMesh.get();
    hmWorker->original   = originalMesh.get();
    hmWorker->width      = res;
    hmWorker->height     = res;

    hmThread = new QThread(this);
    hmWorker->moveToThread(hmThread);

    connect(hmThread, &QThread::started,          hmWorker, &HeightmapWorker::run);
    connect(hmWorker, &HeightmapWorker::progress, this,     &MainWindow::onBakeProgress);
    connect(hmWorker, &HeightmapWorker::finished, this,     &MainWindow::onBakeDone);
    connect(hmWorker, &HeightmapWorker::finished, hmThread, &QThread::quit);
    // hmWorker is deleted explicitly in onBakeDone(), after it has read the results —
    // NOT via a finished->deleteLater connection on hmWorker itself. That connection
    // would be direct (hmWorker and the finished() emitter share hmThread's affinity),
    // so it'd race the queued, cross-thread delivery of finished() to onBakeDone():
    // hmThread could process the deferred delete and free hmWorker's result vectors
    // before/while onBakeDone() reads them from the main thread, corrupting the heap
    // (manifesting later as a stray std::bad_alloc).
    // Only delete the QThread once it has actually wound down (QThread::finished,
    // fired when its event loop really exits) — deleting it right after quit() is
    // merely requested races the real thread teardown and aborts the app
    // (QThread: Destroyed while thread is still running). This races much more
    // reliably on fast bakes (e.g. 128×128) where there's almost no delay.
    connect(hmThread, &QThread::finished,         hmThread, &QObject::deleteLater);

    statusLabel->setText(QString("Baking %1×%1…").arg(res));
    hmThread->start();
}

void MainWindow::onBake() {
    launchBake();
}

void MainWindow::onBakeProgress(int overall, const QString& text) {
    hmProgressBar->setValue(overall);
    hmProgressLabel->setText(text);
}

void MainWindow::onBakeDone() {
    hmResult = hmWorker->results[0];
    displayHeightmap(hmResult);

    // Read of hmWorker's results is done — safe to delete it now. hmThread
    // self-deletes once truly idle (see QThread::finished connection in launchBake).
    hmWorker->deleteLater();
    hmWorker = nullptr;
    hmThread = nullptr;

    setBakeButtonsEnabled(true);
    hmProgressLabel->setText("Done");
    statusLabel->setText("Bake complete");

    updateTpThumbnails();
    updateTpGenerateEnabled();
}

void MainWindow::displayHeightmap(const HeightmapResult& r) {
    if (!r.valid || r.image.empty()) {
        hmPreview->setText("(bake failed)");
        hmInfoLabel->setText("Range: —");
        hmSaveBtn->setEnabled(false);
        return;
    }

    QImage img(r.image.data(), r.width, r.height, r.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    QPixmap px = QPixmap::fromImage(img);
    QSize labelSize = hmPreview->size();
    if (labelSize.isEmpty()) labelSize = QSize(300, 300);
    hmPreview->setPixmap(
        px.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    hmInfoLabel->setText(
        QString("Range: [%1, %2]")
            .arg((double)r.minH, 0, 'f', 4)
            .arg((double)r.maxH, 0, 'f', 4));

    hmSaveBtn->setEnabled(true);
}

void MainWindow::onSaveHeightmap() {
    if (!hmResult.valid || hmResult.image.empty()) return;

    QString fileName = QFileDialog::getSaveFileName(this,
        "Save Heightmap", "",
        "PNG Image (*.png);;All Files (*)");

    if (fileName.isEmpty()) return;

    QImage img(hmResult.image.data(), hmResult.width, hmResult.height,
               hmResult.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    if (img.save(fileName)) {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Saved", "Heightmap saved as PNG.");
    } else {
        QMessageBox::critical(this, "Error", "Failed to save image.");
    }
}

// ─── Textures Preparation ────────────────────────────────────────────────────

void MainWindow::updateTpThumbnails() {
    auto setThumb = [this](int idx, const QImage& img, const char* emptyText) {
        if (img.isNull()) {
            tpThumb[idx]->setPixmap(QPixmap());
            tpThumb[idx]->setText(emptyText);
        } else {
            tpThumb[idx]->setPixmap(QPixmap::fromImage(img)
                .scaled(tpThumb[idx]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };

    QImage colorImg, normalImg;
    if (simplifiedMesh) {
        colorImg  = rgbaTextureToQImage(simplifiedMesh->textureData,
                                         simplifiedMesh->textureWidth, simplifiedMesh->textureHeight);
        normalImg = rgbaTextureToQImage(simplifiedMesh->normalTextureData,
                                         simplifiedMesh->normalTextureWidth, simplifiedMesh->normalTextureHeight);
    }
    setThumb(0, colorImg, "(none)");
    setThumb(2, normalImg, "(none)");

    QImage depthImg;
    if (hmResult.valid && !hmResult.image.empty()) {
        depthImg = QImage(hmResult.image.data(), hmResult.width, hmResult.height,
                           hmResult.width, QImage::Format_Grayscale8).mirrored(false, true);
    }
    setThumb(1, depthImg, "(not baked)");
}

void MainWindow::updateTpGenerateEnabled() {
    bool hasMesh   = simplifiedMesh && simplifiedMesh->faceCount() > 0;
    bool hasColor  = simplifiedMesh && !simplifiedMesh->textureData.empty();
    bool hasNormal = simplifiedMesh && !simplifiedMesh->normalTextureData.empty();
    bool hasDepth  = hmResult.valid && !hmResult.image.empty();
    tpGenerateBtn->setEnabled(hasMesh && hasColor && hasNormal && hasDepth);
}

void MainWindow::onTpGenerate() {
    if (tpThread && tpThread->isRunning()) return;
    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0) {
        QMessageBox::warning(this, "Warning", "Simplify the mesh first before preparing textures.");
        return;
    }
    if (simplifiedMesh->textureData.empty() || simplifiedMesh->normalTextureData.empty()) {
        QMessageBox::warning(this, "Warning",
            "The model has no embedded color and/or normal texture.\n"
            "Load a GLTF with a baseColorTexture and normalTexture.");
        return;
    }
    if (!hmResult.valid || hmResult.image.empty()) {
        QMessageBox::warning(this, "Warning",
            "Bake a heightmap first (Heightmap Baking tab) — it is used as the depth input.");
        return;
    }

    bool hasUVs = false;
    for (const auto& v : simplifiedMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12) { hasUVs = true; break; }
    if (!hasUVs) {
        QMessageBox::warning(this, "Warning",
            "The simplified mesh has no UV coordinates.\n"
            "Load a mesh with UVs (e.g. a GLTF with texture coordinates).");
        return;
    }

    int res = tpResCombo->currentData().toInt();
    int seamBand = tpSeamBandSpin->value();

    tpGenerateBtn->setEnabled(false);
    tpProgressBar->setValue(0);
    tpProgressLabel->setText("Starting…");
    for (int i = 0; i < 4; i++) {
        tpPreview[i]->setText("(generating…)");
        tpPreview[i]->setPixmap(QPixmap());
        tpInfoLabel[i]->setText("—");
        tpSaveBtn[i]->setEnabled(false);
        tpMipSpin[i]->setEnabled(false);
    }

    tpWorker = new TexturePrepWorker();
    tpWorker->mesh      = simplifiedMesh.get();
    tpWorker->colorImg  = rgbaTextureToQImage(simplifiedMesh->textureData,
                                               simplifiedMesh->textureWidth, simplifiedMesh->textureHeight);
    tpWorker->normalImg = rgbaTextureToQImage(simplifiedMesh->normalTextureData,
                                               simplifiedMesh->normalTextureWidth, simplifiedMesh->normalTextureHeight);
    tpWorker->depthImg  = QImage(hmResult.image.data(), hmResult.width, hmResult.height,
                                  hmResult.width, QImage::Format_Grayscale8).mirrored(false, true);
    tpWorker->workRes        = res;
    tpWorker->seamBandTexels = seamBand;

    tpThread = new QThread(this);
    tpWorker->moveToThread(tpThread);

    connect(tpThread, &QThread::started,           tpWorker, &TexturePrepWorker::run);
    connect(tpWorker, &TexturePrepWorker::progress, this,     &MainWindow::onTpProgress);
    connect(tpWorker, &TexturePrepWorker::finished,  this,     &MainWindow::onTpDone);
    connect(tpWorker, &TexturePrepWorker::finished,  tpThread, &QThread::quit);
    // tpWorker is deleted explicitly in onTpDone(), after it has read tpWorker->result —
    // see the comment on the equivalent hmWorker connection in launchBake() for why a
    // finished->deleteLater connection on tpWorker itself would race the cross-thread
    // delivery of finished() to onTpDone().
    // See launchBake(): only delete the QThread once it has actually finished, not
    // right after quit() is merely requested.
    connect(tpThread, &QThread::finished,            tpThread, &QObject::deleteLater);

    statusLabel->setText(QString("Baking textures %1×%1…").arg(res));
    tpThread->start();
}

void MainWindow::onTpProgress(int overall, const QString& text) {
    tpProgressBar->setValue(overall);
    tpProgressLabel->setText(text);
}

void MainWindow::onTpDone() {
    tpResult = tpWorker->result;

    // Read of tpWorker->result is done — safe to delete it now. tpThread
    // self-deletes once truly idle (see QThread::finished connection in onTpGenerate).
    tpWorker->deleteLater();
    tpWorker = nullptr;
    tpThread = nullptr;

    if (!tpResult.valid) {
        QMessageBox::critical(this, "Error", "Failed to generate textures (could not load one of the input images).");
        for (int i = 0; i < 4; i++) tpPreview[i]->setText("(failed)");
    } else {
        int levels[4] = {
            tpResult.colorMap.levelCount(), tpResult.reliefMap.levelCount(),
            tpResult.normalMap.levelCount(), 1
        };
        for (int i = 0; i < 4; i++) {
            tpMipSpin[i]->setRange(0, std::max(0, levels[i] - 1));
            tpMipSpin[i]->setValue(0);
            tpMipSpin[i]->setEnabled(levels[i] > 1);
            tpSaveBtn[i]->setEnabled(true);
            updateTpPreview(i);
        }

        reliefTexturesPending = true;
        trySyncReliefWidget();
    }

    tpGenerateBtn->setEnabled(true);
    tpProgressLabel->setText("Done");
    statusLabel->setText("Texture preparation complete");
}

QImage MainWindow::mipLevelToQImage(const std::vector<float>& data, int w, int h, int channels, bool remapSigned,
                                     const bool* showChannels) const {
    static const bool kAllShown[4] = {true, true, true, true};
    if (!showChannels) showChannels = kAllShown;

    QImage::Format fmt = (channels == 3) ? QImage::Format_RGB888 : QImage::Format_RGBA8888;
    QImage img(w, h, fmt);
    auto remap = [&](float v) { return remapSigned ? v * 0.5f + 0.5f : v; };
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = ((size_t)y * w + x) * channels;
            float r = showChannels[0] ? std::clamp(remap(data[i + 0]), 0.0f, 1.0f) : 0.0f;
            float g = showChannels[1] ? std::clamp(remap(data[i + 1]), 0.0f, 1.0f) : 0.0f;
            float b = showChannels[2] ? std::clamp(remap(data[i + 2]), 0.0f, 1.0f) : 0.0f;
            float a = (channels == 4) ? (showChannels[3] ? std::clamp(remap(data[i + 3]), 0.0f, 1.0f) : 1.0f) : 1.0f;
            img.setPixelColor(x, y, QColor::fromRgbF(r, g, b, a));
        }
    }
    return img;
}

QImage MainWindow::offsetMapMaskImage() const {
    const OffsetMapResult& o = tpResult.offsetMap;
    QImage img(std::max(1, o.width), std::max(1, o.height), QImage::Format_RGB888);
    for (int y = 0; y < o.height; y++) {
        for (int x = 0; x < o.width; x++) {
            float valid = o.data[((size_t)y * o.width + x) * 4 + 3];
            img.setPixelColor(x, y, valid > 0.0f ? QColor(255, 60, 60) : QColor(20, 20, 20));
        }
    }
    return img;
}

void MainWindow::updateTpPreview(int idx) {
    if (!tpResult.valid) return;

    QImage img;
    QString info;

    if (idx == 3) {
        img = offsetMapMaskImage();
        info = QString("%1×%2 (seam mask)").arg(tpResult.offsetMap.width).arg(tpResult.offsetMap.height);
    } else {
        const MipPyramid& p = (idx == 0) ? tpResult.colorMap : (idx == 1) ? tpResult.reliefMap : tpResult.normalMap;
        int level = std::min(tpMipSpin[idx]->value(), p.levelCount() - 1);
        if (level < 0) return;
        int w = std::max(1, p.width >> level), h = std::max(1, p.height >> level);
        bool show[4] = {
            tpChannelCheck[idx][0]->isChecked(), tpChannelCheck[idx][1]->isChecked(),
            tpChannelCheck[idx][2]->isChecked(), tpChannelCheck[idx][3]->isChecked()
        };
        img = mipLevelToQImage(p.mips[level], w, h, p.channels, /*remapSigned=*/idx == 2, show);
        info = QString("%1×%2, %3 mips").arg(w).arg(h).arg(p.levelCount());
    }

    img = img.mirrored(false, true);
    QPixmap px = QPixmap::fromImage(img);
    QSize labelSize = tpPreview[idx]->size();
    if (labelSize.isEmpty()) labelSize = QSize(220, 220);
    tpPreview[idx]->setPixmap(px.scaled(labelSize, Qt::KeepAspectRatio, Qt::FastTransformation));
    tpInfoLabel[idx]->setText(info);
}

void MainWindow::onTpSave(int idx) {
    if (!tpResult.valid) return;
    static const char* names[4] = {"Color Map", "Relief Map", "Normal Map", "Offset Map"};

    QString fileName = QFileDialog::getSaveFileName(this, QString("Save %1").arg(names[idx]), "",
        "PNG Image (*.png);;All Files (*)");
    if (fileName.isEmpty()) return;

    QImage img;
    if (idx == 3) {
        img = offsetMapMaskImage();
    } else {
        const MipPyramid& p = (idx == 0) ? tpResult.colorMap : (idx == 1) ? tpResult.reliefMap : tpResult.normalMap;
        img = mipLevelToQImage(p.mips[0], p.width, p.height, p.channels, /*remapSigned=*/idx == 2);
    }
    img = img.mirrored(false, true);

    if (img.save(fileName)) {
        statusLabel->setText("Saved: " + fileName);
    } else {
        QMessageBox::critical(this, "Error", "Failed to save image.");
    }
}

// ─── Relief Mapping ──────────────────────────────────────────────────────────

void MainWindow::showReliefViewport() {
    if (reliefStack && reliefWidget) reliefStack->setCurrentWidget(reliefWidget);
}

void MainWindow::trySyncReliefWidget() {
    if (!reliefWidget || !tabsWidget || tabsWidget->currentIndex() != reliefTabIndex) return;

    if (reliefMeshPending && simplifiedMesh && simplifiedMesh->faceCount() > 0) {
        reliefWidget->setMesh(simplifiedMesh.get());
        reliefCompareWidget->setMesh(simplifiedMesh.get());
        if (originalMesh) reliefOriginalWidget->setMesh(originalMesh.get());
        reliefMeshPending = false;
    }
    if (reliefTexturesPending && tpResult.valid) {
        reliefWidget->setTextures(tpResult);
        reliefCompareWidget->setColorTexture(tpResult.colorMap);
        reliefTexturesPending = false;
    }
    if (tpResult.valid) showReliefViewport();
}

void MainWindow::onTabChanged(int index) {
    if (index != reliefTabIndex) return;
    trySyncReliefWidget();
}

// ─── Status / helpers ────────────────────────────────────────────────────────

void MainWindow::updateStatusBar() {
    QString originalStats = QString("Original: %1 faces, %2 vertices")
        .arg(originalMesh->faceCount())
        .arg(originalMesh->vertexCount());

    QString simplifiedStats = QString("Simplified: %1 faces, %2 vertices")
        .arg(simplifiedMesh->faceCount())
        .arg(simplifiedMesh->vertexCount());

    originalStatsLabel->setText(originalStats);
    simplifiedStatsLabel->setText(simplifiedStats);

    if (simplifiedMesh->faceCount() > 0) {
        double reduction = 100.0 *
            (1.0 - (double)simplifiedMesh->faceCount() / originalMesh->faceCount());
        statusLabel->setText(QString("Reduction: %1%").arg(reduction, 0, 'f', 1));
    } else {
        statusLabel->setText("Ready");
    }
}

// ─── Inflate / Deflate ───────────────────────────────────────────────────────

void MainWindow::applyInflate(double offset) {
    if (baseSimplifiedPositions.empty()) return;
    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++) {
        if (!simplifiedMesh->vertices[i].removed)
            simplifiedMesh->vertices[i].pos =
                baseSimplifiedPositions[i] + (simplifiedCoverOffsets[i] + offset) * simplifiedVertexNormals[i];
    }
    glWidgetSimplified->updateMeshData();
    glWidgetOverlay->updateSimplifiedMesh();
}

// Computa um campo de offset por vértice (simplifiedCoverOffsets) para que a
// cage cubra toda a malha original com uma folga suave, em vez de um único
// offset global de pior caso (que infla demais regiões planas só para cobrir
// uma região côncava difícil em outro lugar).
//
// 1) Para cada vértice original, acha o vértice da cage mais próximo e mede
//    a distância assinada (ao longo da normal desse vértice) necessária para
//    cobri-lo; agrega por grupo (vértices duplicados de costura comparti-
//    lham normal, então devem compartilhar offset).
// 2) Suaviza esse campo entre grupos vizinhos (média com os vizinhos da
//    malha) para tirar picos/facetamento.
// 3) A suavização pode "comer" picos necessários para cobertura total: mede
//    quanto ainda falta (mesma lógica do antigo Min Cover, mas sobre a cage
//    já deslocada) e soma essa folga uniformemente a todos os offsets.
void MainWindow::computeSmoothCoverOffsets() {
    simplifiedCoverOffsets.assign(simplifiedMesh->vertices.size(), 0.0);
    if (!originalMesh || !simplifiedMesh || baseSimplifiedPositions.empty()
        || simplifiedVertexGroupCount == 0) return;

    std::vector<int> activeVerts;
    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        if (!simplifiedMesh->vertices[i].removed) activeVerts.push_back((int)i);
    if (activeVerts.empty()) return;

    std::vector<double> groupOffset(simplifiedVertexGroupCount, 0.0);
    for (const auto& v : originalMesh->vertices) {
        if (v.removed) continue;
        double bestD2 = std::numeric_limits<double>::max();
        int nearest = activeVerts[0];
        for (int vi : activeVerts) {
            double d2 = (v.pos - baseSimplifiedPositions[vi]).squaredNorm();
            if (d2 < bestD2) { bestD2 = d2; nearest = vi; }
        }
        double signedDist = (v.pos - baseSimplifiedPositions[nearest]).dot(simplifiedVertexNormals[nearest]);
        int g = simplifiedVertexGroup[nearest];
        groupOffset[g] = std::max(groupOffset[g], signedDist);
    }
    for (auto& o : groupOffset) o = std::max(o, 0.0);

    std::vector<std::set<int>> groupAdj(simplifiedVertexGroupCount);
    for (const auto& f : simplifiedMesh->faces) {
        if (f.removed) continue;
        for (int i = 0; i < 3; i++) {
            int ga = simplifiedVertexGroup[f.v[i]];
            int gb = simplifiedVertexGroup[f.v[(i + 1) % 3]];
            if (ga != gb) { groupAdj[ga].insert(gb); groupAdj[gb].insert(ga); }
        }
    }

    constexpr int    kSmoothIters = 12;
    constexpr double kSmoothAlpha = 0.5;
    std::vector<double> smoothed = groupOffset;
    for (int iter = 0; iter < kSmoothIters; iter++) {
        std::vector<double> next = smoothed;
        for (int g = 0; g < simplifiedVertexGroupCount; g++) {
            if (groupAdj[g].empty()) continue;
            double avg = 0.0;
            for (int n : groupAdj[g]) avg += smoothed[n];
            avg /= (double)groupAdj[g].size();
            next[g] = smoothed[g] + kSmoothAlpha * (avg - smoothed[g]);
        }
        smoothed.swap(next);
    }

    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        if (!simplifiedMesh->vertices[i].removed)
            simplifiedCoverOffsets[i] = smoothed[simplifiedVertexGroup[i]];

    struct SFace { Eigen::Vector3d v0, normal, centroid; };
    std::vector<SFace> sFaces;
    for (const auto& f : simplifiedMesh->faces) {
        if (f.removed) continue;
        Eigen::Vector3d p0 = baseSimplifiedPositions[f.v[0]] + simplifiedCoverOffsets[f.v[0]] * simplifiedVertexNormals[f.v[0]];
        Eigen::Vector3d p1 = baseSimplifiedPositions[f.v[1]] + simplifiedCoverOffsets[f.v[1]] * simplifiedVertexNormals[f.v[1]];
        Eigen::Vector3d p2 = baseSimplifiedPositions[f.v[2]] + simplifiedCoverOffsets[f.v[2]] * simplifiedVertexNormals[f.v[2]];
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        if (n.norm() < 1e-10) continue;
        n.normalize();
        sFaces.push_back({p0, n, (p0 + p1 + p2) / 3.0});
    }

    double residual = 0.0;
    for (const auto& v : originalMesh->vertices) {
        if (v.removed || sFaces.empty()) continue;
        double bestD2 = std::numeric_limits<double>::max();
        int nearest = 0;
        for (int fi = 0; fi < (int)sFaces.size(); fi++) {
            double d2 = (v.pos - sFaces[fi].centroid).squaredNorm();
            if (d2 < bestD2) { bestD2 = d2; nearest = fi; }
        }
        double signedDist = (v.pos - sFaces[nearest].v0).dot(sFaces[nearest].normal);
        residual = std::max(residual, signedDist);
    }
    if (residual > 0.0)
        for (auto& o : simplifiedCoverOffsets) o += residual;
}

void MainWindow::onSmoothCover() {
    computeSmoothCoverOffsets();
    applyInflate(inflateSpin->value());
}

void MainWindow::computeAutoTarget() {
    if (originalFaceCount > 0) {
        int target = std::max(4,
            (int)(originalFaceCount * simplificationSlider->value() / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(target);
        targetFacesSpinBox->blockSignals(false);
    }
}
