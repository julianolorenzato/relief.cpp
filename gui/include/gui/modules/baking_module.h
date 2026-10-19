/**
 * @file baking_module.h
 * @brief Pipeline stage that shows the loaded mesh next to a 12-triangle box
 *        rendered with relief mapping from the baked textures.
 */
#pragma once
#include <QWidget>
#include <memory>
#include "relief/mesh.h"
#include "gui/module.h"
#include "gui/orbital3dview.h"
#include "gui/relief_view.h"

/**
 * @brief Module hosting two linked viewports: a common view of the original
 *        mesh, and a relief view of the 12-triangle bounding box textured with
 *        the baked maps (when they exist). The baking itself is triggered from
 *        the Textures module; this module refreshes on onMeshUpdated(). Each of the box's six faces shows its own tile (the
 *        baked UV map as RGB = (u, v, 0), plus its depth) of a 3x2 atlas.
 */
class BakingModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    /**
     * @brief Shows the freshly loaded original mesh, and its untextured
     *        bounding box (nothing is baked yet).
     * @param original   Loaded mesh, shown in the common view.
     * @param simplified Unused.
     */
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;

    /**
     * @brief Rebuilds the box from the original mesh's (possibly newly baked)
     *        planes maps, one tile per face.
     */
    void onMeshUpdated() override;

protected:
    /**
     * @brief Builds the common view and the relief view side by side.
     * @return Widget placed in the module's viewport area.
     */
    QWidget* buildContent() override;

    /**
     * @brief Builds the Bake button and the relief controls.
     * @return Widget placed in the module's side panel.
     */
    QWidget* buildControls() override;

private:
    Orbital3DView* orbitalWidget_ = nullptr;
    ReliefView* reliefWidget_ = nullptr;

    /// Box shown in the relief view; owns the textures it displays.
    std::unique_ptr<mesh::Mesh> boxMesh_;
};
