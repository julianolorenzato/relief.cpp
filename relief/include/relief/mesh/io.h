/**
 * @file io.h
 * @brief Generic mesh import/export, dispatching by file extension.
 */
#pragma once
#include <string>
#include "relief/mesh.h"

namespace mesh::io {

/// @brief Loads a mesh from a file, dispatching on extension (.obj vs .gltf/.glb).
/// @param mesh Mesh to load into (replaces its contents).
/// @param path Path to the mesh file.
/// @return true on success.
bool loadMesh(Mesh& mesh, const std::string& path);
/// @brief Saves a mesh to a file, dispatching on extension (.obj vs .gltf/.glb).
/// @param mesh Mesh to save.
/// @param path Destination path.
/// @return true on success.
bool saveMesh(const Mesh& mesh, const std::string& path);

} // namespace mesh::io
