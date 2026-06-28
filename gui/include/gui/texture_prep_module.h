#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QCheckBox>
#include <QThread>
#include <QImage>
#include "relief/heightmap.h"
#include "relief/qem.h"
#include "gui/texture_prep.h"

class TexturePrepModule : public QWidget {
    Q_OBJECT

public:
    explicit TexturePrepModule(QWidget* parent = nullptr);

public slots:
    // Called when a new model is loaded: sets mesh + full reset.
    void onModelLoaded(QEMSimplifier* simplified);
    // Called when the mesh is updated after simplification: sets mesh + refreshes thumbnails/button.
    void onMeshUpdated(QEMSimplifier* simplified);
    // Called when a heightmap bake finishes: stores result + refreshes.
    void onHeightmapReady(const HeightmapResult& result);

signals:
    void texturesReady(const TexturePrepResult& result);
    void statusMessage(const QString& msg);

private slots:
    void onTpGenerate();
    void onTpProgress(int overall, const QString& text);
    void onTpDone();
    void onTpSave(int idx);

private:
    void buildUI();
    void updateThumbnails();
    void updateGenerateEnabled();
    void updatePreview(int idx);
    QImage mipLevelToQImage(const std::vector<float>& data, int w, int h, int channels,
                            bool remapSigned, const bool* showChannels = nullptr) const;
    QImage offsetMapMaskImage() const;

    // ── Input thumbnails (0=Color, 1=Depth, 2=Normal) ────────────────────────
    QLabel* tpThumb_[3] = {};

    // ── Controls ──────────────────────────────────────────────────────────────
    QComboBox*    tpResCombo_      = nullptr;
    QSpinBox*     tpSeamBandSpin_  = nullptr;
    QPushButton*  tpGenerateBtn_   = nullptr;
    QProgressBar* tpProgressBar_   = nullptr;
    QLabel*       tpProgressLabel_ = nullptr;

    // ── Output preview panels (0=Color, 1=Relief, 2=Normal, 3=Offset) ────────
    QLabel*      tpPreview_[4]     = {};
    QLabel*      tpInfoLabel_[4]   = {};
    QSpinBox*    tpMipSpin_[4]     = {};
    QPushButton* tpSaveBtn_[4]     = {};
    // Per-panel R/G/B/A preview toggles (panels 0-2 only; panel 3 has no toggles)
    QCheckBox*   tpChannelCheck_[3][4] = {};

    // ── State ─────────────────────────────────────────────────────────────────
    TexturePrepResult  tpResult_;
    QObject*           tpWorker_ = nullptr;
    QThread*           tpThread_ = nullptr;

    // Stored heightmap result (copy)
    HeightmapResult hmResult_;

    // Non-owned mesh pointer
    QEMSimplifier* simplifiedMesh_ = nullptr;
};
