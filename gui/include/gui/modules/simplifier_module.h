/**
 * @file simplifier_module.h
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
#include "relief/simplification.h"
#include "relief/inflate.h"
#include "gui/orbital3dview.h"

class QPushButton;
class QLabel;

/// @brief Widget that loads a mesh, runs Simplifier with the configured
///        boundary options, and shows the original, simplified, and
///        overlay views alongside an inflate/deflate preview control.
class SimplifierModule : public Module {
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
    /// smooth, ...): recomputes the inflate baseline and redraws the simplified/overlay views.
    void onMeshUpdated() override;

private slots:
    /// Resets the simplified working mesh back to a full-resolution copy of the original mesh.
    void onReset();
    /// Runs Simplifier on the original mesh with the current UI settings and refreshes the views.
    void onSimplify();
    /// Runs Simplifier again directly on the (possibly inflated) simplified mesh, keeping its
    /// current vertex positions as the new base instead of resetting from the original mesh.
    void onSimplifyInflated();
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

private:
    void buildUI() override;
    /// Applies an inflate/deflate offset along cached per-group vertex normals to the simplified mesh preview.
    void applyInflate(double offset);
    /// @brief Recomputes the inflate baseline (base positions, per-group vertex normals) from
    ///        the current simplifiedMesh_ geometry and resets/enables the inflate controls.
    void captureInflateBaseline();

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

    QSlider*        inflateSlider = nullptr;
    QDoubleSpinBox* inflateSpin   = nullptr;
    QPushButton*    simplifyInflatedBtn = nullptr;

    // ── Smooth controls ───────────────────────────────────────────────────────
    QSpinBox*       smoothIterationsSpin = nullptr;
    QDoubleSpinBox* smoothStrengthSpin   = nullptr;
    QPushButton*    smoothBtn            = nullptr;

    // ── Feature edge lock (brush selection) controls ─────────────────────────
    QPushButton*    brushModeToggleBtn  = nullptr;
    QDoubleSpinBox* brushRadiusSpin     = nullptr;
    QDoubleSpinBox* brushAngleSpin      = nullptr;
    QComboBox*      brushPropagationCombo = nullptr;
    QPushButton*    clearSelectionBtn   = nullptr;
    QLabel*         selectedEdgeCountLabel = nullptr;

    // ── Inflate state ─────────────────────────────────────────────────────────
    inflate::Baseline inflateBaseline;
    double inflateScale = 1.0;

    // ── Face counts ───────────────────────────────────────────────────────────
    int    originalFaceCount     = 0;
    double simplificationPercent = 50.0;
};
