#include "gui/mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QStatusBar>
#include <QSplitter>
#include <QToolBar>
#include <QDockWidget>
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

namespace
{
    QImage rgbaTextureToQImage(const std::vector<uint8_t> &data, int w, int h)
    {
        if (data.empty() || w <= 0 || h <= 0)
            return QImage();
        QImage img(data.data(), w, h, w * 4, QImage::Format_RGBA8888);
        return img.copy(); // detach from the mesh's buffer
    }
} // namespace

// ─── Constructor ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("QEM Mesh Simplifier");
    setGeometry(100, 100, 1600, 900);

    originalMesh = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    setupUI();
    createMenuBar();
    updateStatusBar();
}

// ─── Context builders: viewports ──────────────────────────────────────────────

QWidget *MainWindow::buildSimplifierViewport()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    QWidget *viewportsWidget = new QWidget();
    QHBoxLayout *viewportsLayout = new QHBoxLayout(viewportsWidget);

    QWidget *originalGroup = new QWidget();
    originalGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *leftLayout = new QVBoxLayout(originalGroup);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    glWidgetOriginal = new Orbital3DView(RenderMode::Solid, "Original Mesh");
    glWidgetOriginal->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    leftLayout->addWidget(glWidgetOriginal, 1);
    viewportsLayout->addWidget(originalGroup);

    QWidget *simplifiedGroup = new QWidget();
    simplifiedGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *rightLayout = new QVBoxLayout(simplifiedGroup);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    glWidgetSimplified = new Orbital3DView(RenderMode::Solid, "Simplified Mesh");
    glWidgetSimplified->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(glWidgetSimplified, 1);
    viewportsLayout->addWidget(simplifiedGroup);

    glWidgetOverlay = new Orbital3DView(RenderMode::Overlay, "Overlay");
    glWidgetOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(glWidgetOverlay);

    viewportsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewportsWidget);

    connect(glWidgetOriginal,  &Orbital3DView::cameraChanged, glWidgetSimplified, &Orbital3DView::syncCamera);
    connect(glWidgetOriginal,  &Orbital3DView::cameraChanged, glWidgetOverlay,    &Orbital3DView::syncCamera);
    connect(glWidgetSimplified,&Orbital3DView::cameraChanged, glWidgetOriginal,   &Orbital3DView::syncCamera);
    connect(glWidgetSimplified,&Orbital3DView::cameraChanged, glWidgetOverlay,    &Orbital3DView::syncCamera);
    connect(glWidgetOverlay,   &Orbital3DView::cameraChanged, glWidgetOriginal,   &Orbital3DView::syncCamera);
    connect(glWidgetOverlay,   &Orbital3DView::cameraChanged, glWidgetSimplified, &Orbital3DView::syncCamera);

    return w;
}

QWidget *MainWindow::buildHeightmapViewport()
{
    QGroupBox *panel = new QGroupBox("UV Correspondence");
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *pLayout = new QVBoxLayout(panel);

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

    return panel;
}

QWidget *MainWindow::buildTexturePrepViewport()
{
    static const char *previewTitles[4] = {
        "Color Map", "Relief Map  (R=min G=max(mip-bound) B=offset mask A=—)", "Normal Map", "Offset Map  (atlas leap mask)"};

    QWidget *panelsWidget = new QWidget();
    QHBoxLayout *panelsLayout = new QHBoxLayout(panelsWidget);
    panelsLayout->setSpacing(12);

    for (int i = 0; i < 4; i++)
    {
        QGroupBox *panel = new QGroupBox(previewTitles[i]);
        QVBoxLayout *pLayout = new QVBoxLayout(panel);
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

        if (i < 3)
        {
            static const char *chanLabel[4] = {"R", "G", "B", "A"};
            static const char *chanTooltip[3][4] = {
                {"Red", "Green", "Blue", "Alpha"},
                {"Min depth (mip bound)", "Max depth (mip bound)", "Offset/seam mask", "Reserved (always 0)"},
                {"X", "Y", "Z", ""},
            };
            QHBoxLayout *chanRow = new QHBoxLayout();
            chanRow->addWidget(new QLabel("Channels:"));
            for (int c = 0; c < 4; c++)
            {
                tpChannelCheck[i][c] = new QCheckBox(chanLabel[c]);
                tpChannelCheck[i][c]->setChecked(true);
                tpChannelCheck[i][c]->setToolTip(chanTooltip[i][c]);
                connect(tpChannelCheck[i][c], &QCheckBox::toggled, this, [this, idx = i](bool)
                        { updateTpPreview(idx); });
                chanRow->addWidget(tpChannelCheck[i][c]);
            }
            if (i == 2)
            {
                tpChannelCheck[i][3]->setChecked(false);
                tpChannelCheck[i][3]->setEnabled(false);
            }
            chanRow->addStretch();
            pLayout->addLayout(chanRow);
        }

        QHBoxLayout *btnRow = new QHBoxLayout();
        btnRow->addWidget(new QLabel("Mip:"));
        tpMipSpin[i] = new QSpinBox();
        tpMipSpin[i]->setRange(0, 0);
        tpMipSpin[i]->setEnabled(false);
        int idx = i;
        connect(tpMipSpin[i], QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, idx](int)
                { updateTpPreview(idx); });
        btnRow->addWidget(tpMipSpin[i]);

        tpSaveBtn[i] = new QPushButton("Save");
        tpSaveBtn[i]->setEnabled(false);
        connect(tpSaveBtn[i], &QPushButton::clicked, this, [this, idx]()
                { onTpSave(idx); });
        btnRow->addWidget(tpSaveBtn[i]);

        pLayout->addLayout(btnRow);
        panelsLayout->addWidget(panel);
    }

    panelsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return panelsWidget;
}

