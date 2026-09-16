/**
 * @file global_context.h
 * @brief Shared owner of the currently loaded original mesh, so every
 *        pipeline module can consume it without any one module owning
 *        the load.
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

signals:
    /// Emitted after loadModel() succeeds, with the newly loaded mesh.
    void modelLoaded(mesh::Mesh* original);
    void statusMessage(const QString& msg);

private:
    std::unique_ptr<mesh::Mesh> originalMesh;
};
