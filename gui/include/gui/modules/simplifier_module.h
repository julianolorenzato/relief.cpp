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
#include "relief/mesh.h"
#include "relief/mesh/io.h"
#include "relief/simplification.h"
#include "gui/orbital3dview.h"

class QPushButton;
class QLabel;

/// @brief Widget that loads a mesh, runs Simplifier with the configured
///        boundary options, and shows the original, simplified, and
///        overlay views alongside an inflate/deflate preview control.
class SimplifierModule : public QWidget {
    Q_OBJECT

public:
    explicit SimplifierModule(QWidget* parent = nullptr);

    /// @brief Loads a mesh file (OBJ or GLTF) as the working original mesh.
    /// @param path Path to the mesh file.
    /// @return true on success.
    bool loadModel(const QString& path);
    /// @brief Saves the current simplified mesh to a file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveSimplified(const QString& path);

signals:
    /// Emitted after loadModel() succeeds, with pointers to the (yet unsimplified) meshes.
    void modelLoaded(mesh::Mesh* original, mesh::Mesh* simplified);
    /// Emitted after a simplification run completes.
    void simplificationDone(mesh::Mesh* original, mesh::Mesh* simplified);
    void statusMessage(const QString& msg);

private slots:
    /// Runs Simplifier on the original mesh with the current UI settings and refreshes the views.
    void onSimplify();
    /// Runs Simplifier again directly on the (possibly inflated) simplified mesh, keeping its
    /// current vertex positions as the new base instead of resetting from the original mesh.
    void onSimplifyInflated();
    /// Keeps the target-faces slider and spin box in sync.
    void onTargetFacesChanged(int value);
    /// Resets the camera on all three viewports.
    void onResetCameras();
    /// Updates the "Locked edges: N" label when the brush selection changes.
    void onSelectionChanged(int count);

private:
    void buildUI();
    /// Applies an inflate/deflate offset along cached per-group vertex normals to the simplified mesh preview.
    void applyInflate(double offset);
    /// @brief Recomputes the inflate baseline (base positions, per-group vertex normals) from
    ///        the current simplifiedMesh_ geometry and resets/enables the inflate controls.
    void captureInflateBaseline();
    /// Refreshes the face-count labels for original/simplified meshes.
    void updateStats();

    // ── Mesh data ─────────────────────────────────────────────────────────────
    std::unique_ptr<mesh::Mesh> originalMesh;
    std::unique_ptr<mesh::Mesh> simplifiedMesh;

    // ── Viewports ─────────────────────────────────────────────────────────────
    Orbital3DView* glWidgetOriginal   = nullptr;
    Orbital3DView* glWidgetSimplified = nullptr;
    Orbital3DView* glWidgetOverlay    = nullptr;

    // ── Simplification controls ───────────────────────────────────────────────
    QSlider*  simplificationSlider  = nullptr;
    QSpinBox* targetFacesSpinBox    = nullptr;

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

    // ── Feature edge lock (brush selection) controls ─────────────────────────
    QPushButton*    brushModeToggleBtn  = nullptr;
    QDoubleSpinBox* brushRadiusSpin     = nullptr;
    QDoubleSpinBox* brushAngleSpin      = nullptr;
    QComboBox*      brushPropagationCombo = nullptr;
    QPushButton*    clearSelectionBtn   = nullptr;
    QLabel*         selectedEdgeCountLabel = nullptr;

    // ── Inflate state ─────────────────────────────────────────────────────────
    std::vector<Eigen::Vector3d> baseSimplifiedPositions;
    std::vector<Eigen::Vector3d> simplifiedVertexNormals;
    std::vector<int>             simplifiedVertexGroup;
    int    simplifiedVertexGroupCount = 0;
    double inflateScale               = 1.0;

    // ── Face counts ───────────────────────────────────────────────────────────
    int originalFaceCount = 0;
    int targetFaceCount   = 0;
};
