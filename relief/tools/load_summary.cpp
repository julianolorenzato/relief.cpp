/**
 * @file load_summary.cpp
 * @brief Diagnostic tool: loads a mesh and prints Mesh::logSummary(). Meant
 *        to be run before/after changes to the loading/simplification
 *        algorithms to eyeball what the vertex/wedge/face counts do.
 */
#include "relief/mesh.h"
#include "relief/mesh/io.h"
#include <iostream>
#include <string>

using namespace mesh;
using namespace mesh::io;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: load_summary <file.obj|.gltf|.glb>\n";
        return 1;
    }
    std::string path = argv[1];

    Mesh mesh;
    if (!loadMesh(mesh, path))
    {
        std::cerr << "failed to load " << path << "\n";
        return 1;
    }

    mesh.logSummary();
    return 0;
}
