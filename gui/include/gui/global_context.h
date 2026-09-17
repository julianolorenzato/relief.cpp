/**
 * @file global_context.h
 * @brief Shared owner of the currently loaded original mesh and its
 *        simplified working copy, so every pipeline module can consume
 *        them without any one module owning the load.
 */
#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include "relief/mesh.h"
#include "relief/mesh/io.h"

/// @brief Loads a mesh file from disk and broadcasts it to interested modules.
class GlobalContext : public QObject {
    Q_OBJECT

public:
    explicit GlobalContext(QObject* parent = nullptr);

    /// @brief Loads a mesh file (OBJ or GLTF) as the current original mesh.
    /// @param path Path to the mesh file.
    /// @return true on success.
    bool loadModel(const QString& path);

    /// @brief Broadcasts notifyMeshUpdate() to every module. Call after mutating the
    ///        simplified mesh in place (e.g. after a re-simplify), so modules that
    ///        cache derived state (GPU buffers, thumbnails, ...) know to refresh it.
    void onMeshUpdateNotified();

signals:
    /// Emitted after loadModel() succeeds, with the newly loaded original mesh
    /// and a freshly reset simplified working copy of it.
    void meshLoaded(mesh::Mesh* original, mesh::Mesh* simplified);
    /// Emitted via onMeshUpdateNotified() whenever the simplified mesh's data changes
    /// without its pointer changing.
    void notifyMeshUpdate();

private:
    std::unique_ptr<mesh::Mesh> originalMesh_;
    std::unique_ptr<mesh::Mesh> simplifiedMesh_;
};
