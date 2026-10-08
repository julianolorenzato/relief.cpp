/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge-to-faces adjacency.
 */
#include "relief/mesh.h"

#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>

#include "mesh/io.h"

namespace mesh {

Mesh::Mesh(const std::string &path) {
    if (!io::loadMesh(*this, path)) throw std::runtime_error("failed to load mesh: " + path);
}

bool Mesh::save(const std::string &path) const { return io::saveMesh(*this, path); }

void Mesh::replaceGeometry(std::vector<Vertex> newVertices, std::vector<Wedge> newWedges,
                           std::vector<Face> newFaces) {
    vertices_ = std::move(newVertices);
    wedges_ = std::move(newWedges);
    faces_ = std::move(newFaces);
    computeIslands();
}

int Mesh::faceCount() const {
    int n = 0;
    for (auto &f : faces_)
        if (!f.removed) n++;
    return n;
}

int Mesh::vertexCount() const {
    int n = 0;
    for (auto &v : vertices_)
        if (!v.removed) n++;
    return n;
}

const Wedge &Mesh::faceWedge(const Face &f, int cornerIdx) const {
    return wedges_[f.w[cornerIdx]];
}

const Vertex &Mesh::faceVertex(const Face &f, int cornerIdx) const {
    return vertices_[faceWedge(f, cornerIdx).vertex];
}

Eigen::Vector3d Mesh::faceNormal(const Face &f) const {
    const Eigen::Vector3d &p0 = faceVertex(f, 0).pos;
    const Eigen::Vector3d &p1 = faceVertex(f, 1).pos;
    const Eigen::Vector3d &p2 = faceVertex(f, 2).pos;
    Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
    double len = n.norm();
    if (len == 0.0) return Eigen::Vector3d::Zero();
    return n / len;
}

std::array<Edge, 3> Mesh::faceEdges(const Face &f) const {
    int v0 = wedges_[f.w[0]].vertex, v1 = wedges_[f.w[1]].vertex, v2 = wedges_[f.w[2]].vertex;
    return {Edge(v0, v1), Edge(v1, v2), Edge(v2, v0)};
}

std::map<Edge, std::vector<int>> Mesh::buildEdgeToFaces() const {
    std::map<Edge, std::vector<int>> edgeToFaces;
    for (int fi = 0; fi < (int)faces_.size(); fi++) {
        if (faces_[fi].removed) continue;
        for (const Edge &e : faceEdges(faces_[fi])) edgeToFaces[e].push_back(fi);
    }
    return edgeToFaces;
}

std::vector<std::set<int>> Mesh::buildVertexToVertices() const {
    std::vector<std::set<int>> vertexToVertices(vertices_.size());
    for (const auto &entry : buildEdgeToFaces()) {
        const auto &edge = entry.first;
        vertexToVertices[edge.first].insert(edge.second);
        vertexToVertices[edge.second].insert(edge.first);
    }
    return vertexToVertices;
}

void Mesh::addQuadric(int index, const Eigen::Matrix4d &Q) { vertices_[index].Q += Q; }

void Mesh::clearQuadrics() {
    for (auto &v : vertices_) v.Q.setZero();
}

void Mesh::mergeVertices(
    int keep, int remove, const Eigen::Vector3d &pos,
    const std::vector<std::tuple<int, int, Eigen::Vector2d>> &uvTargets) {
    Vertex &kv = vertices_[keep];
    Vertex &rv = vertices_[remove];

    kv.pos = pos;
    kv.Q += rv.Q;

    // Each uvTarget pairing collapses onto one surviving wedge (wKeep): if
    // wRemove stayed a separate (but now identical-valued) wedge, faces on
    // either side of the old edge would explode into two distinct GPU
    // vertices at the same spot (explodeForGPU dedups by wedge id, not
    // value), each only accumulating its own half of the normal -- faceting
    // the shading right along every collapsed edge. So faces still pointing
    // at wRemove are repointed at wKeep below, folded into the face pass
    // already needed for degeneracy checking.
    std::map<int, int> wedgeRemap;  // wRemove -> wKeep
    for (auto &[wKeep, wRemove, mergedUV] : uvTargets) {
        wedges_[wKeep].uv = mergedUV;
        wedgeRemap[wRemove] = wKeep;
    }

    // Any other wedge still belonging to `remove` (parts of its fan that
    // don't touch this edge) simply moves to `keep`, UV unchanged.
    for (auto &wg : wedges_)
        if (wg.vertex == remove) wg.vertex = keep;

    rv.removed = true;

    // Repoint faces off any merged-away wedge, and mark now-degenerate faces
    // (two corners collapsed onto the same vertex) removed.
    for (auto &fc : faces_) {
        if (fc.removed) continue;
        if (!wedgeRemap.empty())
            for (int i = 0; i < 3; i++) {
                auto it = wedgeRemap.find(fc.w[i]);
                if (it != wedgeRemap.end()) fc.w[i] = it->second;
            }
        int a = wedges_[fc.w[0]].vertex;
        int b = wedges_[fc.w[1]].vertex;
        int c = wedges_[fc.w[2]].vertex;
        if (a == b || b == c || a == c) fc.removed = true;
    }
}

void Mesh::moveVertex(int index, const Eigen::Vector3d &pos) {
    if (!vertices_[index].removed) vertices_[index].pos = pos;
}

Mesh::GPUMesh Mesh::explodeForGPU() const {
    GPUMesh out;
    std::vector<int> wedgeToIdx(wedges_.size(), -1);
    out.indices.reserve(faces_.size() * 3);
    for (const auto &f : faces_) {
        if (f.removed) continue;
        for (int k = 0; k < 3; k++) {
            int wi = f.w[k];
            if (wedgeToIdx[wi] < 0) {
                wedgeToIdx[wi] = (int)out.positions.size();
                out.positions.push_back(vertices_[wedges_[wi].vertex].pos);
                out.uvs.push_back(wedges_[wi].uv);
            }
            out.indices.push_back((uint32_t)wedgeToIdx[wi]);
        }
    }
    return out;
}

void Mesh::logSummary() const {
    std::cout << "Mesh: " << vertexCount() << " vertices, " << wedges_.size() << " wedges, "
              << faceCount() << " faces\n";
    if (!colorTexture_.data.empty())
        std::cout << "  color texture: " << colorTexture_.width << "x" << colorTexture_.height
                  << "\n";
    if (!normalTexture_.data.empty())
        std::cout << "  normal texture: " << normalTexture_.width << "x" << normalTexture_.height
                  << "\n";
}

void Mesh::computeIslands() {
    auto edgeToFaces = buildEdgeToFaces();
    int nf = (int)faces_.size();
    for (auto &f : faces_) f.island = -1;
    if (nf == 0) return;

    std::vector<int> parent(nf);
    for (int i = 0; i < nf; i++) parent[i] = i;
    std::function<int(int)> find = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    // UV at the corner of `face` whose vertex is `vertexId`.
    auto vertexUV = [&](int face, int vertexId) {
        const Face &f = faces_[face];
        for (int k = 0; k < 3; k++) {
            if (wedges_[f.w[k]].vertex == vertexId) return wedges_[f.w[k]].uv;
        }
        return Eigen::Vector2d::Zero().eval();
    };

    constexpr double kUVEps2 = 1e-10;
    for (const auto &[key, faceIds] : edgeToFaces) {
        if (faceIds.size() != 2) continue;  // boundary or non-manifold edge: no weld across it
        int f0 = faceIds[0], f1 = faceIds[1];
        bool uvMatch =
            (vertexUV(f0, key.first) - vertexUV(f1, key.first)).squaredNorm() < kUVEps2 &&
            (vertexUV(f0, key.second) - vertexUV(f1, key.second)).squaredNorm() < kUVEps2;
        if (uvMatch) {
            int a = find(f0), b = find(f1);
            if (a != b) parent[a] = b;
        }
    }

    std::map<int, int> rootToId;
    for (int fi = 0; fi < nf; fi++) {
        if (faces_[fi].removed) continue;
        auto [it, inserted] = rootToId.emplace(find(fi), (int)rootToId.size());
        faces_[fi].island = it->second;
    }
}

}  // namespace mesh
