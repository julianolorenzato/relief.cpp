/**
 * @file obj.cpp
 * @brief Wavefront OBJ import/export for Mesh.
 */
#include "relief/mesh/io/obj.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

namespace mesh::io::obj {

bool loadOBJ(Mesh &mesh, const std::string &path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao abrir: " << path << "\n";
        return false;
    }

    mesh.vertices.clear();
    mesh.faces.clear();

    std::vector<Eigen::Vector3d> positions;
    std::vector<Eigen::Vector2d> uvCoords;
    std::map<int, int> vertexMap; // pos_idx → vertex (vertices are unique by position)

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
            std::vector<int> faceUVs;
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
                auto [it, inserted] = vertexMap.emplace(pos_idx, (int)mesh.vertices.size());
                if (inserted)
                {
                    Vertex vx;
                    vx.pos = positions[pos_idx];
                    mesh.vertices.push_back(vx);
                }
                int vIdx = it->second;
                int localUV = 0;
                if (uv_idx >= 0 && uv_idx < (int)uvCoords.size())
                    localUV = mesh.vertices[vIdx].uvIndex(uvCoords[uv_idx]);
                faceVerts.push_back(vIdx);
                faceUVs.push_back(localUV);
            }
            for (size_t i = 1; i + 1 < faceVerts.size(); i++)
            {
                Face fc;
                fc.v[0] = faceVerts[0];
                fc.v[1] = faceVerts[i];
                fc.v[2] = faceVerts[i + 1];
                fc.uv[0] = faceUVs[0];
                fc.uv[1] = faceUVs[i];
                fc.uv[2] = faceUVs[i + 1];
                mesh.faces.push_back(fc);
            }
        }
    }
    std::cout << "OBJ carregado: " << mesh.vertices.size()
              << " vértices, " << mesh.faces.size() << " faces\n";
    return true;
}

bool saveOBJ(const Mesh &mesh, const std::string &path)
{
    std::ofstream f(path);
    if (!f)
    {
        std::cerr << "Erro ao salvar: " << path << "\n";
        return false;
    }

    bool hasUV = false;
    for (auto &v : mesh.vertices)
        if (!v.removed)
            for (auto &uv : v.uvs)
                if (uv.squaredNorm() > 1e-12)
                {
                    hasUV = true;
                    break;
                }

    std::vector<int> remap(mesh.vertices.size(), -1);
    std::vector<int> vtOffset(mesh.vertices.size(), -1);
    int idx = 1;
    int vtIdx = 1;
    for (int i = 0; i < (int)mesh.vertices.size(); i++)
    {
        if (!mesh.vertices[i].removed)
        {
            remap[i] = idx++;
            const auto &p = mesh.vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }
    if (hasUV)
    {
        for (int i = 0; i < (int)mesh.vertices.size(); i++)
        {
            if (!mesh.vertices[i].removed)
            {
                vtOffset[i] = vtIdx;
                const auto &uvs = mesh.vertices[i].uvs;
                if (uvs.empty())
                {
                    f << "vt " << 0.0 << " " << 1.0 << "\n";
                    vtIdx++;
                }
                else
                {
                    for (auto &uv : uvs)
                    {
                        f << "vt " << uv.x() << " " << 1.0 - uv.y() << "\n";
                        vtIdx++;
                    }
                }
            }
        }
    }
    for (auto &fc : mesh.faces)
    {
        if (fc.removed)
            continue;
        if (hasUV)
        {
            f << "f " << remap[fc.v[0]] << "/" << (vtOffset[fc.v[0]] + fc.uv[0]) << " "
              << remap[fc.v[1]] << "/" << (vtOffset[fc.v[1]] + fc.uv[1]) << " "
              << remap[fc.v[2]] << "/" << (vtOffset[fc.v[2]] + fc.uv[2]) << "\n";
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

} // namespace mesh::io::obj