QWidget *MainWindow::buildReliefMappingViewport()
{
    QWidget *viewportsWidget = new QWidget();
    QHBoxLayout *viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    reliefWidget = new Orbital3DView(RenderMode::Relief, "Relief Mapping");
    reliefWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefWidget);

    reliefCompareWidget = new Orbital3DView(RenderMode::Textured, "Simplified Mesh (no relief)");
    reliefCompareWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefCompareWidget);

    reliefOriginalWidget = new Orbital3DView(RenderMode::Textured, "Original Model");
    reliefOriginalWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(reliefOriginalWidget);

    // Cameras stay in sync across the three viewports so they can be compared directly.
    connect(reliefWidget,        &Orbital3DView::cameraChanged, reliefCompareWidget,  &Orbital3DView::syncCamera);
    connect(reliefWidget,        &Orbital3DView::cameraChanged, reliefOriginalWidget, &Orbital3DView::syncCamera);
    connect(reliefCompareWidget, &Orbital3DView::cameraChanged, reliefWidget,         &Orbital3DView::syncCamera);
    connect(reliefCompareWidget, &Orbital3DView::cameraChanged, reliefOriginalWidget, &Orbital3DView::syncCamera);
    connect(reliefOriginalWidget,&Orbital3DView::cameraChanged, reliefWidget,         &Orbital3DView::syncCamera);
    connect(reliefOriginalWidget,&Orbital3DView::cameraChanged, reliefCompareWidget,  &Orbital3DView::syncCamera);

    return viewportsWidget;
}

// ─── Context builders: controls ───────────────────────────────────────────────

