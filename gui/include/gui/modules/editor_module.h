/**
 * @file editor_module.h
 * @brief Pipeline entry stage: loads a mesh, drives Simplifier, and
 *        previews original/simplified/overlay side by side.
 */
#pragma once
#include <QWidget>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <memory>
#include <vector>
#include "gui/module.h"
#include "relief/mesh.h"
#include "relief/mesh/io.h"
#include "relief/op/simplification.h"
#include "relief/op/inflation.h"
#include "relief/op/smoothing.h"
#include "relief/op/bvol.h"
#include "gui/orbital3dview.h"

class QPushButton;
class QLabel;

/// @brief Widget that loads a mesh, runs Simplifier with the configured
///        boundary options, and shows the original, simplified, and
///        overlay views alongside an inflate/deflate control.
class EditorModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

    /// @brief Saves the current simplified mesh to a file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveSimplified(const QString& path);

public slots:
    /// Receives the original mesh and its simplified working copy from the shared
    /// GlobalContext, and refreshes the views/UI.
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;
    /// Called whenever the simplified mesh's data changes in place (reset, re-simplify,
    /// smooth, ...): redraws the simplified/overlay views.
    void onMeshUpdated() override;

private slots:
    /// Resets the simplified working mesh back to a full-resolution copy of the original mesh.
    void onReset();
    /// Runs Simplifier directly on the simplified mesh's current vertex positions with the
    /// current UI settings and refreshes the views.
    void onSimplify();
    /// Called when the reduction-percentage slider moves: updates simplificationPercent
    /// and the "Reduction: N%" label.
    void onReductionPercentageChanged(int sliderValue);
    /// Resets the camera on all three viewports.
    void onResetCameras();
    /// Updates the "Locked edges: N" label when the brush selection changes.
    void onSelectionChanged(int count);
    /// Applies Laplacian smoothing to the simplified mesh using the current
    /// iterations/strength controls.
    void onSmooth();
    /// Runs op::inflation::InflateOp with the current offset field's value on the
    /// simplified mesh.
    void onApplyInflate();
    /// Runs op::bvol::BoundingVolumeOp with the currently selected
    /// volume type (AABB/OBB) on the simplified mesh, replacing it with a box.
    void onApplyBoundingVolume();

private:
    void buildUI() override;

    // ── Viewports ─────────────────────────────────────────────────────────────
    Orbital3DView* glWidgetOriginal   = nullptr;
    Orbital3DView* glWidgetSimplified = nullptr;
    Orbital3DView* glWidgetOverlay    = nullptr;

    // ── Simplification controls ───────────────────────────────────────────────
    QSlider* simplificationSlider      = nullptr;
    QLabel*  simplificationPercentLabel = nullptr;

    QCheckBox* wireframeCheck            = nullptr;
    QCheckBox* cullFaceCheck             = nullptr;
    QCheckBox* texturedCheck             = nullptr;
    QCheckBox* uvViewCheck               = nullptr;
    QComboBox* boundaryModeCombo         = nullptr;
    QCheckBox* useOptimalCandidateCheck  = nullptr;
    QCheckBox* showInternalEdgesCheck    = nullptr;
    QCheckBox* showSeamEdgesCheck        = nullptr;

    QDoubleSpinBox* inflateSpin     = nullptr;
    QPushButton*    applyInflateBtn = nullptr;

    // ── Smooth controls ───────────────────────────────────────────────────────
    QSpinBox*       smoothIterationsSpin = nullptr;
    QDoubleSpinBox* smoothStrengthSpin   = nullptr;
    QPushButton*    smoothBtn            = nullptr;

    // ── Bounding volume controls ─────────────────────────────────────────────
    QComboBox*   boundingVolumeTypeCombo = nullptr;
    QPushButton* applyBoundingVolumeBtn  = nullptr;

    // ── Feature edge lock (brush selection) controls ─────────────────────────
    QPushButton*    brushModeToggleBtn  = nullptr;
    QDoubleSpinBox* brushRadiusSpin     = nullptr;
    QDoubleSpinBox* brushAngleSpin      = nullptr;
    QComboBox*      brushPropagationCombo = nullptr;
    QPushButton*    clearSelectionBtn   = nullptr;
    QLabel*         selectedEdgeCountLabel = nullptr;

    // ── Face counts ───────────────────────────────────────────────────────────
    int    originalFaceCount     = 0;
    double simplificationPercent = 50.0;

    // ── Ops ───────────────────────────────────────────────────────────────────
    op::simplification::SimplifyOp   simplifyOp{4};
    op::smoothing::SmoothOp          smoothOp;
    op::inflation::InflateOp         inflateOp{0.0};
    op::bvol::BoundingVolumeOp boundingVolumeOp;
};
