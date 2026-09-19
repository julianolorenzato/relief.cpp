#include "relief/op/smoothing.h"

namespace op::smoothing {

void SmoothOp::apply(mesh::Mesh& mesh) const {
    if (iterations_ <= 0 || mesh.vertices.empty()) return;

    auto neighbors = mesh.buildVertexToVertices();

    std::vector<Eigen::Vector3d> newPos(mesh.vertices.size());
    for (int it = 0; it < iterations_; it++) {
        for (size_t i = 0; i < mesh.vertices.size(); i++) {
            if (mesh.vertices[i].removed || neighbors[i].empty()) {
                newPos[i] = mesh.vertices[i].pos;
                continue;
            }
            Eigen::Vector3d avg = Eigen::Vector3d::Zero();
            for (int n : neighbors[i]) avg += mesh.vertices[n].pos;
            avg /= (double)neighbors[i].size();
            newPos[i] = mesh.vertices[i].pos + lambda_ * (avg - mesh.vertices[i].pos);
        }
        for (size_t i = 0; i < mesh.vertices.size(); i++)
            mesh.moveVertex((int)i, newPos[i]);
    }
}

} // namespace op::smoothing
