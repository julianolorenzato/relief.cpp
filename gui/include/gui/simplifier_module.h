/**
 * @file simplifier_module.h
 * @brief Pipeline entry stage: loads a mesh, drives QEMSimplifier, and
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
#include "relief/qem.h"
#include "gui/orbital3dview.h"

/// @brief Widget that loads a mesh, runs QEMSimplifier with the configured
///        boundary/envelope options, and shows the original, simplified, and
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
    void modelLoaded(QEMSimplifier* original, QEMSimplifier* simplified);
    /// Emitted after a simplification run completes.
    void simplificationDone(QEMSimplifier* original, QEMSimplifier* simplified);
    void statusMessage(const QString& msg);

private slots:
    /// Runs QEMSimplifier on the original mesh with the current UI settings and refreshes the views.
    void onSimplify();
    /// Keeps the target-faces slider and spin box in sync.
    void onTargetFacesChanged(int value);
    /// Resets the camera on all three viewports.
    void onResetCameras();

private:
    void buildUI();
    /// Applies an inflate/deflate offset along cached per-group vertex normals to the simplified mesh preview.
    void applyInflate(double offset);
    /// Refreshes the face-count labels for original/simplified meshes.
    void updateStats();

    // ── Mesh data ─────────────────────────────────────────────────────────────
    std::unique_ptr<QEMSimplifier> originalMesh_;
    std::unique_ptr<QEMSimplifier> simplifiedMesh_;

    // ── Viewports ─────────────────────────────────────────────────────────────
    Orbital3DView* glWidgetOriginal_   = nullptr;
    Orbital3DView* glWidgetSimplified_ = nullptr;
    Orbital3DView* glWidgetOverlay_    = nullptr;

    // ── Simplification controls ───────────────────────────────────────────────
    QSlider*  simplificationSlider_  = nullptr;
    QSpinBox* targetFacesSpinBox_    = nullptr;

    QCheckBox* wireframeCheck_            = nullptr;
    QCheckBox* cullFaceCheck_             = nullptr;
    QCheckBox* texturedCheck_             = nullptr;
    QCheckBox* uvViewCheck_               = nullptr;
    QComboBox* boundaryModeCombo_         = nullptr;
    QCheckBox* envelopeConstraintCheck_   = nullptr;
    QCheckBox* useOptimalCandidateCheck_  = nullptr;
    QCheckBox* showBoundaryEdgesCheck_    = nullptr;
    QCheckBox* showInternalEdgesCheck_    = nullptr;

    QSlider*        inflateSlider_ = nullptr;
    QDoubleSpinBox* inflateSpin_   = nullptr;

    // ── Inflate state ─────────────────────────────────────────────────────────
    std::vector<Eigen::Vector3d> baseSimplifiedPositions_;
    std::vector<Eigen::Vector3d> simplifiedVertexNormals_;
    std::vector<int>             simplifiedVertexGroup_;
    int    simplifiedVertexGroupCount_ = 0;
    double inflateScale_               = 1.0;

    // ── Face counts ───────────────────────────────────────────────────────────
    int originalFaceCount_ = 0;
    int targetFaceCount_   = 0;
};
