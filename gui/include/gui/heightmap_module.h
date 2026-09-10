#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include "relief/heightmap.h"
#include "relief/mesh.h"

class HeightmapModule : public QWidget {
    Q_OBJECT

public:
    explicit HeightmapModule(QWidget* parent = nullptr);

public slots:
    // Called when a new model is loaded: stores pointers and resets state.
    void onModelLoaded(mesh::Mesh* original, mesh::Mesh* simplified);
    // Called when the mesh is updated after simplification: just stores ptrs.
    void onMeshUpdated(mesh::Mesh* original, mesh::Mesh* simplified);

signals:
    void bakeReady(const heightmap::HeightmapResult& result);
    void statusMessage(const QString& msg);

private slots:
    void onBake();
    void onSaveHeightmap();
    void onBakeProgress(int overall, const QString& text);
    void onBakeDone();

private:
    void buildUI();
    void launchBake();
    void displayHeightmap(const heightmap::HeightmapResult& r);
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
    heightmap::HeightmapResult  hmResult_;
    QObject*         hmWorker_ = nullptr;
    QThread*         hmThread_ = nullptr;

    // Non-owned mesh pointers (set by onModelLoaded / onMeshUpdated)
    mesh::Mesh* originalMesh_   = nullptr;
    mesh::Mesh* simplifiedMesh_ = nullptr;
};
