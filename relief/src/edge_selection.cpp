/**
 * @file edge_selection.cpp
 * @brief Implementation of brush-based edge selection: raycasting, face
 *        adjacency, and normal-gated flood fill.
 */
#include "relief/edge_selection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace edgesel {

namespace {

/// Vertex ids of face `fi`'s 3 corners, in order.
std::array<int, 3> faceVerts(const mesh::Mesh& mesh, int fi) {
    const mesh::Face& f = mesh.faces[fi];
    return {mesh.wedges[f.w[0]].vertex, mesh.wedges[f.w[1]].vertex, mesh.wedges[f.w[2]].vertex};
}

EdgeKey canon(int a, int b) {
    if (a > b) std::swap(a, b);
    return {a, b};
}

} // namespace

FaceAdjacency FaceAdjacency::build(const mesh::Mesh& mesh) {
    FaceAdjacency adj;
    adj.neighborFace.assign(mesh.faces.size(), {-1, -1, -1});

    auto edgeToFaces = mesh.buildEdgeToFaces();
    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        if (mesh.faces[fi].removed) continue;
        auto vs = faceVerts(mesh, fi);
        for (int k = 0; k < 3; k++) {
            auto key = canon(vs[k], vs[(k + 1) % 3]);
            const auto& faces = edgeToFaces[key];
            if (faces.size() != 2) continue; // boundary edge: no neighbor to cross
            int other = (faces[0] == fi) ? faces[1] : faces[0];
            adj.neighborFace[fi][k] = other;
        }
    }
    return adj;
}

std::vector<Eigen::Vector3d> computeFaceNormals(const mesh::Mesh& mesh) {
    std::vector<Eigen::Vector3d> normals(mesh.faces.size(), Eigen::Vector3d::Zero());
    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        if (mesh.faces[fi].removed) continue;
        auto vs = faceVerts(mesh, fi);
        const Eigen::Vector3d& p0 = mesh.vertices[vs[0]].pos;
        const Eigen::Vector3d& p1 = mesh.vertices[vs[1]].pos;
        const Eigen::Vector3d& p2 = mesh.vertices[vs[2]].pos;
        Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
        double len = n.norm();
        normals[fi] = (len > 1e-12) ? Eigen::Vector3d(n / len) : Eigen::Vector3d::Zero();
    }
    return normals;
}

RayHit raycastMesh(const mesh::Mesh& mesh, const Eigen::Vector3d& origin, const Eigen::Vector3d& dir) {
    RayHit best;
    double bestT = std::numeric_limits<double>::infinity();
    constexpr double kEps = 1e-9;

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        if (mesh.faces[fi].removed) continue;
        auto vs = faceVerts(mesh, fi);
        const Eigen::Vector3d& p0 = mesh.vertices[vs[0]].pos;
        const Eigen::Vector3d& p1 = mesh.vertices[vs[1]].pos;
        const Eigen::Vector3d& p2 = mesh.vertices[vs[2]].pos;

        // Möller–Trumbore
        Eigen::Vector3d e1 = p1 - p0, e2 = p2 - p0;
        Eigen::Vector3d pvec = dir.cross(e2);
        double det = e1.dot(pvec);
        if (std::abs(det) < kEps) continue;
        double invDet = 1.0 / det;

        Eigen::Vector3d tvec = origin - p0;
        double u = tvec.dot(pvec) * invDet;
        if (u < 0.0 || u > 1.0) continue;

        Eigen::Vector3d qvec = tvec.cross(e1);
        double v = dir.dot(qvec) * invDet;
        if (v < 0.0 || u + v > 1.0) continue;

        double t = e2.dot(qvec) * invDet;
        if (t < kEps || t >= bestT) continue;

        bestT = t;
        best.found = true;
        best.faceIdx = fi;
        best.t = t;
        best.point = origin + dir * t;
    }
    return best;
}

bool touchesEdge(const mesh::Mesh& mesh, int v0, int v1, const Eigen::Vector3d& center, double radius) {
    const Eigen::Vector3d& a = mesh.vertices[v0].pos;
    const Eigen::Vector3d& b = mesh.vertices[v1].pos;
    Eigen::Vector3d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = (len2 > 1e-12) ? (center - a).dot(ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    Eigen::Vector3d closest = a + t * ab;
    return (closest - center).norm() <= radius;
}

void BrushSelection::applyBrush(const mesh::Mesh& mesh, const FaceAdjacency& adj,
                                 const std::vector<Eigen::Vector3d>& faceNormals,
                                 int seedFace, const Eigen::Vector3d& hitPoint,
                                 double radius, double angleThresholdRad,
                                 PropagationMode mode, bool erase) {
    if (seedFace < 0 || seedFace >= (int)mesh.faces.size()) return;
    if (mesh.faces[seedFace].removed) return;

    // Generous slack so flood fill can "reach across" a triangle whose edges
    // sit outside `radius` while still being a legitimate propagation step,
    // without running away across a huge coplanar region far from the brush.
    const double reachRadius = radius * 1.5;

    std::vector<bool> visited(mesh.faces.size(), false);
    std::queue<int> queue;
    visited[seedFace] = true;
    queue.push(seedFace);

    while (!queue.empty()) {
        int fi = queue.front();
        queue.pop();

        auto vs = faceVerts(mesh, fi);
        for (int k = 0; k < 3; k++) {
            int v0 = vs[k], v1 = vs[(k + 1) % 3];
            if (touchesEdge(mesh, v0, v1, hitPoint, radius)) {
                auto key = canon(v0, v1);
                if (erase) edges_.erase(key); else edges_.insert(key);
            }
        }

        const Eigen::Vector3d& compareNormal = faceNormals[(mode == PropagationMode::AnchoredToSeed) ? seedFace : fi];
        for (int k = 0; k < 3; k++) {
            int nb = adj.neighborFace[fi][k];
            if (nb < 0 || visited[nb]) continue;

            double dot = std::clamp(compareNormal.dot(faceNormals[nb]), -1.0, 1.0);
            double angle = std::acos(dot);
            if (angle > angleThresholdRad) continue;

            auto nbVerts = faceVerts(mesh, nb);
            bool inReach = false;
            for (int vi : nbVerts) {
                if ((mesh.vertices[vi].pos - hitPoint).norm() <= reachRadius) { inReach = true; break; }
            }
            if (!inReach) continue;

            visited[nb] = true;
            queue.push(nb);
        }
    }
}

} // namespace edgesel
