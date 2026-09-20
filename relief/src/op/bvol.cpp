#include "relief/op/bvol.h"

#include <Eigen/Eigenvalues>

namespace op::bvol {

void BoundingVolumeOp::buildBoxMesh(mesh::Mesh& mesh,
                                     const Eigen::Vector3d& center,
                                     const std::array<Eigen::Vector3d, 3>& axes,
                                     const Eigen::Vector3d& halfExtents) {
    static const double signs[8][3] = {
        {-1,-1,-1}, {+1,-1,-1}, {+1,+1,-1}, {-1,+1,-1},
        {-1,-1,+1}, {+1,-1,+1}, {+1,+1,+1}, {-1,+1,+1},
    };
    static const int tris[12][3] = {
        {0,3,2}, {0,2,1},   // bottom  (z = -hz)
        {4,5,6}, {4,6,7},   // top     (z = +hz)
        {0,1,5}, {0,5,4},   // front   (y = -hy)
        {3,7,6}, {3,6,2},   // back    (y = +hy)
        {0,4,7}, {0,7,3},   // left    (x = -hx)
        {1,2,6}, {1,6,5},   // right   (x = +hx)
    };

    mesh.vertices.assign(8, mesh::Vertex{});
    mesh.wedges.assign(8, mesh::Wedge{});
    for (int i = 0; i < 8; i++) {
        mesh.vertices[i].pos = center
            + signs[i][0] * halfExtents.x() * axes[0]
            + signs[i][1] * halfExtents.y() * axes[1]
            + signs[i][2] * halfExtents.z() * axes[2];
        mesh.wedges[i].vertex = i;
        mesh.wedges[i].uv = Eigen::Vector2d::Zero();
    }

    mesh.faces.resize(12);
    for (int f = 0; f < 12; f++)
        for (int c = 0; c < 3; c++)
            mesh.faces[f].w[c] = tris[f][c];

    mesh.textureData.clear();
    mesh.textureWidth = 0;
    mesh.textureHeight = 0;
    mesh.normalTextureData.clear();
    mesh.normalTextureWidth = 0;
    mesh.normalTextureHeight = 0;
}

void BoundingVolumeOp::apply(mesh::Mesh& mesh) const {
    if (mesh.vertices.empty()) return;
    switch (type_) {
        case BoundingVolumeType::AABB: applyAABB(mesh); break;
        case BoundingVolumeType::OBB:  applyOBB(mesh);  break;
    }
}

void BoundingVolumeOp::applyAABB(mesh::Mesh& mesh) const {
    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    int count = 0;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
        count++;
    }
    if (count == 0) return;

    Eigen::Vector3d center = 0.5 * (bmin + bmax);
    Eigen::Vector3d halfExtents = 0.5 * (bmax - bmin);
    std::array<Eigen::Vector3d, 3> axes = {
        Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY(), Eigen::Vector3d::UnitZ()
    };
    buildBoxMesh(mesh, center, axes, halfExtents);
}

void BoundingVolumeOp::applyOBB(mesh::Mesh& mesh) const {
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    int count = 0;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        mean += v.pos;
        count++;
    }
    if (count == 0) return;
    mean /= (double)count;

    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        Eigen::Vector3d d = v.pos - mean;
        cov += d * d.transpose();
    }
    cov /= (double)count;

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(cov);
    Eigen::Matrix3d evecs = solver.eigenvectors();
    std::array<Eigen::Vector3d, 3> axes = {
        evecs.col(0).normalized(), evecs.col(1).normalized(), evecs.col(2).normalized()
    };
    if (axes[0].cross(axes[1]).dot(axes[2]) < 0.0) axes[2] = -axes[2];

    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        Eigen::Vector3d d = v.pos - mean;
        Eigen::Vector3d local(d.dot(axes[0]), d.dot(axes[1]), d.dot(axes[2]));
        bmin = bmin.cwiseMin(local);
        bmax = bmax.cwiseMax(local);
    }

    Eigen::Vector3d localCenter = 0.5 * (bmin + bmax);
    Eigen::Vector3d halfExtents = 0.5 * (bmax - bmin);
    Eigen::Vector3d worldCenter = mean
        + localCenter.x() * axes[0] + localCenter.y() * axes[1] + localCenter.z() * axes[2];

    buildBoxMesh(mesh, worldCenter, axes, halfExtents);
}

} // namespace op::bvol
