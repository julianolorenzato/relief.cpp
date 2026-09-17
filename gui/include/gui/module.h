/**
 * @file module.h
 * @brief Base class for pipeline module widgets: standardizes the connection
 *        to the shared GlobalContext's meshLoaded signal.
 */
#pragma once
#include <QWidget>
#include "gui/global_context.h"
#include "relief/mesh.h"

/// @brief Base widget for pipeline modules. Connects onMeshLoaded() and
///        onMeshUpdated() to `context`'s meshLoaded/notifyMeshUpdate signals so every
///        module can react to a newly loaded model or a mutated mesh without any
///        per-module wiring in MainWindow.
class Module : public QWidget {
    Q_OBJECT

public:
    explicit Module(GlobalContext* context, QWidget* parent = nullptr);

    /// Builds this module's UI. Called once, right after construction, by
    /// createModule() below — not from Module's own constructor, since a
    /// virtual call there can't dispatch to the subclass override yet.
    virtual void buildUI() = 0;

signals:
    void statusMessage(const QString& msg);
    /// Emitted by a module after it mutates the simplified mesh in place (pointer
    /// unchanged). Module's constructor wires this straight into
    /// GlobalContext::onMeshUpdateNotified(), which then broadcasts notifyMeshUpdate() back
    /// out to every module's onMeshUpdated().
    void notifyMeshUpdate();

public slots:
    /// Called when GlobalContext loads a new model, with the original mesh
    /// and its freshly reset simplified working copy. Stores both in
    /// originalMesh_/simplifiedMesh_; override in modules that need extra
    /// setup on load, calling Module::onMeshLoaded() first.
    virtual void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) {
        originalMesh_   = original;
        simplifiedMesh_ = simplified;
    }
    /// Called when GlobalContext::notifyMeshUpdate() fires: the simplified mesh's
    /// data changed in place (pointer unchanged). Default is a no-op; override in
    /// modules that cache derived state (GPU buffers, thumbnails, ...) from it.
    virtual void onMeshUpdated() {}

protected:
    // Non-owned mesh pointers, kept in sync with GlobalContext by onMeshLoaded().
    mesh::Mesh* originalMesh_   = nullptr;
    mesh::Mesh* simplifiedMesh_ = nullptr;
};

/// Constructs a Module subclass and then calls its buildUI() override. Needed because
/// Module's own constructor can't dispatch a virtual call to the derived override — the
/// derived object isn't fully constructed yet at that point.
template <class T>
T* createModule(GlobalContext* context, QWidget* parent = nullptr) {
    T* module = new T(context, parent);
    static_cast<Module*>(module)->buildUI();
    return module;
}