QWidget *MainWindow::buildSimplifierControls()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);
    layout->setContentsMargins(4, 4, 4, 4);

    // ── Simplification Controls ────────────────────────────────────────────
    QGroupBox *controlsGroup = new QGroupBox("Simplification");
    QVBoxLayout *controlsRows = new QVBoxLayout(controlsGroup);
    controlsRows->setSpacing(4);

    // Target faces row
    QHBoxLayout *facesRow = new QHBoxLayout();
    facesRow->addWidget(new QLabel("Target Faces:"));
    targetFacesSpinBox = new QSpinBox();
    targetFacesSpinBox->setMinimum(4);
    targetFacesSpinBox->setMaximum(1000000);
    targetFacesSpinBox->setValue(1000);
    facesRow->addWidget(targetFacesSpinBox, 1);
    controlsRows->addLayout(facesRow);

    simplificationSlider = new QSlider(Qt::Horizontal);
    simplificationSlider->setMinimum(1);
    simplificationSlider->setMaximum(100);
    simplificationSlider->setValue(50);
    controlsRows->addWidget(simplificationSlider);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *simplifyBtn = new QPushButton("Simplify");
    connect(simplifyBtn, &QPushButton::clicked, this, &MainWindow::onSimplify);
    btnRow->addWidget(simplifyBtn);
    QPushButton *resetCamBtn = new QPushButton("Reset Cameras");
    connect(resetCamBtn, &QPushButton::clicked, this, &MainWindow::onResetCameras);
    btnRow->addWidget(resetCamBtn);
    controlsRows->addLayout(btnRow);

    // View options
    wireframeCheck = new QCheckBox("Wireframe");
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setWireframe);
    connect(wireframeCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setWireframe);
    controlsRows->addWidget(wireframeCheck);

    texturedCheck = new QCheckBox("Textured");
    texturedCheck->setEnabled(false);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setTextured);
    connect(texturedCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setTextured);
    controlsRows->addWidget(texturedCheck);

    cullFaceCheck = new QCheckBox("Backface Cull");
    cullFaceCheck->setChecked(true);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setCullFace);
    connect(cullFaceCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setCullFace);
    controlsRows->addWidget(cullFaceCheck);

    uvViewCheck = new QCheckBox("UV View");
    uvViewCheck->setEnabled(false);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setUVMode);
    connect(uvViewCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setUVMode);
    controlsRows->addWidget(uvViewCheck);

    // Simplification options
    QHBoxLayout *boundaryRow = new QHBoxLayout();
    boundaryRow->addWidget(new QLabel("Boundary:"));
    boundaryModeCombo = new QComboBox();
    boundaryModeCombo->addItem("No constraint",       (int)BoundaryMode::None);
    boundaryModeCombo->addItem("Constraint",          (int)BoundaryMode::Constraint);
    boundaryModeCombo->addItem("Lock seam edges",     (int)BoundaryMode::LockSeamVertices);
    boundaryModeCombo->addItem("Sync seam twins",     (int)BoundaryMode::SyncSeamTwins);
    boundaryModeCombo->setCurrentIndex(1);
    boundaryRow->addWidget(boundaryModeCombo, 1);
    controlsRows->addLayout(boundaryRow);

    envelopeConstraintCheck = new QCheckBox("Envelope Constraint");
    envelopeConstraintCheck->setToolTip(
        "Garante que a malha simplificada fique sempre do lado de fora (ou sobre)\n"
        "a malha original. Pode travar colapsos em regioes muito concavas, entao\n"
        "a malha final pode nao atingir a contagem de faces alvo.");
    controlsRows->addWidget(envelopeConstraintCheck);

    useOptimalCandidateCheck = new QCheckBox("Use Optimal Candidate");
    useOptimalCandidateCheck->setToolTip(
        "Soma o otimo irrestrito da quadrica como mais um candidato de posicao\n"
        "de colapso, alem de v1, v2 e ponto medio.");
    controlsRows->addWidget(useOptimalCandidateCheck);

    showBoundaryEdgesCheck = new QCheckBox("Show Boundary Edges");
    connect(showBoundaryEdgesCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setShowBoundaryEdges);
    connect(showBoundaryEdgesCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setShowBoundaryEdges);
    controlsRows->addWidget(showBoundaryEdgesCheck);

    showInternalEdgesCheck = new QCheckBox("Show Internal Edges");
    connect(showInternalEdgesCheck, &QCheckBox::toggled, glWidgetOriginal,   &Orbital3DView::setShowInternalEdges);
    connect(showInternalEdgesCheck, &QCheckBox::toggled, glWidgetSimplified, &Orbital3DView::setShowInternalEdges);
    controlsRows->addWidget(showInternalEdgesCheck);

    layout->addWidget(controlsGroup);

    // ── Inflate / Deflate ──────────────────────────────────────────────────
    QGroupBox *inflateGroup = new QGroupBox("Inflate / Deflate");
    QVBoxLayout *inflateLayout = new QVBoxLayout(inflateGroup);
    inflateLayout->setSpacing(4);

    QHBoxLayout *inflateValRow = new QHBoxLayout();
    inflateValRow->addWidget(new QLabel("Offset:"));
    inflateSpin = new QDoubleSpinBox();
    inflateSpin->setMinimum(-1e6);
    inflateSpin->setMaximum(1e6);
    inflateSpin->setValue(0.0);
    inflateSpin->setDecimals(5);
    inflateSpin->setSingleStep(0.001);
    inflateSpin->setEnabled(false);
    inflateValRow->addWidget(inflateSpin, 1);
    inflateLayout->addLayout(inflateValRow);

    inflateSlider = new QSlider(Qt::Horizontal);
    inflateSlider->setMinimum(-1000);
    inflateSlider->setMaximum(1000);
    inflateSlider->setValue(0);
    inflateSlider->setEnabled(false);
    inflateLayout->addWidget(inflateSlider);

    connect(inflateSlider, &QSlider::valueChanged, this, [this](int val)
            {
        double offset = (inflateScale > 1e-10) ? val / 1000.0 * inflateScale : 0.0;
        inflateSpin->blockSignals(true);
        inflateSpin->setValue(offset);
        inflateSpin->blockSignals(false);
        applyInflate(offset); });
    connect(inflateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val)
            {
        int sliderVal = (inflateScale > 1e-10) ? (int)(val / inflateScale * 1000.0) : 0;
        inflateSlider->blockSignals(true);
        inflateSlider->setValue(std::max(-1000, std::min(1000, sliderVal)));
        inflateSlider->blockSignals(false);
        applyInflate(val); });

    layout->addWidget(inflateGroup);

    // ── Signals ───────────────────────────────────────────────────────────
    connect(targetFacesSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onTargetFacesChanged);
    connect(simplificationSlider, &QSlider::valueChanged, this, [this](int val)
            {
        int targetFaces = std::max(4, (int)(originalFaceCount * val / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(targetFaces);
        targetFacesSpinBox->blockSignals(false); });

    layout->addStretch();
    return w;
}

QWidget *MainWindow::buildHeightmapControls()
{
    QWidget *w = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(w);

    QGroupBox *ctrlGroup = new QGroupBox("Baking Controls");
    QVBoxLayout *ctrlOuter = new QVBoxLayout(ctrlGroup);

    QHBoxLayout *ctrlRow = new QHBoxLayout();
    ctrlRow->addWidget(new QLabel("Resolution:"));
    hmResCombo = new QComboBox();
    hmResCombo->addItem("128 × 128", 128);
    hmResCombo->addItem("256 × 256", 256);
    hmResCombo->addItem("512 × 512", 512);
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

    QHBoxLayout *progressRow = new QHBoxLayout();
    hmProgressBar = new QProgressBar();
    hmProgressBar->setRange(0, 100);
    hmProgressBar->setValue(0);
    hmProgressBar->setTextVisible(true);
    hmProgressBar->setFixedHeight(18);
    progressRow->addWidget(hmProgressBar, 1);

    hmProgressLabel = new QLabel("Ready");
    hmProgressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(hmProgressLabel);
    ctrlOuter->addLayout(progressRow);

    mainLayout->addWidget(ctrlGroup);
    mainLayout->addStretch();
    return w;
}

QWidget *MainWindow::buildTexturePrepControls()
{
    QWidget *w = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(w);

    QGroupBox *ctrlGroup = new QGroupBox("Input Textures && Baking Controls");
    QVBoxLayout *ctrlOuter = new QVBoxLayout(ctrlGroup);

    // Input texture thumbnails
    static const char *thumbCaptions[3] = {"Color", "Depth", "Normal"};
    QHBoxLayout *thumbRow = new QHBoxLayout();
    for (int i = 0; i < 3; i++)
    {
        QVBoxLayout *col = new QVBoxLayout();
        col->setSpacing(2);
        tpThumb[i] = new QLabel();
        tpThumb[i]->setFixedSize(56, 56);
        tpThumb[i]->setAlignment(Qt::AlignCenter);
        tpThumb[i]->setStyleSheet("background-color:#1e1e1e;border:1px solid #555;");
        tpThumb[i]->setText("—");
        col->addWidget(tpThumb[i]);
        QLabel *caption = new QLabel(thumbCaptions[i]);
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("font-size: 10px;");
        col->addWidget(caption);
        thumbRow->addLayout(col);
    }
    ctrlOuter->addLayout(thumbRow);

    QHBoxLayout *resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel("Resolution:"));
    tpResCombo = new QComboBox();
    tpResCombo->addItem("128 × 128",   128);
    tpResCombo->addItem("256 × 256",   256);
    tpResCombo->addItem("512 × 512",   512);
    tpResCombo->addItem("1024 × 1024", 1024);
    tpResCombo->addItem("2048 × 2048", 2048);
    tpResCombo->setCurrentIndex(2);
    resRow->addWidget(tpResCombo, 1);
    ctrlOuter->addLayout(resRow);

    QHBoxLayout *seamRow = new QHBoxLayout();
    seamRow->addWidget(new QLabel("Seam Band:"));
    tpSeamBandSpin = new QSpinBox();
    tpSeamBandSpin->setRange(1, 32);
    tpSeamBandSpin->setValue(4);
    tpSeamBandSpin->setToolTip(
        "Width (in texels) of the atlas-leap band baked around UV seams.\n"
        "Wider bands tolerate longer relief-mapping rays crossing islands.");
    seamRow->addWidget(tpSeamBandSpin, 1);
    ctrlOuter->addLayout(seamRow);

    tpGenerateBtn = new QPushButton("Generate");
    tpGenerateBtn->setEnabled(false);
    connect(tpGenerateBtn, &QPushButton::clicked, this, &MainWindow::onTpGenerate);
    ctrlOuter->addWidget(tpGenerateBtn);

    QHBoxLayout *progressRow = new QHBoxLayout();
    tpProgressBar = new QProgressBar();
    tpProgressBar->setRange(0, 100);
    tpProgressBar->setValue(0);
    tpProgressBar->setTextVisible(true);
    tpProgressBar->setFixedHeight(18);
    progressRow->addWidget(tpProgressBar, 1);

    tpProgressLabel = new QLabel("Ready");
    tpProgressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(tpProgressLabel);
    ctrlOuter->addLayout(progressRow);

    mainLayout->addWidget(ctrlGroup);
    mainLayout->addStretch();
    return w;
}

