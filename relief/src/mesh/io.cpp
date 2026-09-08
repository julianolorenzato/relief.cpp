/**
 * @file io.cpp
 * @brief Generic mesh import/export, dispatching by file extension.
 */
#include "relief/mesh/io.h"
#include "relief/mesh/io/obj.h"
#include "relief/mesh/io/gltf.h"
#include <algorithm>
#include <cctype>

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
    return endsWithCI(path, ".obj") ? loadOBJ(mesh, path) : loadGLTF(mesh, path);
}

bool saveMesh(const Mesh &mesh, const std::string &path)
{
    return endsWithCI(path, ".obj") ? saveOBJ(mesh, path) : saveGLTF(mesh, path);
}
