/**
 * @file bounding_module.h
 * @brief Pipeline stage for bounding-volume operations: previews a mesh in an
 *        orbital 3D view and a relief view side by side.
 */
#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QQueue>
#include <QStackedWidget>
#include <memory>
#include "relief/mesh.h"
#include "relief/op.h"
#include "relief/op/bboxproj.h"
#include "relief/op/bvol.h"
#include "gui/module.h"
#include "gui/orbital3dview.h"
#include "gui/relief_view.h"

/// @brief One entry in BoundingModule's pipeline queue: the Op used to reach
///        this entry's mesh from the previous entry's (null for the seed
///        entry created by onMeshLoaded(), which just snapshots the freshly
///        loaded mesh), the resulting mesh snapshot, and a display label.
///        Holds shared_ptr rather than unique_ptr so BoundingStep stays
///        copyable, as required by QQueue (a QList) for its element type.
struct BoundingStep {
    std::shared_ptr<op::Op> op;        ///< Null for the seed entry.
    std::shared_ptr<mesh::Mesh> mesh;  ///< Mesh after applying `op`.
    QString label;                     ///< Text shown in the queue list.
};

/// @brief Widget hosting an orbital 3D view and a relief view side by side, plus
///        controls to list/add/pop bounding-volume pipeline steps. Independent
///        of sibling modules: it does not read data from any other module,
///        only from GlobalContext via the inherited Module mesh-loaded wiring.
class BoundingModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    /// Called when a new model is loaded: resets the pipeline queue and seeds
    /// it with the freshly loaded simplified mesh (no Op applied yet).
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;

private slots:
    /// Builds the Op selected by boundingOpCombo_ (parameterized by whichever
    /// controls are on the matching boundingParamsStack_ page), applies it to
    /// a copy of the mesh currently at the back of the queue, and enqueues
    /// the result.
    void onAddStep();
    /// Pops the step at the back of the queue, as long as more than one
    /// remains (the seed step from onMeshLoaded() is never popped).
    void onPopStep();

private:
    QWidget* buildContent() override;
    QWidget* buildControls() override;

    /// Builds the parameter panel for the "Bounding Volume" operation (a
    /// Type combo offering AABB/OBB) and appends it as a page of
    /// boundingParamsStack_.
    QWidget* buildBoundingVolumeParams();
    /// Builds the (empty) parameter panel for the "BBox Projection"
    /// operation, which takes no parameters, and appends it as a page of
    /// boundingParamsStack_.
    QWidget* buildBBoxProjectionParams();

    /// Repopulates the queue list widget and points both viewports at the
    /// mesh from the last (most recently enqueued) queue entry.
    void refreshQueue();

    // ── Viewports ─────────────────────────────────────────────────────────────
    Orbital3DView* boundingOrbitalWidget_ = nullptr;
    ReliefView*    boundingReliefWidget_  = nullptr;

    // ── Controls ──────────────────────────────────────────────────────────────
    /// View-feature toggles applied to both viewports (wireframe/backface
    /// cull) or Orbital3DView only (seam edges, which ReliefView has no
    /// equivalent slot for).
    QCheckBox*      boundingWireframeCheck_   = nullptr;
    QCheckBox*      boundingCullFaceCheck_    = nullptr;
    QCheckBox*      boundingSeamEdgesCheck_   = nullptr;
    QListWidget*    boundingQueueList_       = nullptr;
    /// Selects which Op kind onAddStep() builds; index matches the page of
    /// boundingParamsStack_ holding that operation's parameter controls.
    QComboBox*      boundingOpCombo_         = nullptr;
    /// Shows the parameter controls for whichever operation is currently
    /// selected in boundingOpCombo_ (one page per operation).
    QStackedWidget* boundingParamsStack_     = nullptr;
    QComboBox*      boundingVolumeTypeCombo_ = nullptr;
    QPushButton*    boundingAddStepBtn_      = nullptr;
    QPushButton*    boundingPopStepBtn_      = nullptr;

    // ── Pipeline queue ────────────────────────────────────────────────────────
    /// Steps in application order; boundingQueue_.last().mesh is the current
    /// state shown in the viewports.
    QQueue<BoundingStep> boundingQueue_;
};