QWidget *MainWindow::buildReliefMappingControls()
{
    // Called after buildReliefMappingViewport(), so reliefWidget/Compare/Original are set.
    QGroupBox *ctrlGroup = new QGroupBox("Relief Mapping Parameters");
    QVBoxLayout *ctrlLayout = new QVBoxLayout(ctrlGroup);

    reliefEnabledCheck = new QCheckBox("Enable Relief Mapping");
    reliefEnabledCheck->setChecked(true);
    connect(reliefEnabledCheck, &QCheckBox::toggled, reliefWidget, &Orbital3DView::setReliefEnabled);
    ctrlLayout->addWidget(reliefEnabledCheck);

    QHBoxLayout *stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    reliefStepsSpin = new QSpinBox();
    reliefStepsSpin->setRange(1, 256);
    reliefStepsSpin->setValue(64);
    connect(reliefStepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget, &Orbital3DView::setSteps);
    stepsRow->addWidget(reliefStepsSpin, 1);
    ctrlLayout->addLayout(stepsRow);

    QHBoxLayout *binRow = new QHBoxLayout();
    binRow->addWidget(new QLabel("Binary Steps:"));
    reliefBinaryStepsSpin = new QSpinBox();
    reliefBinaryStepsSpin->setRange(0, 16);
    reliefBinaryStepsSpin->setValue(5);
    connect(reliefBinaryStepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), reliefWidget, &Orbital3DView::setBinarySteps);
    binRow->addWidget(reliefBinaryStepsSpin, 1);
    ctrlLayout->addLayout(binRow);

    QHBoxLayout *depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    reliefDepthScaleSpin = new QDoubleSpinBox();
    reliefDepthScaleSpin->setRange(0.0, 2.0);
    reliefDepthScaleSpin->setSingleStep(0.01);
    reliefDepthScaleSpin->setDecimals(4);
    reliefDepthScaleSpin->setValue(0.05);
    connect(reliefDepthScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), reliefWidget, &Orbital3DView::setDepthScale);
    depthRow->addWidget(reliefDepthScaleSpin, 1);
    ctrlLayout->addLayout(depthRow);

    reliefUseAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    reliefUseAtlasCheck->setChecked(true);
    connect(reliefUseAtlasCheck, &QCheckBox::toggled, reliefWidget, &Orbital3DView::setUseAtlas);
    ctrlLayout->addWidget(reliefUseAtlasCheck);

    QHBoxLayout *debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    reliefDebugViewCombo = new QComboBox();
    reliefDebugViewCombo->addItem("Shaded",         0);
    reliefDebugViewCombo->addItem("Step Count",     1);
    reliefDebugViewCombo->addItem("Leap Count",     2);
    reliefDebugViewCombo->addItem("UV After Relief", 3);
    connect(reliefDebugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
            { reliefWidget->setDebugView(reliefDebugViewCombo->currentData().toInt()); });
    debugRow->addWidget(reliefDebugViewCombo, 1);
    ctrlLayout->addLayout(debugRow);

    QHBoxLayout *viewRow = new QHBoxLayout();
    reliefWireframeCheck = new QCheckBox("Wireframe");
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefWidget,        &Orbital3DView::setWireframe);
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefCompareWidget,  &Orbital3DView::setWireframe);
    connect(reliefWireframeCheck, &QCheckBox::toggled, reliefOriginalWidget, &Orbital3DView::setWireframe);
    viewRow->addWidget(reliefWireframeCheck);
    reliefCullFaceCheck = new QCheckBox("Backface Cull");
    reliefCullFaceCheck->setChecked(true);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefWidget,        &Orbital3DView::setCullFace);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefCompareWidget,  &Orbital3DView::setCullFace);
    connect(reliefCullFaceCheck, &QCheckBox::toggled, reliefOriginalWidget, &Orbital3DView::setCullFace);
    viewRow->addWidget(reliefCullFaceCheck);
    ctrlLayout->addLayout(viewRow);

    reliefResetCamBtn = new QPushButton("Reset Camera");
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefWidget,        &Orbital3DView::resetCamera);
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefCompareWidget,  &Orbital3DView::resetCamera);
    connect(reliefResetCamBtn, &QPushButton::clicked, reliefOriginalWidget, &Orbital3DView::resetCamera);
    ctrlLayout->addWidget(reliefResetCamBtn);

    ctrlLayout->addStretch();
    return ctrlGroup;
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void MainWindow::setupUI()
{
    // ── Context toolbar ────────────────────────────────────────────────────
    contextToolBar = addToolBar("Contexts");
    contextToolBar->setMovable(false);
    contextToolBar->setStyleSheet(R"(
        QToolBar { background: #2d2d2d; border: none; spacing: 2px; padding: 2px 6px; }
        QToolButton { background: transparent; color: #ccc; border: none;
                      border-radius: 3px; padding: 5px 14px; font-weight: bold; }
        QToolButton:checked { background: #4a7abf; color: white; }
        QToolButton:hover:!checked { background: #3d3d3d; }
    )");
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    const char *labels[] = {"Mesh", "Heightmap", "Textures", "Relief"};
    for (int i = 0; i < 4; ++i)
    {
        auto *act = new QAction(labels[i], this);
        act->setCheckable(true);
        group->addAction(act);
        contextToolBar->addAction(act);
        connect(act, &QAction::triggered, this, [this, i](bool) { switchContext(i); });
    }
    group->actions().first()->setChecked(true);

    // ── Viewport stack (central widget) ────────────────────────────────────
    // Viewport builders must run before controls builders because the controls
    // builders connect to GL widget pointers set by the viewport builders.
    viewportStack = new QStackedWidget();
    viewportStack->addWidget(buildSimplifierViewport());
    viewportStack->addWidget(buildHeightmapViewport());
    viewportStack->addWidget(buildTexturePrepViewport());
    viewportStack->addWidget(buildReliefMappingViewport());
    setCentralWidget(viewportStack);

    // ── Controls dock ──────────────────────────────────────────────────────
    dockStack = new QStackedWidget();
    dockStack->addWidget(buildSimplifierControls());
    dockStack->addWidget(buildHeightmapControls());
    dockStack->addWidget(buildTexturePrepControls());
    dockStack->addWidget(buildReliefMappingControls());

    controlsDock = new QDockWidget("Controls", this);
    controlsDock->setWidget(dockStack);
    controlsDock->setFeatures(QDockWidget::DockWidgetMovable);
    controlsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    controlsDock->setMaximumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, controlsDock);

    statusLabel = new QLabel("Ready");
    statusBar()->addWidget(statusLabel);
}

// ─── Menu ─────────────────────────────────────────────────────────────────────

void MainWindow::createMenuBar()
{
    QMenuBar *menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu *fileMenu = menuBar->addMenu("&File");

    QAction *loadAction = fileMenu->addAction("&Load Model...");
    connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadModel);

    QAction *saveAction = fileMenu->addAction("&Save Simplified...");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveSimplified);

    fileMenu->addSeparator();

    QAction *exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu *helpMenu = menuBar->addMenu("&Help");
    QAction *aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, [this]()
            { QMessageBox::about(this, "About QEM Simplifier",
                                 "QEM Mesh Simplifier\n\n"
                                 "Quadric Error Metrics simplification with Qt GUI\n"
                                 "Mouse: Drag to rotate, Scroll to zoom\n"
                                 "Formats: OBJ, GLTF\n\n"
                                 "Heightmap tab: bakes displacement between simplified and original mesh\n"
                                 "via shared UV correspondence."); });
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void MainWindow::switchContext(int index)
{
    viewportStack->setCurrentIndex(index);
    dockStack->setCurrentIndex(index);
    if (index == 3)
        trySyncReliefWidget();
}

