/**
 * @file baking_module.h
 * @brief Pipeline stage for baking operations. Currently an empty shell with
 *        access to the loaded mesh.
 */
#pragma once
#include <QWidget>
#include "gui/module.h"

/**
 * @brief Module reserved for baking features. Content and controls are empty
 *        for now. The loaded meshes are available through the inherited
 *        Module::originalMesh_ and Module::simplifiedMesh_ pointers, which
 *        Module::onMeshLoaded() fills in whenever a model is loaded.
 */
class BakingModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

protected:
    /**
     * @brief Builds the (currently empty) main content area.
     * @return Empty widget placed in the module's viewport area.
     */
    QWidget* buildContent() override;

    /**
     * @brief Builds the (currently empty) controls panel.
     * @return Empty widget placed in the module's side panel.
     */
    QWidget* buildControls() override;
};
