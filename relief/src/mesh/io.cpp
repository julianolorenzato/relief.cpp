/**
 * @file io.cpp
 * @brief Generic mesh import/export, dispatching by file extension.
 */
#include "relief/mesh/io.h"
#include "relief/mesh/io/obj.h"
#include "relief/mesh/io/gltf.h"
#include <algorithm>
#include <cctype>

namespace mesh::io {

/// @return true if `path` ends with `suffix` (case-insensitive).
static bool endsWithCI(const std::string &path, const std::string &suffix)
{
    if (path.size() < suffix.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), path.rbegin(),
                       [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
}

bool loadMesh(Mesh &mesh, const std::string &path)
{
    return endsWithCI(path, ".obj") ? obj::loadOBJ(mesh, path) : gltf::loadGLTF(mesh, path);
}

bool saveMesh(const Mesh &mesh, const std::string &path)
{
    return endsWithCI(path, ".obj") ? obj::saveOBJ(mesh, path) : gltf::saveGLTF(mesh, path);
}

} // namespace mesh::io