void MainWindow::onLoadModel()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Open Mesh File", "",
                                                    "Model Files (*.obj *.gltf *.glb);;OBJ Files (*.obj);;GLTF Files (*.gltf *.glb);;All Files (*)");

    if (fileName.isEmpty())
        return;

    originalMesh = std::make_unique<QEMSimplifier>();
    simplifiedMesh = std::make_unique<QEMSimplifier>();

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive))
        success = originalMesh->loadOBJ(fileName.toStdString());
    else
        success = originalMesh->loadGLTF(fileName.toStdString());

    if (!success)
    {
        QMessageBox::critical(this, "Error", "Failed to load mesh file!");
        return;
    }

    // Start "simplified" as a copy of the original so Textures Preparation / Relief
    // Mapping work even before the user runs Simplify (e.g. baking with an already
    // low-poly model and pre-existing textures).
    *simplifiedMesh = *originalMesh;

    currentFilePath = fileName;
    originalFaceCount = originalMesh->faceCount();
    targetFaceCount = std::max(4, originalFaceCount / 4);

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
    if (!hasTexture)
        texturedCheck->setChecked(false);

    bool hasUVs = false;
    for (const auto &v : originalMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12)
        {
            hasUVs = true;
            break;
        }
    uvViewCheck->setEnabled(hasUVs);
    if (!hasUVs)
        uvViewCheck->setChecked(false);

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
    inflateSlider->blockSignals(true);
    inflateSlider->setValue(0);
    inflateSlider->blockSignals(false);
    inflateSlider->setEnabled(false);
    inflateSpin->blockSignals(true);
    inflateSpin->setValue(0.0);
    inflateSpin->blockSignals(false);
    inflateSpin->setEnabled(false);

    // Reset Textures Preparation / Relief Mapping state — the previous bake was for
    // the old mesh's UV layout and no longer applies. simplifiedMesh already holds a
    // copy of the freshly loaded mesh (see above), so the relief widget can pick it
    // up as soon as its tab is shown.
    tpResult = TexturePrepResult{};
    reliefMeshPending = true;
    reliefTexturesPending = false;
    updateTpThumbnails();
    updateTpGenerateEnabled();
    for (int i = 0; i < 4; i++)
    {
        tpPreview[i]->setText("(not generated)");
        tpPreview[i]->setPixmap(QPixmap());
        tpInfoLabel[i]->setText("—");
        tpSaveBtn[i]->setEnabled(false);
        tpMipSpin[i]->setEnabled(false);
        tpMipSpin[i]->setRange(0, 0);
    }

    updateStatusBar();
}

void MainWindow::onSaveSimplified()
{
    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning",
                             "No simplified mesh to save!\nRun simplification first.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "Save Simplified Mesh", "",
                                                    "OBJ Files (*.obj);;GLTF Files (*.gltf);;All Files (*)");

    if (fileName.isEmpty())
        return;

    bool success = false;
    if (fileName.endsWith(".obj", Qt::CaseInsensitive))
        success = simplifiedMesh->saveOBJ(fileName.toStdString());
    else
        success = simplifiedMesh->saveGLTF(fileName.toStdString());

    if (success)
    {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Success", "Mesh saved successfully!");
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to save mesh!");
    }
}

