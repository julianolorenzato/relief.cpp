#pragma once
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QImage>
#include <memory>
#include "relief/qem.h"
#include "relief/textures.h"
#include "gui/relief_view.h"

// Standalone context for testing relief mapping in isolation.
// Load a mesh and three textures (color, depth, normal) via file dialogs,
// bake synchronously, and inspect the result in a single ReliefView.
class ReliefTestModule : public QWidget
{
    Q_OBJECT

public:
    explicit ReliefTestModule(QWidget *parent = nullptr);

private slots:
    void onLoadMesh();
    void onLoadColor();
    void onLoadDepth();
    void onLoadNormal();
    void onBake();

private:
    QWidget *buildControls();
    void setThumb(QLabel *label, const QImage &img);

    // ── Viewport ──────────────────────────────────────────────────────────────
    ReliefView *reliefView = nullptr;

    // ── Load controls ─────────────────────────────────────────────────────────
    QPushButton *loadMeshBtn = nullptr;
    QLabel *meshStatusLbl = nullptr;
    QPushButton *loadColorBtn = nullptr;
    QPushButton *loadDepthBtn = nullptr;
    QPushButton *loadNormalBtn = nullptr;
    QLabel *thumbColor = nullptr;
    QLabel *thumbDepth = nullptr;
    QLabel *thumbNormal = nullptr;

    // ── Relief controls ───────────────────────────────────────────────────────
    QSpinBox *stepsSpin = nullptr;
    QDoubleSpinBox *depthScaleSpin = nullptr;
    QCheckBox *useAtlasCheck = nullptr;
    QComboBox *textureTypeCombo = nullptr;
    QComboBox *debugViewCombo = nullptr;
    QCheckBox *wireframeCheck = nullptr;
    QCheckBox *cullFaceCheck = nullptr;
    QPushButton *resetCamBtn = nullptr;
    QPushButton *renderBtn   = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    std::unique_ptr<QEMSimplifier> mesh;
    QImage colorImg, depthImg, normalImg;
    TexturePrepResult tpResult;
};
