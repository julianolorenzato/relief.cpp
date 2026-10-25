/**
 * @file old_module.h
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
#include "relief/mesh.h"
#include "gui/module.h"
#include "gui/views/mesh_view.h"
#include "gui/views/old_relief_view.h"
#include "relief/textures.h"

/// @brief Widget hosting the relief-mapped preview, a textured comparison of
///        the original mesh, and the relief-mapping controls (steps, depth
///        scale, atlas leaping, debug view).
class OldModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    /// Stores the mesh pointers and marks them pending for sync.
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;
    /// Called when the simplified mesh's data changes in place: the mesh pointers are
    /// unchanged, so re-mark them pending for sync.
    void onMeshUpdated() override;

private:
    QWidget* buildContent() override;
    QWidget* buildControls() override;
    /// Flushes any pending mesh/texture data once the tab becomes visible — the GL
    /// widgets below can only safely receive data once they have a valid context,
    /// which Qt only guarantees once they're actually shown.
    void showEvent(QShowEvent* event) override;
    /// Builds the "Lighting" group box (X/Y/Z sliders) and appends it to
    /// `outerControls`'s layout, driving the shared point light on all three viewports.
    void buildLightingGroup(QWidget* outerControls);
    /// Pushes pending mesh/texture data into the viewports once both are available and the tab is visible.
    void syncIfReady();

    // ── Viewports ─────────────────────────────────────────────────────────────
    OldReliefView*    reliefWidget_          = nullptr;
    MeshView* reliefCompareWidget_  = nullptr;  // mode: Textured
    MeshView* reliefOriginalWidget_ = nullptr;  // mode: Textured

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
    bool meshPending_ = false;
};
