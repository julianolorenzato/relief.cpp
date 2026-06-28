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

class SimplifierModule : public QWidget {
    Q_OBJECT

public:
    explicit SimplifierModule(QWidget* parent = nullptr);

    // Load a mesh file (OBJ or GLTF). Returns true on success.
    bool loadModel(const QString& path);
    // Save the simplified mesh to a file. Returns true on success.
    bool saveSimplified(const QString& path);

signals:
    void modelLoaded(QEMSimplifier* original, QEMSimplifier* simplified);
    void simplificationDone(QEMSimplifier* original, QEMSimplifier* simplified);
    void statusMessage(const QString& msg);

private slots:
    void onSimplify();
    void onTargetFacesChanged(int value);
    void onResetCameras();

private:
    void buildUI();
    void applyInflate(double offset);
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