void MainWindow::onSimplify()
{
    if (!originalMesh || originalMesh->faceCount() == 0)
    {
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
    for (const auto &f : simplifiedMesh->faces)
    {
        if (f.removed)
            continue;
        const auto &p0 = baseSimplifiedPositions[f.v[0]];
        const auto &p1 = baseSimplifiedPositions[f.v[1]];
        const auto &p2 = baseSimplifiedPositions[f.v[2]];
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        simplifiedVertexNormals[f.v[0]] += n;
        simplifiedVertexNormals[f.v[1]] += n;
        simplifiedVertexNormals[f.v[2]] += n;
    }

    // Vértices duplicados na mesma posição 3D (ex.: costuras de UV, separadas
    // em vertices distintos no loadOBJ) devem inflar juntos. Caso contrário,
    // cada cópia usa só suas próprias faces incidentes, as normais divergem,
    // e a costura abre um buraco ao inflar mesmo com seam vertices travados.
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto &p : baseSimplifiedPositions)
        {
            bmin = bmin.cwiseMin(p);
            bmax = bmax.cwiseMax(p);
        }
        double cell = std::max((bmax - bmin).norm() * 1e-7, 1e-9);

        auto quantize = [cell](const Eigen::Vector3d &p)
        {
            return std::make_tuple(
                (long long)std::llround(p.x() / cell),
                (long long)std::llround(p.y() / cell),
                (long long)std::llround(p.z() / cell));
        };

        std::map<std::tuple<long long, long long, long long>, int> groupId;
        simplifiedVertexGroup.assign(simplifiedMesh->vertices.size(), -1);
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        {
            if (simplifiedMesh->vertices[i].removed)
                continue;
            auto key = quantize(baseSimplifiedPositions[i]);
            auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
            simplifiedVertexGroup[i] = it->second;
        }
        simplifiedVertexGroupCount = (int)groupId.size();

        std::vector<Eigen::Vector3d> groupNormal(simplifiedVertexGroupCount, Eigen::Vector3d::Zero());
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        {
            if (simplifiedMesh->vertices[i].removed)
                continue;
            groupNormal[simplifiedVertexGroup[i]] += simplifiedVertexNormals[i];
        }
        for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
        {
            if (simplifiedMesh->vertices[i].removed)
                continue;
            simplifiedVertexNormals[i] = groupNormal[simplifiedVertexGroup[i]];
        }
    }

    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
    {
        double len = simplifiedVertexNormals[i].norm();
        if (len > 1e-10)
            simplifiedVertexNormals[i] /= len;
    }

    // Set inflate range based on original mesh bounding box diagonal
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto &v : originalMesh->vertices)
        {
            if (!v.removed)
            {
                bmin = bmin.cwiseMin(v.pos);
                bmax = bmax.cwiseMax(v.pos);
            }
        }
        inflateScale = std::max((bmax - bmin).norm() * 0.5, 1e-6);
    }

    inflateSpin->blockSignals(true);
    inflateSpin->setMinimum(-inflateScale);
    inflateSpin->setMaximum(inflateScale);
    inflateSpin->setSingleStep(inflateScale / 1000.0);
    inflateSpin->setValue(0.0);
    inflateSpin->blockSignals(false);
    inflateSlider->blockSignals(true);
    inflateSlider->setValue(0);
    inflateSlider->blockSignals(false);
    inflateSlider->setEnabled(true);
    inflateSpin->setEnabled(true);

    glWidgetSimplified->setMesh(simplifiedMesh.get());
    glWidgetOverlay->setMeshes(originalMesh.get(), simplifiedMesh.get());
    updateStatusBar();

    updateTpThumbnails();
    updateTpGenerateEnabled();

    reliefMeshPending = true;
    trySyncReliefWidget();
}

void MainWindow::onTargetFacesChanged(int value)
{
    targetFaceCount = value;
}

void MainWindow::onResetCameras()
{
    if (glWidgetOriginal)   glWidgetOriginal->resetCamera();
    if (glWidgetSimplified) glWidgetSimplified->resetCamera();
    if (glWidgetOverlay)    glWidgetOverlay->resetCamera();
}

// ─── Heightmap baking ────────────────────────────────────────────────────────

void MainWindow::setBakeButtonsEnabled(bool enabled)
{
    hmBakeBtn->setEnabled(enabled);
}

void MainWindow::launchBake()
{
    if (hmThread && hmThread->isRunning())
        return;

    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning",
                             "Simplify the mesh first before baking a heightmap.");
        return;
    }
    if (!originalMesh || originalMesh->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning", "No original mesh loaded.");
        return;
    }

    bool hasUVs = false;
    for (const auto &v : simplifiedMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12)
        {
            hasUVs = true;
            break;
        }
    if (!hasUVs)
    {
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
    hmWorker->original = originalMesh.get();
    hmWorker->width = res;
    hmWorker->height = res;

    hmThread = new QThread(this);
    hmWorker->moveToThread(hmThread);

    connect(hmThread, &QThread::started, hmWorker, &HeightmapWorker::run);
    connect(hmWorker, &HeightmapWorker::progress, this, &MainWindow::onBakeProgress);
    connect(hmWorker, &HeightmapWorker::finished, this, &MainWindow::onBakeDone);
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
    connect(hmThread, &QThread::finished, hmThread, &QObject::deleteLater);

    statusLabel->setText(QString("Baking %1×%1…").arg(res));
    hmThread->start();
}

void MainWindow::onBake()
{
    launchBake();
}

void MainWindow::onBakeProgress(int overall, const QString &text)
{
    hmProgressBar->setValue(overall);
    hmProgressLabel->setText(text);
}

