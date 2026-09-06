/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge classification.
 */
#include "relief/mesh.h"

#include <map>
#include <utility>

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

// Classificação de arestas (boundary = referenciada por exatamente 1 face)
std::vector<Edge> Mesh::classifyEdges() const {
    std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        if (faces[fi].removed) continue;
        for (int i = 0; i < 3; i++) {
            int a = faces[fi].v[i], b = faces[fi].v[(i + 1) % 3];
            if (a > b) std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }

    std::vector<Edge> result;
    result.reserve(edgeFaces.size());
    for (auto &[edge, faceList] : edgeFaces) {
        std::optional<int> faceId = faceList.size() == 1
                                        ? std::optional<int>(faceList[0])
                                        : std::nullopt;
        result.push_back({edge.first, edge.second, faceId});
    }
    return result;
}
