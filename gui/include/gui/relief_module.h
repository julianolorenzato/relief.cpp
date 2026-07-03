#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include "relief/qem.h"
#include "gui/orbital3dview.h"
#include "gui/relief_view.h"
#include "relief/textures.h"

class ReliefModule : public QWidget {
    Q_OBJECT

public:
    explicit ReliefModule(QWidget* parent = nullptr);

public slots:
    // Store mesh pointers and mark them pending for sync.
    void setMeshes(QEMSimplifier* original, QEMSimplifier* simplified);
    // Store the texture prep result and mark it pending for sync.
    // void onTexturesReady(const TexturePrepResult& result); // disabled: TexturePrepResult removed
    // Called when this tab is activated — flushes any pending data.
    void onActivated();

signals:
    void statusMessage(const QString& msg);

private:
    void buildUI();
    void syncIfReady();

    // ── Viewports ─────────────────────────────────────────────────────────────
    ReliefView*    reliefWidget_          = nullptr;
    Orbital3DView* reliefCompareWidget_  = nullptr;  // mode: Textured
    Orbital3DView* reliefOriginalWidget_ = nullptr;  // mode: Textured

    // ── Controls ──────────────────────────────────────────────────────────────
    QCheckBox*      reliefEnabledCheck_       = nullptr;
    QSpinBox*       reliefStepsSpin_          = nullptr;
    QDoubleSpinBox* reliefDepthScaleSpin_     = nullptr;
    QCheckBox*      reliefUseAtlasCheck_          = nullptr;
    QComboBox*      reliefTextureTypeCombo_    = nullptr;
    QComboBox*      reliefDebugViewCombo_         = nullptr;
    QCheckBox*      reliefWireframeCheck_     = nullptr;
    QCheckBox*      reliefCullFaceCheck_      = nullptr;
    QPushButton*    reliefResetCamBtn_        = nullptr;

    // ── Pending state ─────────────────────────────────────────────────────────
    bool meshPending_     = false;
    bool texturesPending_ = false;

    // Non-owned mesh pointers
    QEMSimplifier* originalMesh_   = nullptr;
    QEMSimplifier* simplifiedMesh_ = nullptr;

    // TexturePrepResult tpResult_; // disabled: TexturePrepResult removed
};
