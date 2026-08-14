/**
 * @file relief_module.h
 * @brief Pipeline stage that previews the relief-mapped simplified mesh
 *        against the original, high-detail mesh side by side.
 */
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
#include "gui/texture_prep_module.h"
#include "relief/textures.h"

/// @brief Widget hosting the relief-mapped preview, a textured comparison of
///        the original mesh, and the relief-mapping controls (steps, depth
///        scale, atlas leaping, debug view).
class ReliefModule : public QWidget {
    Q_OBJECT

public:
    explicit ReliefModule(QWidget* parent = nullptr);

public slots:
    /// Stores the mesh pointers and marks them pending for sync.
    void setMeshes(QEMSimplifier* original, QEMSimplifier* simplified);
    /// Stores the texture-prep source and marks its maps pending for sync.
    void onTexturesReady(TexturePrepModule* source);
    /// Called when this tab is activated — flushes any pending data.
    void onActivated();

signals:
    void statusMessage(const QString& msg);

private:
    void buildUI();
    /// Pushes pending mesh/texture data into the viewports once both are available and the tab is visible.
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

    // Non-owned texture-prep source
    TexturePrepModule* texturePrepSource_ = nullptr;
};
