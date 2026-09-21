#include "relief/op/bvol.h"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <thread>

namespace op::bvol {

namespace {

/// Axis index (0=X, 1=Y, 2=Z) of BoundingBox::faces[i].
constexpr int kFaceAxis[6] = {0, 0, 1, 1, 2, 2};

/// Outward sign (-1 or +1 along kFaceAxis[i]) of BoundingBox::faces[i].
constexpr double kFaceSign[6] = {-1, +1, -1, +1, -1, +1};

/// @return Number of non-removed vertices in `mesh`.
int countVisibleVertices(const mesh::Mesh& mesh) {
    int count = 0;
    for (const auto& v : mesh.vertices) {
        if (!v.removed) count++;
    }
    return count;
}

/// @return Twice the signed area of (a, b, c); positive iff CCW.
double edgeSide(const Eigen::Vector2d& a, const Eigen::Vector2d& b, const Eigen::Vector2d& c) {
    Eigen::Vector2d ab = b - a;
    Eigen::Vector2d ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

/// @return Whether `p` falls inside `tri` (winding-agnostic: all 3 edge
///         sides must agree in sign, or be zero).
bool pointInTriangle(const Eigen::Vector2d& p, const std::array<Eigen::Vector2d, 3>& tri) {
    double d0 = edgeSide(tri[0], tri[1], p);
    double d1 = edgeSide(tri[1], tri[2], p);
    double d2 = edgeSide(tri[2], tri[0], p);
    bool hasNeg = d0 < 0.0 || d1 < 0.0 || d2 < 0.0;
    bool hasPos = d0 > 0.0 || d1 > 0.0 || d2 > 0.0;
    return !(hasNeg && hasPos);
}

}  // namespace

void BoundingVolumeOp::apply(mesh::Mesh& mesh) const {
    if (mesh.vertices.empty()) return;
    switch (this->type) {
        case BoundingVolumeType::AABB:
            std::cout << "BoundingVolumeOp: computing AABB over " << mesh.faceCount() << " faces, "
                      << mesh.vertexCount() << " vertices\n";
            applyAABB(mesh);
            break;
        case BoundingVolumeType::OBB:
            std::cout << "BoundingVolumeOp: computing OBB over " << mesh.faceCount() << " faces, "
                      << mesh.vertexCount() << " vertices\n";
            applyOBB(mesh);
            break;
    }
    mesh.logSummary();
}

void BoundingVolumeOp::applyAABB(mesh::Mesh& mesh) const {
    if (countVisibleVertices(mesh) == 0) return;

    BoundingBox box;
    box.axes = {Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY(), Eigen::Vector3d::UnitZ()};
    computeBoxExtents(mesh, box.axes, box.center, box.halfExtents);
    projectMeshOntoBoxFaces(mesh, box);
    flattenBoxFaces(box, mesh);
}

void BoundingVolumeOp::applyOBB(mesh::Mesh& mesh) const {
    if (countVisibleVertices(mesh) == 0) return;

    BoundingBox box;
    box.axes = computeOBBAxes(mesh);
    computeBoxExtents(mesh, box.axes, box.center, box.halfExtents);
    projectMeshOntoBoxFaces(mesh, box);
    flattenBoxFaces(box, mesh);
}

std::array<Eigen::Vector3d, 3> BoundingVolumeOp::computeOBBAxes(const mesh::Mesh& mesh) {
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    int count = 0;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        mean += v.pos;
        count++;
    }
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
    std::array<Eigen::Vector3d, 3> axes = {evecs.col(0).normalized(), evecs.col(1).normalized(),
                                           evecs.col(2).normalized()};
    if (axes[0].cross(axes[1]).dot(axes[2]) < 0.0) axes[2] = -axes[2];
    return axes;
}

void BoundingVolumeOp::computeBoxExtents(const mesh::Mesh& mesh,
                                         const std::array<Eigen::Vector3d, 3>& axes,
                                         Eigen::Vector3d& center, Eigen::Vector3d& halfExtents) {
    Eigen::Vector3d bmin = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d bmax = Eigen::Vector3d::Constant(-1e18);
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        Eigen::Vector3d local(v.pos.dot(axes[0]), v.pos.dot(axes[1]), v.pos.dot(axes[2]));
        bmin = bmin.cwiseMin(local);
        bmax = bmax.cwiseMax(local);
    }

    Eigen::Vector3d localCenter = 0.5 * (bmin + bmax);
    halfExtents = 0.5 * (bmax - bmin);
    center = localCenter.x() * axes[0] + localCenter.y() * axes[1] + localCenter.z() * axes[2];
}