void MainWindow::onBakeDone()
{
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

void MainWindow::displayHeightmap(const HeightmapResult &r)
{
    if (!r.valid || r.image.empty())
    {
        hmPreview->setText("(bake failed)");
        hmInfoLabel->setText("Range: —");
        hmSaveBtn->setEnabled(false);
        return;
    }

    QImage img(r.image.data(), r.width, r.height, r.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    QPixmap px = QPixmap::fromImage(img);
    QSize labelSize = hmPreview->size();
    if (labelSize.isEmpty())
        labelSize = QSize(300, 300);
    hmPreview->setPixmap(
        px.scaled(labelSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    hmInfoLabel->setText(
        QString("Range: [%1, %2]")
            .arg((double)r.minH, 0, 'f', 4)
            .arg((double)r.maxH, 0, 'f', 4));

    hmSaveBtn->setEnabled(true);
}

void MainWindow::onSaveHeightmap()
{
    if (!hmResult.valid || hmResult.image.empty())
        return;

    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "Save Heightmap", "",
                                                    "PNG Image (*.png);;All Files (*)");

    if (fileName.isEmpty())
        return;

    QImage img(hmResult.image.data(), hmResult.width, hmResult.height,
               hmResult.width, QImage::Format_Grayscale8);
    img = img.mirrored(false, true);

    if (img.save(fileName))
    {
        statusLabel->setText("Saved: " + fileName);
        QMessageBox::information(this, "Saved", "Heightmap saved as PNG.");
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to save image.");
    }
}

// ─── Textures Preparation ────────────────────────────────────────────────────

void MainWindow::updateTpThumbnails()
{
    auto setThumb = [this](int idx, const QImage &img, const char *emptyText)
    {
        if (img.isNull())
        {
            tpThumb[idx]->setPixmap(QPixmap());
            tpThumb[idx]->setText(emptyText);
        }
        else
        {
            tpThumb[idx]->setPixmap(QPixmap::fromImage(img)
                                        .scaled(tpThumb[idx]->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };

    QImage colorImg, normalImg;
    if (simplifiedMesh)
    {
        colorImg = rgbaTextureToQImage(simplifiedMesh->textureData,
                                       simplifiedMesh->textureWidth, simplifiedMesh->textureHeight);
        normalImg = rgbaTextureToQImage(simplifiedMesh->normalTextureData,
                                        simplifiedMesh->normalTextureWidth, simplifiedMesh->normalTextureHeight);
    }
    setThumb(0, colorImg, "(none)");
    setThumb(2, normalImg, "(none)");

    QImage depthImg;
    if (hmResult.valid && !hmResult.image.empty())
    {
        depthImg = QImage(hmResult.image.data(), hmResult.width, hmResult.height,
                          hmResult.width, QImage::Format_Grayscale8)
                       .mirrored(false, true);
    }
    setThumb(1, depthImg, "(not baked)");
}

void MainWindow::updateTpGenerateEnabled()
{
    bool hasMesh = simplifiedMesh && simplifiedMesh->faceCount() > 0;
    bool hasColor = simplifiedMesh && !simplifiedMesh->textureData.empty();
    bool hasNormal = simplifiedMesh && !simplifiedMesh->normalTextureData.empty();
    bool hasDepth = hmResult.valid && !hmResult.image.empty();
    tpGenerateBtn->setEnabled(hasMesh && hasColor && hasNormal && hasDepth);
}

void MainWindow::onTpGenerate()
{
    if (tpThread && tpThread->isRunning())
        return;
    if (!simplifiedMesh || simplifiedMesh->faceCount() == 0)
    {
        QMessageBox::warning(this, "Warning", "Simplify the mesh first before preparing textures.");
        return;
    }
    if (simplifiedMesh->textureData.empty() || simplifiedMesh->normalTextureData.empty())
    {
        QMessageBox::warning(this, "Warning",
                             "The model has no embedded color and/or normal texture.\n"
                             "Load a GLTF with a baseColorTexture and normalTexture.");
        return;
    }
    if (!hmResult.valid || hmResult.image.empty())
    {
        QMessageBox::warning(this, "Warning",
                             "Bake a heightmap first (Heightmap context) — it is used as the depth input.");
        return;
    }

    bool hasUVs = false;
    for (const auto &v : simplifiedMesh->vertices)
        if (v.uv.squaredNorm() > 1e-12)
        {
            hasUVs = true;
            break;
        }
    if (!hasUVs)
    {
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
    for (int i = 0; i < 4; i++)
    {
        tpPreview[i]->setText("(generating…)");
        tpPreview[i]->setPixmap(QPixmap());
        tpInfoLabel[i]->setText("—");
        tpSaveBtn[i]->setEnabled(false);
        tpMipSpin[i]->setEnabled(false);
    }

    tpWorker = new TexturePrepWorker();
    tpWorker->mesh = simplifiedMesh.get();
    tpWorker->colorImg = rgbaTextureToQImage(simplifiedMesh->textureData,
                                             simplifiedMesh->textureWidth, simplifiedMesh->textureHeight);
    tpWorker->normalImg = rgbaTextureToQImage(simplifiedMesh->normalTextureData,
                                              simplifiedMesh->normalTextureWidth, simplifiedMesh->normalTextureHeight);
    tpWorker->depthImg = QImage(hmResult.image.data(), hmResult.width, hmResult.height,
                                hmResult.width, QImage::Format_Grayscale8)
                             .mirrored(false, true);
    tpWorker->workRes = res;
    tpWorker->seamBandTexels = seamBand;

    tpThread = new QThread(this);
    tpWorker->moveToThread(tpThread);

    connect(tpThread, &QThread::started, tpWorker, &TexturePrepWorker::run);
    connect(tpWorker, &TexturePrepWorker::progress, this, &MainWindow::onTpProgress);
    connect(tpWorker, &TexturePrepWorker::finished, this, &MainWindow::onTpDone);
    connect(tpWorker, &TexturePrepWorker::finished, tpThread, &QThread::quit);
    // tpWorker is deleted explicitly in onTpDone(), after it has read tpWorker->result —
    // see the comment on the equivalent hmWorker connection in launchBake() for why a
    // finished->deleteLater connection on tpWorker itself would race the cross-thread
    // delivery of finished() to onTpDone().
    // See launchBake(): only delete the QThread once it has actually finished, not
    // right after quit() is merely requested.
    connect(tpThread, &QThread::finished, tpThread, &QObject::deleteLater);

    statusLabel->setText(QString("Baking textures %1×%1…").arg(res));
    tpThread->start();
}

void MainWindow::onTpProgress(int overall, const QString &text)
{
    tpProgressBar->setValue(overall);
    tpProgressLabel->setText(text);
}

void MainWindow::onTpDone()
{
    tpResult = tpWorker->result;

    // Read of tpWorker->result is done — safe to delete it now. tpThread
    // self-deletes once truly idle (see QThread::finished connection in onTpGenerate).
    tpWorker->deleteLater();
    tpWorker = nullptr;
    tpThread = nullptr;

    if (!tpResult.valid)
    {
        QMessageBox::critical(this, "Error", "Failed to generate textures (could not load one of the input images).");
        for (int i = 0; i < 4; i++)
            tpPreview[i]->setText("(failed)");
    }
    else
    {
        int levels[4] = {
            tpResult.colorMap.levelCount(), tpResult.reliefMap.levelCount(),
            tpResult.normalMap.levelCount(), 1};
        for (int i = 0; i < 4; i++)
        {
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

QImage MainWindow::mipLevelToQImage(const std::vector<float> &data, int w, int h, int channels, bool remapSigned,
                                    const bool *showChannels) const
{
    static const bool kAllShown[4] = {true, true, true, true};
    if (!showChannels)
        showChannels = kAllShown;

    QImage::Format fmt = (channels == 3) ? QImage::Format_RGB888 : QImage::Format_RGBA8888;
    QImage img(w, h, fmt);
    auto remap = [&](float v)
    { return remapSigned ? v * 0.5f + 0.5f : v; };
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
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

QImage MainWindow::offsetMapMaskImage() const
{
    const OffsetMapResult &o = tpResult.offsetMap;
    QImage img(std::max(1, o.width), std::max(1, o.height), QImage::Format_RGB888);
    for (int y = 0; y < o.height; y++)
    {
        for (int x = 0; x < o.width; x++)
        {
            float valid = o.data[((size_t)y * o.width + x) * 4 + 3];
            img.setPixelColor(x, y, valid > 0.0f ? QColor(255, 60, 60) : QColor(20, 20, 20));
        }
    }
    return img;
}

void MainWindow::updateTpPreview(int idx)
{
    if (!tpResult.valid)
        return;

    QImage img;
    QString info;

    if (idx == 3)
    {
        img = offsetMapMaskImage();
        info = QString("%1×%2 (seam mask)").arg(tpResult.offsetMap.width).arg(tpResult.offsetMap.height);
    }
    else
    {
        const MipPyramid &p = (idx == 0) ? tpResult.colorMap : (idx == 1) ? tpResult.reliefMap
                                                                          : tpResult.normalMap;
        int level = std::min(tpMipSpin[idx]->value(), p.levelCount() - 1);
        if (level < 0)
            return;
        int w = std::max(1, p.width >> level), h = std::max(1, p.height >> level);
        bool show[4] = {
            tpChannelCheck[idx][0]->isChecked(), tpChannelCheck[idx][1]->isChecked(),
            tpChannelCheck[idx][2]->isChecked(), tpChannelCheck[idx][3]->isChecked()};
        img = mipLevelToQImage(p.mips[level], w, h, p.channels, /*remapSigned=*/idx == 2, show);
        info = QString("%1×%2, %3 mips").arg(w).arg(h).arg(p.levelCount());
    }

    img = img.mirrored(false, true);
    QPixmap px = QPixmap::fromImage(img);
    QSize labelSize = tpPreview[idx]->size();
    if (labelSize.isEmpty())
        labelSize = QSize(220, 220);
    tpPreview[idx]->setPixmap(px.scaled(labelSize, Qt::KeepAspectRatio, Qt::FastTransformation));
    tpInfoLabel[idx]->setText(info);
}

void MainWindow::onTpSave(int idx)
{
    if (!tpResult.valid)
        return;
    static const char *names[4] = {"Color Map", "Relief Map", "Normal Map", "Offset Map"};

    QString fileName = QFileDialog::getSaveFileName(this, QString("Save %1").arg(names[idx]), "",
                                                    "PNG Image (*.png);;All Files (*)");
    if (fileName.isEmpty())
        return;

    QImage img;
    if (idx == 3)
    {
        img = offsetMapMaskImage();
    }
    else
    {
        const MipPyramid &p = (idx == 0) ? tpResult.colorMap : (idx == 1) ? tpResult.reliefMap
                                                                          : tpResult.normalMap;
        img = mipLevelToQImage(p.mips[0], p.width, p.height, p.channels, /*remapSigned=*/idx == 2);
    }
    img = img.mirrored(false, true);

    if (img.save(fileName))
    {
        statusLabel->setText("Saved: " + fileName);
    }
    else
    {
        QMessageBox::critical(this, "Error", "Failed to save image.");
    }
}

// ─── Relief Mapping ──────────────────────────────────────────────────────────

void MainWindow::trySyncReliefWidget()
{
    if (!reliefWidget || viewportStack->currentIndex() != 3)
        return;

    if (reliefMeshPending && simplifiedMesh && simplifiedMesh->faceCount() > 0)
    {
        reliefWidget->setMesh(simplifiedMesh.get());
        reliefCompareWidget->setMesh(simplifiedMesh.get());
        if (originalMesh)
            reliefOriginalWidget->setMesh(originalMesh.get());
        reliefMeshPending = false;
    }
    if (reliefTexturesPending && tpResult.valid)
    {
        reliefWidget->setTextures(tpResult);
        reliefCompareWidget->setColorTexture(tpResult.colorMap);
        reliefOriginalWidget->setColorTexture(tpResult.colorMap);
        reliefTexturesPending = false;
    }
}

// ─── Status / helpers ────────────────────────────────────────────────────────

void MainWindow::updateStatusBar()
{
    QString originalStats = QString("Original: %1 faces, %2 vertices")
                                .arg(originalMesh->faceCount())
                                .arg(originalMesh->vertexCount());

    QString simplifiedStats = QString("Simplified: %1 faces, %2 vertices")
                                  .arg(simplifiedMesh->faceCount())
                                  .arg(simplifiedMesh->vertexCount());

    glWidgetOriginal->setStats(originalMesh->faceCount(), originalMesh->vertexCount());
    glWidgetSimplified->setStats(simplifiedMesh->faceCount(), simplifiedMesh->vertexCount());

    if (simplifiedMesh->faceCount() > 0)
    {
        double reduction = 100.0 *
                           (1.0 - (double)simplifiedMesh->faceCount() / originalMesh->faceCount());
        statusLabel->setText(QString("Reduction: %1%").arg(reduction, 0, 'f', 1));
    }
    else
    {
        statusLabel->setText("Ready");
    }
}

// ─── Inflate / Deflate ───────────────────────────────────────────────────────

void MainWindow::applyInflate(double offset)
{
    if (baseSimplifiedPositions.empty())
        return;
    for (size_t i = 0; i < simplifiedMesh->vertices.size(); i++)
    {
        if (!simplifiedMesh->vertices[i].removed)
            simplifiedMesh->vertices[i].pos =
                baseSimplifiedPositions[i] + offset * simplifiedVertexNormals[i];
    }
    glWidgetSimplified->updateMeshData();
    glWidgetOverlay->updateSecondaryMesh();
}

void MainWindow::computeAutoTarget()
{
    if (originalFaceCount > 0)
    {
        int target = std::max(4,
                              (int)(originalFaceCount * simplificationSlider->value() / 100.0));
        targetFacesSpinBox->blockSignals(true);
        targetFacesSpinBox->setValue(target);
        targetFacesSpinBox->blockSignals(false);
    }
}
