/**
 * @file edge_diag.cpp
 * @brief Diagnostic tool: loads a mesh and checks whether boundary edges
 *        reported by Mesh::buildEdgeFaces are "real" mesh boundaries or
 *        artifacts of duplicated vertices at UV/material seams (same 3D
 *        position, different vertex index). Not part of the library build.
 */
#include "relief/mesh.h"
#include "relief/mesh_io.h"
#include <iostream>
#include <map>
#include <string>
#include <array>
#include <cmath>
#include <vector>

static bool endsWith(const std::string &s, const std::string &suf)
{
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "uso: edge_diag <arquivo.obj|.gltf|.glb>\n";
        return 1;
    }
    std::string path = argv[1];

    Mesh mesh;
    bool ok = endsWith(path, ".obj") ? loadOBJ(mesh, path) : loadGLTF(mesh, path);
    if (!ok)
    {
        std::cerr << "falha ao carregar " << path << "\n";
        return 1;
    }

    auto edgeFaces = mesh.buildEdgeFaces();

    int boundaryCount = 0;
    std::vector<std::pair<int, int>> boundaryEdges;
    for (auto &[edge, faceIds] : edgeFaces)
        if (faceIds.size() == 1)
        {
            boundaryCount++;
            boundaryEdges.push_back(edge);
        }

    // Chave geométrica: posições 3D das duas pontas da aresta, arredondadas.
    auto quantize = [](double x) { return std::llround(x * 1e6); };
    std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>, int> geomEdgeCount;

    auto posKey = [&](int vi) -> std::array<long long, 3>
    {
        auto &p = mesh.vertices[vi].pos;
        return {quantize(p.x()), quantize(p.y()), quantize(p.z())};
    };

    for (auto &e : boundaryEdges)
    {
        auto a = posKey(e.first), b = posKey(e.second);
        if (b < a)
            std::swap(a, b);
        geomEdgeCount[{a, b}]++;
    }

    int seamArtifacts = 0; // arestas de borda cujo par geométrico aparece 2x (ou mais)
    for (auto &[key, cnt] : geomEdgeCount)
        if (cnt >= 2)
            seamArtifacts += cnt;

    std::cout << "vertices: " << mesh.vertices.size() << "\n";
    std::cout << "faces: " << mesh.faces.size() << "\n";
    std::cout << "arestas totais: " << edgeFaces.size() << "\n";
    std::cout << "arestas de borda (por indice): " << boundaryCount << "\n";
    std::cout << "arestas de borda com posicao 3D duplicada (provavel costura UV/material): "
              << seamArtifacts << " (" << (boundaryCount ? 100.0 * seamArtifacts / boundaryCount : 0.0) << "%)\n";
    std::cout << "arestas de borda geometricamente reais (estimativa): "
              << (boundaryCount - seamArtifacts) << "\n";

    return 0;
}
