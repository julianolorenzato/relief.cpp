#pragma once
#include <QMainWindow>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include <QList>
#include <QImage>
#include <QTabWidget>
#include <QStackedWidget>
#include <memory>
#include "qem.h"
#include "glwidget.h"
#include "overlayglwidget.h"
#include "heightmap.h"
#include "heightmap_worker.h"
#include "texture_prep_worker.h"
#include "reliefglwidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onLoadModel();
    void onSaveSimplified();
    void onSimplify();
    void onTargetFacesChanged(int value);
    void onResetCameras();

    void onSmoothCover();

    void onBake();
    void onSaveHeightmap();
    void onBakeProgress(int overall, const QString& text);
    void onBakeDone();

    void onTpGenerate();
    void onTpProgress(int overall, const QString& text);
    void onTpDone();
    void onTpSave(int idx);

private:
    void setupUI();
    void createMenuBar();
    void updateStatusBar();
    void computeAutoTarget();

    QWidget* buildSimplifierTab();
    QWidget* buildHeightmapTab();
    QWidget* buildTexturePrepTab();
    QWidget* buildReliefMappingTab();

    void updateTpPreview(int idx);
    QImage mipLevelToQImage(const std::vector<float>& data, int w, int h, int channels, bool remapSigned) const;
    QImage offsetMapMaskImage() const;
    void showReliefViewport();
    void trySyncReliefWidget();
    void onTabChanged(int index);

    void applyInflate(double offset);
    void computeSmoothCoverOffsets();

    void displayHeightmap(const HeightmapResult& r);
    void launchBake();
    void setBakeButtonsEnabled(bool enabled);
    void updateTpThumbnails();
    void updateTpGenerateEnabled();

    // ── Mesh data ────────────────────────────────────────────────────────────
    std::unique_ptr<QEMSimplifier> originalMesh;
    std::unique_ptr<QEMSimplifier> simplifiedMesh;

    // ── Simplifier tab ───────────────────────────────────────────────────────
    GLWidget*        glWidgetOriginal   = nullptr;
    GLWidget*        glWidgetSimplified = nullptr;
    OverlayGLWidget* glWidgetOverlay    = nullptr;
    QSlider*  simplificationSlider  = nullptr;
    QSpinBox* targetFacesSpinBox    = nullptr;
    QLabel*   statusLabel           = nullptr;
    QLabel*   originalStatsLabel    = nullptr;
    QLabel*   simplifiedStatsLabel  = nullptr;

    QCheckBox* wireframeCheck          = nullptr;
    QCheckBox* cullFaceCheck           = nullptr;
    QCheckBox* texturedCheck           = nullptr;
    QCheckBox* uvViewCheck             = nullptr;
    QComboBox* boundaryModeCombo       = nullptr;
    QCheckBox* envelopeConstraintCheck = nullptr;
    QCheckBox* useOptimalCandidateCheck = nullptr;
    QCheckBox* showBoundaryEdgesCheck  = nullptr;
    QCheckBox* showInternalEdgesCheck  = nullptr;

    QSlider*        inflateSlider  = nullptr;
    QDoubleSpinBox* inflateSpin    = nullptr;
    QPushButton*    smoothCoverBtn = nullptr;

    std::vector<Eigen::Vector3d> baseSimplifiedPositions;
    std::vector<Eigen::Vector3d> simplifiedVertexNormals;
    // Vértices que ocupam a mesma posição 3D (duplicados em costuras de UV)
    // recebem o mesmo group id, para que normais e cover offsets fiquem
    // sincronizados entre as cópias e a costura não se abra ao inflar.
    std::vector<int> simplifiedVertexGroup;
    int simplifiedVertexGroupCount = 0;
    // Offset por vértice (na direção da normal) calculado por computeSmoothCoverOffsets,
    // para que a cage cubra a malha original com uma folga suave em vez de um
    // único offset global de pior caso. O slider/spin de inflate soma uma
    // margem manual uniforme em cima deste campo.
    std::vector<double> simplifiedCoverOffsets;
    double inflateScale = 1.0;

    // ── Heightmap tab ────────────────────────────────────────────────────────
    QComboBox*       hmResCombo       = nullptr;
    QPushButton*     hmBakeBtn        = nullptr;
    QProgressBar*    hmProgressBar    = nullptr;
    QLabel*          hmProgressLabel  = nullptr;

    QLabel*          hmPreview        = nullptr;
    QLabel*          hmInfoLabel      = nullptr;
    QPushButton*     hmSaveBtn        = nullptr;

    HeightmapResult   hmResult;
    HeightmapWorker*  hmWorker = nullptr;
    QThread*          hmThread = nullptr;

    // ── Textures Preparation tab ─────────────────────────────────────────────
    // index 0 = Color, 1 = Depth, 2 = Normal (inputs, sourced automatically from
    // the model's own textures and the baked heightmap); preview/save panels are
    // 0 = Color Map, 1 = Relief Map, 2 = Normal Map, 3 = Offset Map (outputs).
    QLabel*          tpThumb[3]    = {};
    QComboBox*       tpResCombo       = nullptr;
    QSpinBox*        tpSeamBandSpin   = nullptr;
    QPushButton*     tpGenerateBtn    = nullptr;
    QProgressBar*    tpProgressBar    = nullptr;
    QLabel*          tpProgressLabel  = nullptr;
    QLabel*          tpPreview[4]     = {};
    QLabel*          tpInfoLabel[4]   = {};
    QSpinBox*        tpMipSpin[4]     = {};
    QPushButton*     tpSaveBtn[4]     = {};
    TexturePrepWorker* tpWorker = nullptr;
    QThread*           tpThread = nullptr;
    TexturePrepResult  tpResult;

    // ── Relief Mapping tab ───────────────────────────────────────────────────
    QTabWidget*      tabsWidget       = nullptr;
    int              reliefTabIndex   = -1;
    QStackedWidget*  reliefStack      = nullptr;
    QLabel*          reliefPlaceholder = nullptr;
    ReliefGLWidget*  reliefWidget     = nullptr;
    GLWidget*        reliefCompareWidget  = nullptr;
    GLWidget*        reliefOriginalWidget = nullptr;
    QComboBox*       reliefTypeCombo          = nullptr;
    QSpinBox*        reliefStepsSpin          = nullptr;
    QSpinBox*        reliefBinaryStepsSpin    = nullptr;
    QDoubleSpinBox*  reliefDepthScaleSpin     = nullptr;
    QCheckBox*       reliefUseAtlasCheck      = nullptr;
    QComboBox*       reliefOffsetVersionCombo = nullptr;
    QCheckBox*       reliefFilter0Check       = nullptr;
    QComboBox*       reliefDebugViewCombo     = nullptr;
    QCheckBox*       reliefWireframeCheck     = nullptr;
    QCheckBox*       reliefCullFaceCheck      = nullptr;
    QPushButton*     reliefResetCamBtn        = nullptr;
    bool             reliefMeshPending     = false;
    bool             reliefTexturesPending = false;

    // ── State ────────────────────────────────────────────────────────────────
    QString currentFilePath;
    int originalFaceCount = 0;
    int targetFaceCount   = 0;
};
