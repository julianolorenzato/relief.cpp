#include "relief/op/inflation.h"

#include <cmath>
#include <map>
#include <tuple>

namespace op::inflation {

void InflateOp::apply(mesh::Mesh& mesh) const {
    std::vector<Eigen::Vector3d> normals = computeVertexNormals(mesh);
    for (size_t i = 0; i < mesh.vertices.size(); i++)
        mesh.moveVertex((int)i, mesh.vertices[i].pos + offset_ * normals[i]);
}

std::vector<Eigen::Vector3d> InflateOp::computeVertexNormals(const mesh::Mesh& mesh) {
    std::vector<Eigen::Vector3d> normals(mesh.vertices.size(), Eigen::Vector3d::Zero());
    for (const auto& f : mesh.faces) {
        if (f.removed) continue;
        int v0 = mesh.wedges[f.w[0]].vertex;
        int v1 = mesh.wedges[f.w[1]].vertex;
        int v2 = mesh.wedges[f.w[2]].vertex;
        const auto& p0 = mesh.vertices[v0].pos;
        const auto& p1 = mesh.vertices[v1].pos;
        const auto& p2 = mesh.vertices[v2].pos;
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        normals[v0] += n;
        normals[v1] += n;
        normals[v2] += n;
    }

    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
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
        auto key = quantize(mesh.vertices[i].pos);
        auto [it, inserted] = groupId.try_emplace(key, (int)groupId.size());
        vertexGroup[i] = it->second;
    }

    std::vector<Eigen::Vector3d> groupNormal(groupId.size(), Eigen::Vector3d::Zero());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        if (mesh.vertices[i].removed) continue;
        groupNormal[vertexGroup[i]] += normals[i];
    }
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        if (mesh.vertices[i].removed) continue;
        normals[i] = groupNormal[vertexGroup[i]];
    }

    for (auto& n : normals) {
        double len = n.norm();
        if (len > 1e-10) n /= len;
    }
    return normals;
}

}  // namespace op::inflation
