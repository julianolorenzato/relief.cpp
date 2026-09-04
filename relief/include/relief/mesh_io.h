/**
 * @file mesh_io.h
 * @brief OBJ and glTF/GLB import/export for Mesh.
 */
#pragma once
#include <string>
#include "relief/mesh.h"

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
/// @brief Loads a mesh (and its embedded textures) from a glTF/GLB file.
/// @param mesh Mesh to load into (replaces its contents).
/// @param path Path to the .gltf/.glb file.
/// @return true on success.
bool loadGLTF(Mesh& mesh, const std::string& path);
/// @brief Saves the mesh to a glTF/GLB file.
/// @param mesh Mesh to save.
/// @param path Destination path.
/// @return true on success.
bool saveGLTF(const Mesh& mesh, const std::string& path);
