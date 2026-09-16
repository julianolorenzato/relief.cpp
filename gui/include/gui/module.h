/**
 * @file module.h
 * @brief Base class for pipeline module widgets: standardizes the connection
 *        to the shared GlobalContext's modelLoaded signal.
 */
#pragma once
#include <QWidget>
#include "gui/global_context.h"
#include "relief/mesh.h"

/// @brief Base widget for pipeline modules. Connects onModelLoaded() to
///        `context`'s modelLoaded signal so every module can react to a
///        newly loaded model without any per-module wiring in MainWindow.
class Module : public QWidget {
    Q_OBJECT

public:
    explicit Module(GlobalContext* context, QWidget* parent = nullptr);

public slots:
    /// Called when GlobalContext loads a new original mesh. Default is a
    /// no-op; override in modules that consume the raw original mesh
    /// directly.
    virtual void onModelLoaded(mesh::Mesh*) {}
};
