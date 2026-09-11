/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge adjacency.
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

EdgeFaces Mesh::buildEdgeFaces() const {
    EdgeFaces edgeFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = wedges[faces[fi].w[i]].vertex, b = wedges[faces[fi].w[(i + 1) % 3]].vertex;
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }
    return edgeFaces;
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
