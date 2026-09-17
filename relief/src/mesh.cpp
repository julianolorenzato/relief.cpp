/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge-to-faces adjacency.
 */
#include "relief/mesh.h"

#include <iostream>
#include <map>
#include <utility>

namespace mesh {

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

EdgeToFaces Mesh::buildEdgeToFaces() const {
    EdgeToFaces edgeToFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = wedges[faces[fi].w[i]].vertex, b = wedges[faces[fi].w[(i + 1) % 3]].vertex;
            if (a > b) std::swap(a, b);
            edgeToFaces[{a, b}].push_back(fi);
        }
    }
    return edgeToFaces;
}

void Mesh::smooth(int iterations, double lambda) {
    if (iterations <= 0 || vertices.empty()) return;

    std::vector<std::vector<int>> neighbors(vertices.size());
    for (const auto &entry : buildEdgeToFaces()) {
        int a = entry.first.first, b = entry.first.second;
        neighbors[a].push_back(b);
        neighbors[b].push_back(a);
    }

    std::vector<Eigen::Vector3d> newPos(vertices.size());
    for (int it = 0; it < iterations; it++) {
        for (size_t i = 0; i < vertices.size(); i++) {
            if (vertices[i].removed || neighbors[i].empty()) {
                newPos[i] = vertices[i].pos;
                continue;
            }
            Eigen::Vector3d avg = Eigen::Vector3d::Zero();
            for (int n : neighbors[i]) avg += vertices[n].pos;
            avg /= (double)neighbors[i].size();
            newPos[i] = vertices[i].pos + lambda * (avg - vertices[i].pos);
        }
        for (size_t i = 0; i < vertices.size(); i++)
            vertices[i].pos = newPos[i];
    }
}

void Mesh::logSummary() const {
    std::cout << "Mesh: " << vertexCount() << " vertices, " << wedges.size()
              << " wedges, " << faceCount() << " faces\n";
    if (!textureData.empty())
        std::cout << "  color texture: " << textureWidth << "x" << textureHeight << "\n";
    if (!normalTextureData.empty())
        std::cout << "  normal texture: " << normalTextureWidth << "x" << normalTextureHeight << "\n";
}

} // namespace mesh
