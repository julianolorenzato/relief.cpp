/**
 * @file mainwindow.h
 * @brief Top-level application window: hosts the toolbar that switches
 *        between pipeline stages and the stacked viewport for each module.
 */
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QToolBar>
#include <QStackedWidget>
#include "gui/simplifier_module.h"
#include "gui/heightmap_module.h"
#include "gui/texture_prep_module.h"
#include "gui/relief_module.h"
#include "gui/relief_test_module.h"

/// @brief Main application window; wires the pipeline modules (simplifier,
///        heightmap baker, texture prep, relief viewer, relief test) together
///        via a context toolbar and a QStackedWidget.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    /// Opens a file dialog to load a mesh (OBJ/GLTF) into the simplifier module.
    void onLoadModel();
    /// Opens a file dialog to save the current simplified mesh.
    void onSaveSimplified();

private:
    void setupUI();
    void createMenuBar();
    /// Switches the viewport stack (and toolbar checked state) to the module at `index`.
    void switchContext(int index);

    QToolBar*       contextToolBar = nullptr;
    QStackedWidget* viewportStack  = nullptr;
    QLabel*         statusLabel    = nullptr;

    SimplifierModule*   simplifier_   = nullptr;
    HeightmapModule*    heightmap_    = nullptr;
    TexturePrepModule*  texturePrep_  = nullptr;
    ReliefModule*       relief_       = nullptr;
    ReliefTestModule*   reliefTest_   = nullptr;
};
