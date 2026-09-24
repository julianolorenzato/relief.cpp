#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QThread>
#include "gui/module.h"
#include "relief/heightmap.h"
#include "relief/mesh.h"

class HeightmapModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    // Called when a new model is loaded: stores pointers and resets state.
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;

signals:
    void bakeReady(const heightmap::HeightmapResult& result);

private slots:
    void onBake();
    void onSaveHeightmap();
    void onBakeProgress(int overall, const QString& text);
    void onBakeDone();

private:
    QWidget* buildContent() override;
    QWidget* buildControls() override;
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
};
