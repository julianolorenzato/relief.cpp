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

#include "relief/mesh/io.h"

namespace mesh {

Mesh::Mesh(const std::string &path) {
    if (!io::loadMesh(*this, path)) throw std::runtime_error("failed to load mesh: " + path);
    computeIslands();
}

void Mesh::computeIslands() { islands = detectIslands(); }

std::vector<int> Mesh::detectIslands() const {
    auto edgeToFaces = buildEdgeToFaces();
    int nf = (int)faces.size();
    std::vector<int> island(nf, -1);
    if (nf == 0) return island;

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
        const Face &f = faces[face];
        for (int k = 0; k < 3; k++) {
            if (wedges[f.w[k]].vertex == vertexId) return wedges[f.w[k]].uv;
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
        if (faces[fi].removed) continue;
        auto [it, inserted] = rootToId.emplace(find(fi), (int)rootToId.size());
        island[fi] = it->second;
    }
    return island;
}

Mesh::GPUMesh Mesh::explodeForGPU() const {
    GPUMesh out;
    std::vector<int> wedgeToIdx(wedges.size(), -1);
    out.indices.reserve(faces.size() * 3);
    for (const auto &f : faces) {
        if (f.removed) continue;
        for (int k = 0; k < 3; k++) {
            int wi = f.w[k];
            if (wedgeToIdx[wi] < 0) {
                wedgeToIdx[wi] = (int)out.positions.size();
                out.positions.push_back(vertices[wedges[wi].vertex].pos);
                out.uvs.push_back(wedges[wi].uv);
            }
            out.indices.push_back((uint32_t)wedgeToIdx[wi]);
        }
    }
    return out;
}

const Wedge &Mesh::faceWedge(const Face &f, int cornerIdx) const {
    return wedges[f.w[cornerIdx]];
}

const Vertex &Mesh::faceVertex(const Face &f, int cornerIdx) const {
    return vertices[faceWedge(f, cornerIdx).vertex];
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

int Mesh::faceCount() const {
    int n = 0;
    for (auto &f : faces)
        if (!f.removed) n++;
    return n;
}

int Mesh::vertexCount() const {
    int n = 0;
    for (auto &v : vertices)
        if (!v.removed) n++;
    return n;
}

std::map<Edge, std::vector<int>> Mesh::buildEdgeToFaces() const {
    std::map<Edge, std::vector<int>> edgeToFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = wedges[faces[fi].w[i]].vertex, b = wedges[faces[fi].w[(i + 1) % 3]].vertex;
            edgeToFaces[Edge(a, b)].push_back(fi);
        }
    }
    return edgeToFaces;
}

std::vector<std::set<int>> Mesh::buildVertexToVertices() const {
    std::vector<std::set<int>> vertexToVertices(vertices.size());
    for (const auto &entry : buildEdgeToFaces()) {
        const auto &edge = entry.first;
        vertexToVertices[edge.first].insert(edge.second);
        vertexToVertices[edge.second].insert(edge.first);
    }
    return vertexToVertices;
}

void Mesh::moveVertex(int index, const Eigen::Vector3d &pos) {
    if (!vertices[index].removed) vertices[index].pos = pos;
}

void Mesh::logSummary() const {
    std::cout << "Mesh: " << vertexCount() << " vertices, " << wedges.size() << " wedges, "
              << faceCount() << " faces\n";
    if (!textureData.empty())
        std::cout << "  color texture: " << textureWidth << "x" << textureHeight << "\n";
    if (!normalTextureData.empty())
        std::cout << "  normal texture: " << normalTextureWidth << "x" << normalTextureHeight
                  << "\n";
}

}  // namespace mesh
