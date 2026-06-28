#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include "relief/heightmap.h"
#include "relief/qem.h"

class HeightmapModule : public QWidget {
    Q_OBJECT

public:
    explicit HeightmapModule(QWidget* parent = nullptr);

public slots:
    // Called when a new model is loaded: stores pointers and resets state.
    void onModelLoaded(QEMSimplifier* original, QEMSimplifier* simplified);
    // Called when the mesh is updated after simplification: just stores ptrs.
    void onMeshUpdated(QEMSimplifier* original, QEMSimplifier* simplified);

signals:
    void bakeReady(const HeightmapResult& result);
    void statusMessage(const QString& msg);

private slots:
    void onBake();
    void onSaveHeightmap();
    void onBakeProgress(int overall, const QString& text);
    void onBakeDone();

private:
    void buildUI();
    void launchBake();
    void displayHeightmap(const HeightmapResult& r);
    void setBakeButtonsEnabled(bool enabled);
    void reset();

    // ── Viewport (preview) ────────────────────────────────────────────────────
    QLabel*      hmPreview_   = nullptr;
    QLabel*      hmInfoLabel_ = nullptr;
    QPushButton* hmSaveBtn_   = nullptr;

    // ── Controls ──────────────────────────────────────────────────────────────
    QComboBox*    hmResCombo_      = nullptr;
    QPushButton*  hmBakeBtn_       = nullptr;
    QProgressBar* hmProgressBar_   = nullptr;
    QLabel*       hmProgressLabel_ = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    HeightmapResult  hmResult_;
    QObject*         hmWorker_ = nullptr;
    QThread*         hmThread_ = nullptr;

    // Non-owned mesh pointers (set by onModelLoaded / onMeshUpdated)
    QEMSimplifier* originalMesh_   = nullptr;
    QEMSimplifier* simplifiedMesh_ = nullptr;
};
