/**
 * @file obj.h
 * @brief Wavefront OBJ import/export for Mesh.
 */
#pragma once
#include <string>
#include "relief/mesh.h"

namespace mesh::io::obj {

/// @brief Loads a mesh from a Wavefront OBJ file.
/// @param mesh Mesh to load into (replaces its contents).
/// @param path Path to the .obj file.
/// @return true on success.
bool loadOBJ(Mesh& mesh, const std::string& path);
/// @brief Saves the mesh to a Wavefront OBJ file.
/// @param mesh Mesh to save.
/// @param path Destination path.
/// @return true on success.
bool saveOBJ(const Mesh& mesh, const std::string& path);

} // namespace mesh::io::obj