void BoundingVolumeOp::projectMeshOntoBoxFaces(const mesh::Mesh& mesh, BoundingBox& box) {
    // A mesh triangle projected onto one box face's plane, not yet resolved
    // against occluders.
    struct FaceCandidate {
        std::array<Eigen::Vector2d, 3> local;  ///< Projected local-plane coordinates.
        std::array<Eigen::Vector2d, 3> uv;     ///< Original mesh texture UVs.
        double depth;  ///< Nearest of the 3 vertices' distance to the face, along its outward axis.
    };

    // Gather every outward-facing mesh face's projection onto each box
    // face it faces (a face can face more than one, e.g. towards a
    // corner, so it's duplicated as a candidate on each).
    std::array<std::vector<FaceCandidate>, 6> candidatesPerFace;
    for (const auto& f : mesh.faces) {
        if (f.removed) continue;
        Eigen::Vector3d p[3] = {mesh.vertices[mesh.wedges[f.w[0]].vertex].pos,
                                mesh.vertices[mesh.wedges[f.w[1]].vertex].pos,
                                mesh.vertices[mesh.wedges[f.w[2]].vertex].pos};
        Eigen::Vector2d texUV[3] = {mesh.wedges[f.w[0]].uv, mesh.wedges[f.w[1]].uv,
                                    mesh.wedges[f.w[2]].uv};
        Eigen::Vector3d normal = (p[1] - p[0]).cross(p[2] - p[0]);
        if (normal.squaredNorm() == 0.0) continue;
        normal.normalize();

        for (int face = 0; face < 6; face++) {
            int axis = kFaceAxis[face];
            Eigen::Vector3d outward = kFaceSign[face] * box.axes[axis];
            if (normal.dot(outward) <= 0.0) continue;

            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            FaceCandidate candidate;
            candidate.depth = -1e18;
            for (int i = 0; i < 3; i++) {
                Eigen::Vector3d d = p[i] - box.center;
                candidate.local[i] = Eigen::Vector2d(d.dot(box.axes[u]), d.dot(box.axes[v]));
                candidate.uv[i] = texUV[i];
                candidate.depth = std::max(candidate.depth, p[i].dot(outward));
            }
            candidatesPerFace[face].push_back(candidate);
        }
    }

    // Resolve occlusion per box face: process candidates front-to-back
    // (nearest first); a candidate is discarded whole if its centroid
    // falls inside any nearer candidate's footprint, and kept whole
    // otherwise (no clipping/re-triangulation -- partial occlusion isn't
    // represented, a triangle is either fully visible or fully dropped).
    // The 6 faces are independent (disjoint candidatesPerFace/box.faces
    // slots), so they're resolved concurrently, one thread per face.
    std::mutex logMutex;
    auto resolveFace = [&](int face) {
        std::vector<FaceCandidate>& candidates = candidatesPerFace[face];
        std::sort(candidates.begin(), candidates.end(),
                  [](const FaceCandidate& a, const FaceCandidate& b) { return a.depth > b.depth; });
        {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << "  face " << face << ": " << candidates.size() << " candidate triangles\n";
        }

        BoundingBoxFace& patch = box.faces[face];
        std::vector<std::array<Eigen::Vector2d, 3>> occluders;
        for (const auto& candidate : candidates) {
            const std::array<Eigen::Vector2d, 3>& tri = candidate.local;
            Eigen::Vector2d centroid = (tri[0] + tri[1] + tri[2]) / 3.0;

            bool occluded = false;
            for (const auto& occluder : occluders) {
                if (pointInTriangle(centroid, occluder)) {
                    occluded = true;
                    break;
                }
            }
            if (occluded) continue;

            int base = (int)patch.vertices.size();
            for (int i = 0; i < 3; i++) {
                patch.vertices.push_back(tri[i]);
                patch.uvs.push_back(candidate.uv[i]);
            }
            patch.triangles.push_back({{base, base + 1, base + 2}});

            occluders.push_back(tri);
        }

        if (patch.triangles.empty()) {
            int axis = kFaceAxis[face];
            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            double hu = box.halfExtents[u];
            double hv = box.halfExtents[v];

            // No mesh geometry ever faced this direction (e.g. a flat/open source
            // mesh). Fill with a flat quad spanning the full face rectangle, with
            // a synthetic unit-square UV (no source triangle to inherit UVs from),
            // so the box stays closed.
            patch.vertices = {Eigen::Vector2d(-hu, -hv), Eigen::Vector2d(hu, -hv),
                              Eigen::Vector2d(hu, hv), Eigen::Vector2d(-hu, hv)};
            patch.uvs = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0), Eigen::Vector2d(1, 1),
                         Eigen::Vector2d(0, 1)};
            if (kFaceSign[face] > 0) {
                patch.triangles.push_back({{0, 1, 2}});
                patch.triangles.push_back({{0, 2, 3}});
            } else {
                patch.triangles.push_back({{0, 2, 1}});
                patch.triangles.push_back({{0, 3, 2}});
            }
        }
        {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << "  face " << face << ": " << patch.triangles.size()
                      << " visible triangles after occlusion\n";
        }
    };

    std::vector<std::thread> pool;
    for (int face = 0; face < 6; face++) pool.emplace_back(resolveFace, face);
    for (auto& th : pool) th.join();
}

void BoundingVolumeOp::flattenBoxFaces(const BoundingBox& box, mesh::Mesh& mesh) {
    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();
    for (int face = 0; face < 6; face++) {
        int axis = kFaceAxis[face];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d faceOrigin =
            box.center + kFaceSign[face] * box.halfExtents[axis] * box.axes[axis];

        const BoundingBoxFace& patch = box.faces[face];
        std::vector<int> wedgeOf(patch.vertices.size());
        for (size_t i = 0; i < patch.vertices.size(); i++) {
            const Eigen::Vector2d& local = patch.vertices[i];
            Eigen::Vector3d pos = faceOrigin + local.x() * box.axes[u] + local.y() * box.axes[v];

            mesh::Vertex vertex;
            vertex.pos = pos;
            int vertexIdx = (int)mesh.vertices.size();
            mesh.vertices.push_back(vertex);

            mesh::Wedge wedge;
            wedge.vertex = vertexIdx;
            wedge.uv = patch.uvs[i];
            wedgeOf[i] = (int)mesh.wedges.size();
            mesh.wedges.push_back(wedge);
        }
        for (const BoundingBoxTriangle& tri : patch.triangles) {
            mesh::Face face3;
            face3.w[0] = wedgeOf[tri.v[0]];
            face3.w[1] = wedgeOf[tri.v[1]];
            face3.w[2] = wedgeOf[tri.v[2]];
            mesh.faces.push_back(face3);
        }
    }
}

}  // namespace op::bvol
