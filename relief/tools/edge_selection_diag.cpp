/**
 * @file edge_selection_diag.cpp
 * @brief Diagnostic tool: loads a mesh, raycasts straight down its bounding
 *        box's top-center, and applies one brush touch at the hit point.
 *        Prints the resulting locked-edge count so the core brush-selection
 *        algorithm (raycastMesh/FaceAdjacency/BrushSelection) can be sanity
 *        checked independently of the GUI.
 */
#include "relief/mesh.h"
#include "relief/mesh/io.h"
#include "relief/mesh/edgesel.h"
#include <iostream>
#include <string>

using namespace mesh;
using namespace mesh::io;
using namespace mesh::edgesel;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: edge_selection_diag <file.obj|.gltf|.glb> [radius] [angleDeg]\n";
        return 1;
    }
    std::string path = argv[1];
    double radius = (argc > 2) ? std::stod(argv[2]) : 0.1;
    double angleDeg = (argc > 3) ? std::stod(argv[3]) : 35.0;

    Mesh mesh;
    if (!loadMesh(mesh, path))
    {
        std::cerr << "failed to load " << path << "\n";
        return 1;
    }
    mesh.logSummary();

    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    for (const auto &v : mesh.vertices)
    {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
    }
    Eigen::Vector3d center = (bmin + bmax) * 0.5;
    double radiusScale = (bmax - bmin).norm() * 0.5;
    if (radiusScale < 1e-9) radiusScale = 1.0;

    Eigen::Vector3d origin(center.x(), bmax.y() + radiusScale, center.z());
    Eigen::Vector3d dir(0.0, -1.0, 0.0);

    RayHit hit = raycastMesh(mesh, origin, dir);
    if (!hit.found)
    {
        std::cerr << "no hit from top-center ray\n";
        return 1;
    }
    std::cout << "hit face " << hit.faceIdx << " at (" << hit.point.x() << ", " << hit.point.y()
              << ", " << hit.point.z() << ")\n";

    FaceAdjacency adj = FaceAdjacency::build(mesh);
    std::vector<Eigen::Vector3d> normals = computeFaceNormals(mesh);

    BrushSelection selection;
    selection.applyBrush(mesh, adj, normals, hit.faceIdx, hit.point,
                          radius * radiusScale, angleDeg * 3.14159265358979323846 / 180.0,
                          PropagationMode::Chained);

    std::cout << "selected " << selection.size() << " edges\n";
    int shown = 0;
    for (const auto &e : selection.edges())
    {
        std::cout << "  (" << e.first << ", " << e.second << ")\n";
        if (++shown >= 10) break;
    }
    return 0;
}
