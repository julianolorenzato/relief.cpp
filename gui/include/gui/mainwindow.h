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
#include <QToolBar>
#include <QDockWidget>
#include <QStackedWidget>
#include <memory>
#include "relief/qem.h"
#include "gui/orbital3dview.h"
#include "relief/heightmap.h"
#include "gui/heightmap_worker.h"
#include "gui/texture_prep_worker.h"

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
    void switchContext(int index);

    QWidget* buildSimplifierViewport();
    QWidget* buildSimplifierControls();
    QWidget* buildHeightmapViewport();
    QWidget* buildHeightmapControls();
    QWidget* buildTexturePrepViewport();
    QWidget* buildTexturePrepControls();
    QWidget* buildReliefMappingViewport();
    QWidget* buildReliefMappingControls();

    void updateTpPreview(int idx);
    QImage mipLevelToQImage(const std::vector<float>& data, int w, int h, int channels, bool remapSigned,
                             const bool* showChannels = nullptr) const;
    QImage offsetMapMaskImage() const;
    void trySyncReliefWidget();

    void applyInflate(double offset);

    void displayHeightmap(const HeightmapResult& r);
    void launchBake();
    void setBakeButtonsEnabled(bool enabled);
    void updateTpThumbnails();
    void updateTpGenerateEnabled();

    // ── Mesh data ────────────────────────────────────────────────────────────
    std::unique_ptr<QEMSimplifier> originalMesh;
    std::unique_ptr<QEMSimplifier> simplifiedMesh;

    // ── Context switching ────────────────────────────────────────────────────
    QToolBar*        contextToolBar  = nullptr;
    QDockWidget*     controlsDock    = nullptr;
    QStackedWidget*  dockStack       = nullptr;
    QStackedWidget*  viewportStack   = nullptr;

    // ── Simplifier context ───────────────────────────────────────────────────
    Orbital3DView*   glWidgetOriginal   = nullptr;  // mode: Solid
    Orbital3DView*   glWidgetSimplified = nullptr;  // mode: Solid
    Orbital3DView*   glWidgetOverlay    = nullptr;  // mode: Overlay
    QSlider*  simplificationSlider  = nullptr;
    QSpinBox* targetFacesSpinBox    = nullptr;
    QLabel*   statusLabel           = nullptr;

    QCheckBox* wireframeCheck            = nullptr;
    QCheckBox* cullFaceCheck             = nullptr;
    QCheckBox* texturedCheck             = nullptr;
    QCheckBox* uvViewCheck               = nullptr;
    QComboBox* boundaryModeCombo        = nullptr;
    QCheckBox* envelopeConstraintCheck  = nullptr;
    QCheckBox* useOptimalCandidateCheck = nullptr;
    QCheckBox* showBoundaryEdgesCheck   = nullptr;
    QCheckBox* showInternalEdgesCheck   = nullptr;

    QSlider*        inflateSlider  = nullptr;
    QDoubleSpinBox* inflateSpin    = nullptr;

    std::vector<Eigen::Vector3d> baseSimplifiedPositions;
    std::vector<Eigen::Vector3d> simplifiedVertexNormals;
    // Vértices que ocupam a mesma posição 3D (duplicados em costuras de UV)
    // recebem o mesmo group id, para que normais e cover offsets fiquem
    // sincronizados entre as cópias e a costura não se abra ao inflar.
    std::vector<int> simplifiedVertexGroup;
    int simplifiedVertexGroupCount = 0;
    double inflateScale = 1.0;

    // ── Heightmap context ────────────────────────────────────────────────────
    QComboBox*    hmResCombo      = nullptr;
    QPushButton*  hmBakeBtn       = nullptr;
    QProgressBar* hmProgressBar   = nullptr;
    QLabel*       hmProgressLabel = nullptr;

    QLabel*      hmPreview   = nullptr;
    QLabel*      hmInfoLabel = nullptr;
    QPushButton* hmSaveBtn   = nullptr;

    HeightmapResult   hmResult;
    HeightmapWorker*  hmWorker = nullptr;
    QThread*          hmThread = nullptr;

    // ── Textures Preparation context ─────────────────────────────────────────
    // index 0 = Color, 1 = Depth, 2 = Normal (inputs, sourced automatically from
    // the model's own textures and the baked heightmap); preview/save panels are
    // 0 = Color Map, 1 = Relief Map, 2 = Normal Map, 3 = Offset Map (outputs).
    QLabel*          tpThumb[3]       = {};
    QComboBox*       tpResCombo       = nullptr;
    QSpinBox*        tpSeamBandSpin   = nullptr;
    QPushButton*     tpGenerateBtn    = nullptr;
    QProgressBar*    tpProgressBar    = nullptr;
    QLabel*          tpProgressLabel  = nullptr;
    QLabel*          tpPreview[4]     = {};
    QLabel*          tpInfoLabel[4]   = {};
    QSpinBox*        tpMipSpin[4]     = {};
    QPushButton*     tpSaveBtn[4]     = {};
    // Per-panel R/G/B/A preview toggles (panels 0=Color, 1=Relief, 2=Normal);
    // panel 3 (Offset Map) renders a derived mask and has no channel toggles.
    QCheckBox*         tpChannelCheck[3][4] = {};
    TexturePrepWorker* tpWorker = nullptr;
    QThread*           tpThread = nullptr;
    TexturePrepResult  tpResult;

    // ── Relief Mapping context ───────────────────────────────────────────────
    Orbital3DView*   reliefWidget         = nullptr;  // mode: Relief
    Orbital3DView*   reliefCompareWidget  = nullptr;  // mode: Textured
    Orbital3DView*   reliefOriginalWidget = nullptr;  // mode: Textured
    QCheckBox*       reliefEnabledCheck       = nullptr;
    QSpinBox*        reliefStepsSpin          = nullptr;
    QSpinBox*        reliefBinaryStepsSpin    = nullptr;
    QDoubleSpinBox*  reliefDepthScaleSpin     = nullptr;
    QCheckBox*       reliefUseAtlasCheck      = nullptr;
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
