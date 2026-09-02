/**
 * @file mesh.cpp
 * @brief Mesh implementation: OBJ I/O and edge classification.
 */
#include "relief/mesh.h"
#include <iostream>
#include <fstream>
#include <sstream>

int Mesh::faceCount() const
{
    int n = 0;
    for (auto &f : faces)
        if (!f.removed)
            n++;
    return n;
}

int Mesh::vertexCount() const
{
    int n = 0;
    for (auto &v : vertices)
        if (!v.removed)
            n++;
    return n;
}

bool Mesh::loadOBJ(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao abrir: " << path << "\n";
        return false;
    }

    vertices.clear();
    faces.clear();

    std::vector<Eigen::Vector3d> positions;
    std::vector<Eigen::Vector2d> uvCoords;
    std::map<std::pair<int, int>, int> vertexMap; // (pos_idx, uv_idx) → vertex

    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v")
        {
            double px, py, pz;
            ss >> px >> py >> pz;
            positions.emplace_back(px, py, pz);
        }
        else if (tok == "vt")
        {
            double u, v;
            ss >> u >> v;
            // OBJ's vt has v=0 at the bottom of the image, but texture data
            // is uploaded with row 0 = top (no flip elsewhere in the
            // pipeline) — flip here so mesh UV matches texel rows.
            uvCoords.emplace_back(u, 1.0 - v);
        }
        else if (tok == "f")
        {
            // Uma face "f" pode ter 3+ vértices (quads, n-gons); lê todos e
            // faz fan-triangulation em vez de descartar os além do 3º.
            std::vector<int> faceVerts;
            std::string token;
            while (ss >> token)
            {
                int pos_idx = -1, uv_idx = -1;
                size_t s1 = token.find('/');
                pos_idx = std::stoi(token.substr(0, s1)) - 1;
                if (s1 != std::string::npos)
                {
                    size_t s2 = token.find('/', s1 + 1);
                    std::string uv_str = token.substr(s1 + 1,
                                                      s2 == std::string::npos ? s2 : s2 - s1 - 1);
                    if (!uv_str.empty())
                        uv_idx = std::stoi(uv_str) - 1;
                }
                if (pos_idx < 0)
                    pos_idx += (int)positions.size() + 1; // índice relativo negativo
                auto key = std::make_pair(pos_idx, uv_idx);
                auto [it, inserted] = vertexMap.emplace(key, (int)vertices.size());
                if (inserted)
                {
                    Vertex vx;
                    vx.pos = positions[pos_idx];
                    if (uv_idx >= 0 && uv_idx < (int)uvCoords.size())
                        vx.uv = uvCoords[uv_idx];
                    vertices.push_back(vx);
                }
                faceVerts.push_back(it->second);
            }
            for (size_t i = 1; i + 1 < faceVerts.size(); i++)
            {
                Face fc;
                fc.v[0] = faceVerts[0];
                fc.v[1] = faceVerts[i];
                fc.v[2] = faceVerts[i + 1];
                faces.push_back(fc);
            }
        }
    }
    std::cout << "OBJ carregado: " << vertices.size()
              << " vértices, " << faces.size() << " faces\n";
    return true;
}

bool Mesh::saveOBJ(const std::string &path) const
{
    std::ofstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao salvar: " << path << "\n";
        return false;
    }

    bool hasUV = false;
    for (auto &v : vertices)
        if (!v.removed && v.uv.squaredNorm() > 1e-12)
        {
            hasUV = true;
            break;
        }

    std::vector<int> remap(vertices.size(), -1);
    int idx = 1;
    for (int i = 0; i < (int)vertices.size(); i++)
    {
        if (!vertices[i].removed)
        {
            remap[i] = idx++;
            const auto &p = vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }
    if (hasUV)
    {
        for (int i = 0; i < (int)vertices.size(); i++)
        {
            if (!vertices[i].removed)
            {
                const auto &uv = vertices[i].uv;
                f << "vt " << uv.x() << " " << 1.0 - uv.y() << "\n";
            }
        }
    }
    for (auto &fc : faces)
    {
        if (fc.removed)
            continue;
        if (hasUV)
        {
            f << "f " << remap[fc.v[0]] << "/" << remap[fc.v[0]] << " "
              << remap[fc.v[1]] << "/" << remap[fc.v[1]] << " "
              << remap[fc.v[2]] << "/" << remap[fc.v[2]] << "\n";
        }
        else
        {
            f << "f " << remap[fc.v[0]] << " "
              << remap[fc.v[1]] << " "
              << remap[fc.v[2]] << "\n";
        }
    }
    std::cout << "OBJ salvo: " << path << "\n";
    return true;
}

// Classificação de arestas (boundary = referenciada por exatamente 1 face)

std::vector<Mesh::EdgeInfo> Mesh::classifyEdges() const
{
    std::map<std::pair<int, int>, std::vector<int>> edgeFaces;
    for (int fi = 0; fi < (int)faces.size(); fi++)
    {
        if (faces[fi].removed)
            continue;
        for (int i = 0; i < 3; i++)
        {
            int a = faces[fi].v[i], b = faces[fi].v[(i + 1) % 3];
            if (a > b)
                std::swap(a, b);
            edgeFaces[{a, b}].push_back(fi);
        }
    }

    std::vector<EdgeInfo> result;
    result.reserve(edgeFaces.size());
    for (auto &[edge, faceList] : edgeFaces)
    {
        result.push_back({edge.first, edge.second, faceList.size() == 1, faceList[0]});
    }
    return result;
}
