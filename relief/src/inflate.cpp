/**
 * @file inflate.cpp
 * @brief Inflate/deflate implementation: see inflate.h.
 */
#include "relief/inflate.h"

#include <cmath>
#include <map>
#include <tuple>

namespace inflate {

Baseline computeBaseline(const mesh::Mesh& mesh) {
    Baseline baseline;
    baseline.basePositions.resize(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++)
        baseline.basePositions[i] = mesh.vertices[i].pos;

    baseline.vertexNormals.assign(mesh.vertices.size(), Eigen::Vector3d::Zero());
    for (const auto& f : mesh.faces) {
        if (f.removed) continue;
        int v0 = mesh.wedges[f.w[0]].vertex;
        int v1 = mesh.wedges[f.w[1]].vertex;
        int v2 = mesh.wedges[f.w[2]].vertex;
        const auto& p0 = baseline.basePositions[v0];
        const auto& p1 = baseline.basePositions[v1];
        const auto& p2 = baseline.basePositions[v2];
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        baseline.vertexNormals[v0] += n;
        baseline.vertexNormals[v1] += n;
        baseline.vertexNormals[v2] += n;
    }

    // Vertices duplicated at the same 3D position (e.g. UV seams, split into
    // distinct vertices at load time) must inflate together. Otherwise each
    // copy uses only its own incident faces, the normals diverge, and the
    // seam opens a hole when inflating even with seam vertices locked.
    {
        Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
        Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
        for (const auto& p : baseline.basePositions) {
            bmin = bmin.cwiseMin(p);
            bmax = bmax.cwiseMax(p);
        }
        double cell = std::max((bmax - bmin).norm() * 1e-7, 1e-9);

        auto quantize = [cell](const Eigen::Vector3d& p) {
            return std::make_tuple((long long)std::llround(p.x() / cell),
                                    (long long)std::llround(p.y() / cell),
                                    (long long)std::llround(p.z() / cell));
        };

        std::map<std::tuple<long long, long long, long long>, int> groupId;
        std::vector<int> vertexGroup(mesh.vertices.size(), -1);
        for (size_t i = 0; i < mesh.vertices.size(); i++) {
            if (mesh.vertices[i].removed) continue;
            auto key = quantize(baseline.basePositions[i]);
            auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
            vertexGroup[i] = it->second;
        }

        std::vector<Eigen::Vector3d> groupNormal(groupId.size(), Eigen::Vector3d::Zero());
        for (size_t i = 0; i < mesh.vertices.size(); i++) {
            if (mesh.vertices[i].removed) continue;
            groupNormal[vertexGroup[i]] += baseline.vertexNormals[i];
        }
        for (size_t i = 0; i < mesh.vertices.size(); i++) {
            if (mesh.vertices[i].removed) continue;
            baseline.vertexNormals[i] = groupNormal[vertexGroup[i]];
        }
    }

    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        double len = baseline.vertexNormals[i].norm();
        if (len > 1e-10) baseline.vertexNormals[i] /= len;
    }

    return baseline;
}

void apply(mesh::Mesh& mesh, const Baseline& baseline, double offset) {
    if (baseline.basePositions.empty()) return;
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        if (!mesh.vertices[i].removed)
            mesh.vertices[i].pos = baseline.basePositions[i] + offset * baseline.vertexNormals[i];
    }
}

void applyOffset(mesh::Mesh& mesh, double offset) {
    apply(mesh, computeBaseline(mesh), offset);
}

} // namespace inflate
