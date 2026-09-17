/**
 * @file mainwindow.h
 * @brief Top-level application window: hosts the toolbar that switches
 *        between pipeline stages and the stacked viewport for each module.
 */
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QString>
#include <QToolBar>
#include <QStackedWidget>
#include <utility>
#include <vector>
#include "gui/global_context.h"
#include "gui/module.h"
#include "gui/modules/simplifier_module.h"
#include "gui/modules/heightmap_module.h"
#include "gui/modules/texture_prep_module.h"
#include "gui/modules/relief_module.h"
#include "gui/modules/relief_sandbox_module.h"
#include "gui/modules/normal_map_module.h"
#include "gui/modules/validation_module.h"

/// @brief Main application window; wires the pipeline modules (simplifier,
///        heightmap baker, texture prep, relief viewer, relief sandbox) together
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

    QToolBar*       contextToolBar = nullptr;
    QStackedWidget* viewportStack  = nullptr;
    QLabel*         statusLabel    = nullptr;

    GlobalContext* globalContext = nullptr;

    /// Modules paired with their toolbar label, in toolbar/viewport-stack order.
    std::vector<std::pair<QString, Module*>> modules;
};
