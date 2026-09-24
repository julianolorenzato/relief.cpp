/**
 * @file bounding_module.h
 * @brief Pipeline stage for bounding-volume operations: previews a mesh in an
 *        orbital 3D view and a relief view side by side.
 */
#pragma once
#include <QWidget>
#include <QQueue>
#include <QPushButton>
#include <utility>
#include "relief/mesh.h"
#include "gui/module.h"
#include "gui/orbital3dview.h"
#include "gui/relief_view.h"

/// @brief Placeholder for a bounding-volume processing step. Parameters and
///        behavior are decided later; for now it only exists so BoundingModule's
///        queue has a concrete pair type to hold.
struct BoundingStep {};

/// @brief Widget hosting an orbital 3D view and a relief view side by side, plus
///        an (currently empty) controls pane. Independent of sibling modules: it
///        does not read data from any other module, only from GlobalContext via
///        the inherited Module mesh-loaded wiring.
class BoundingModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

private:
    void buildUI() override;

    // ── Viewports ─────────────────────────────────────────────────────────────
    Orbital3DView* boundingOrbitalWidget_ = nullptr;
    ReliefView*    boundingReliefWidget_  = nullptr;

    // ── Controls ──────────────────────────────────────────────────────────────
    QPushButton* boundingPlaceholderBtn_ = nullptr;

    // ── Pipeline queue ────────────────────────────────────────────────────────
    /// Queue of mesh/step pairs awaiting processing. Not yet consumed anywhere;
    /// filled in once BoundingStep grows real parameters and processing logic.
    QQueue<std::pair<mesh::Mesh*, BoundingStep>> boundingQueue_;
};
