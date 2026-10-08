/**
 * @file load_summary.cpp
 * @brief Diagnostic tool: loads a mesh and prints Mesh::logSummary(). Meant
 *        to be run before/after changes to the loading/simplification
 *        algorithms to eyeball what the vertex/wedge/face counts do.
 */
#include "relief/mesh.h"
#include <iostream>
#include <string>

using namespace mesh;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: load_summary <file.obj|.gltf|.glb>\n";
        return 1;
    }
    std::string path = argv[1];

    try
    {
        Mesh mesh(path);
        mesh.logSummary();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
