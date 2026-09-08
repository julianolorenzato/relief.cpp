/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge adjacency.
 */
#include "relief/mesh.h"

#include <map>
#include <utility>

int Vertex::uvIndex(const Eigen::Vector2d& uv, double eps) {
    double eps2 = eps * eps;
    for (size_t i = 0; i < uvs.size(); i++)
        if ((uvs[i] - uv).squaredNorm() < eps2)
            return (int)i;
    uvs.push_back(uv);
    return (int)uvs.size() - 1;
}

Mesh::GPUMesh Mesh::explodeForGPU() const {
    GPUMesh out;
    std::map<std::pair<int, int>, uint32_t> keyToIdx;
    out.indices.reserve(faces.size() * 3);
    for (const auto &f : faces) {
        if (f.removed) continue;
        for (int k = 0; k < 3; k++) {
            int vi = f.v[k];
            int uvi = f.uv[k];
            auto [it, inserted] = keyToIdx.try_emplace({vi, uvi}, (uint32_t)out.positions.size());
            if (inserted) {
                const Vertex &v = vertices[vi];
                out.positions.push_back(v.pos);
                out.uvs.push_back(uvi >= 0 && uvi < (int)v.uvs.size()
                                       ? v.uvs[uvi]
                                       : Eigen::Vector2d::Zero());
            }
            out.indices.push_back(it->second);
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
            int a = faces[fi].v[i], b = faces[fi].v[(i + 1) % 3];
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }
    return edgeFaces;
}
