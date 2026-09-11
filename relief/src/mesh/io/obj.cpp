/**
 * @file obj.cpp
 * @brief Wavefront OBJ import/export for Mesh.
 */
#include "relief/mesh/io/obj.h"
#include "stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_DOUBLE
#include "tiny_obj_loader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>

namespace mesh::io::obj {

namespace {

/**
 * @brief Directory portion of a path, trailing slash included.
 * @param path File path to split.
 * @return Everything up to and including the last path separator, or ""
 *         if `path` has no separator.
 */
std::string dirOf(const std::string &path)
{
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? "" : path.substr(0, pos + 1);
}

/**
 * @brief Decodes an image file into a flat RGBA8 buffer via stb_image.
 * @param path Path to the image file.
 * @param[out] outData Resized to width*height*4 and filled with RGBA bytes.
 * @param[out] outW Decoded image width.
 * @param[out] outH Decoded image height.
 * @return true on success.
 */
bool loadTexture(const std::string &path, std::vector<uint8_t> &outData, int &outW, int &outH)
{
    int comp;
    unsigned char *pixels = stbi_load(path.c_str(), &outW, &outH, &comp, 4);
    if (!pixels)
    {
        std::cerr << "Could not load texture: " << path << "\n";
        return false;
    }
    outData.assign(pixels, pixels + (size_t)outW * outH * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace

/* Superseded by the tinyobjloader-based loadOBJ below; kept for reference.
   Also predates the Wedge-based Face (Face::v/Face::uv, Vertex::uvs), so
   this wouldn't compile as-is even if un-commented.

void parseMTL(const std::string &mtlPath, std::string &colorMap, std::string &normalMap)
{
    std::ifstream f(mtlPath);
    if (!f)
    {
        std::cerr << "Could not open MTL: " << mtlPath << "\n";
        return;
    }

    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        // The filename is the last token on the line: map_* lines may carry
        // options (e.g. "-o 0 0") before it.
        std::string t, last;
        while (ss >> t)
            last = t;
        if (last.empty())
            continue;
        if (tok == "map_Kd" && colorMap.empty())
            colorMap = last;
        else if ((tok == "map_Bump" || tok == "bump" || tok == "norm") && normalMap.empty())
            normalMap = last;
    }
}

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
    mesh.textureData.clear();
    mesh.textureWidth = mesh.textureHeight = 0;
    mesh.normalTextureData.clear();
    mesh.normalTextureWidth = mesh.normalTextureHeight = 0;

    std::string dir = dirOf(path);
    std::string colorMapFile, normalMapFile;

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
        else if (tok == "mtllib")
        {
            std::string mtlFile;
            ss >> mtlFile;
            if (!mtlFile.empty())
                parseMTL(dir + mtlFile, colorMapFile, normalMapFile);
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
    if (!colorMapFile.empty() &&
        loadTexture(dir + colorMapFile, mesh.textureData, mesh.textureWidth, mesh.textureHeight))
        std::cout << "OBJ textura de cor: " << mesh.textureWidth << "x" << mesh.textureHeight << "\n";
    if (!normalMapFile.empty() &&
        loadTexture(dir + normalMapFile, mesh.normalTextureData, mesh.normalTextureWidth, mesh.normalTextureHeight))
        std::cout << "OBJ textura de normal: " << mesh.normalTextureWidth << "x" << mesh.normalTextureHeight << "\n";

    std::cout << "OBJ carregado: " << mesh.vertices.size()
              << " vértices, " << mesh.faces.size() << " faces\n";
    return true;
}

*/

bool loadOBJ(Mesh &mesh, const std::string &path)
{
    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();
    mesh.textureData.clear();
    mesh.textureWidth = mesh.textureHeight = 0;
    mesh.normalTextureData.clear();
    mesh.normalTextureWidth = mesh.normalTextureHeight = 0;

    std::string dir = dirOf(path);

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                               path.c_str(), dir.c_str(), /*triangulate=*/true);
    if (!warn.empty())
        std::cerr << "OBJ warning: " << warn;
    if (!err.empty())
        std::cerr << "OBJ error: " << err;
    if (!ok)
        return false;

    // Weld vertices by actual 3D position, not by raw OBJ vertex index: some
    // exporters emit a separate "v" line per UV-seam/hard-edge split instead
    // of reusing one position index across multiple "vt" indices, so
    // index-based dedup alone would leave geometrically-identical corners as
    // distinct Mesh::Vertex entries (invisible to seam detection, and free
    // to drift apart under simplification since they'd never share
    // adjacency). Epsilon is relative to the whole model's bounding box.
    size_t numPositions = attrib.vertices.size() / 3;
    Eigen::Vector3d bmin(1e18, 1e18, 1e18), bmax(-1e18, -1e18, -1e18);
    for (size_t i = 0; i < numPositions; i++)
    {
        Eigen::Vector3d p(attrib.vertices[3 * i + 0], attrib.vertices[3 * i + 1], attrib.vertices[3 * i + 2]);
        bmin = bmin.cwiseMin(p);
        bmax = bmax.cwiseMax(p);
    }
    double diag = (bmax - bmin).norm();
    if (!(diag > 1e-12))
        diag = 1.0;
    double weldEps = diag * 1e-6;

    struct PosKey
    {
        int64_t x, y, z;
        bool operator<(const PosKey &o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
    };
    auto quant = [&](double v) { return (int64_t)std::llround(v / weldEps); };
    std::map<PosKey, int> posToVertex;
    std::vector<int> rawToVertex(numPositions, -1); // raw obj vertex_index → welded mesh vertex

    auto weldedVertex = [&](int rawIdx) -> int
    {
        if (rawToVertex[rawIdx] >= 0)
            return rawToVertex[rawIdx];
        Eigen::Vector3d p(attrib.vertices[3 * rawIdx + 0], attrib.vertices[3 * rawIdx + 1], attrib.vertices[3 * rawIdx + 2]);
        PosKey k{quant(p.x()), quant(p.y()), quant(p.z())};
        auto [it, inserted] = posToVertex.emplace(k, (int)mesh.vertices.size());
        if (inserted)
        {
            Vertex vx;
            vx.pos = p;
            mesh.vertices.push_back(vx);
        }
        rawToVertex[rawIdx] = it->second;
        return it->second;
    };

    std::map<std::pair<int, int>, int> wedgeMap; // (welded vertex, texcoord_index) → wedge
    for (const auto &shape : shapes)
    {
        const auto &indices = shape.mesh.indices;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            Face fc;
            for (int c = 0; c < 3; c++)
            {
                const tinyobj::index_t &ii = indices[i + c];
                int vIdx = weldedVertex(ii.vertex_index);

                auto wkey = std::make_pair(vIdx, ii.texcoord_index);
                auto [wit, wInserted] = wedgeMap.emplace(wkey, (int)mesh.wedges.size());
                if (wInserted)
                {
                    Wedge wg;
                    wg.vertex = vIdx;
                    if (ii.texcoord_index >= 0)
                        // OBJ's vt has v=0 at the bottom of the image, but
                        // texture data is uploaded with row 0 = top — flip
                        // here so mesh UV matches texel rows.
                        wg.uv = Eigen::Vector2d(attrib.texcoords[2 * ii.texcoord_index + 0],
                                                 1.0 - attrib.texcoords[2 * ii.texcoord_index + 1]);
                    mesh.wedges.push_back(wg);
                }
                fc.w[c] = wit->second;
            }
            mesh.faces.push_back(fc);
        }
    }

    // First material with a texture wins: Mesh holds one global color/normal
    // texture pair (matching the glTF import path), not per-material ones.
    for (const auto &mat : materials)
    {
        if (mesh.textureData.empty() && !mat.diffuse_texname.empty() &&
            loadTexture(dir + mat.diffuse_texname, mesh.textureData, mesh.textureWidth, mesh.textureHeight))
            std::cout << "OBJ textura de cor: " << mesh.textureWidth << "x" << mesh.textureHeight << "\n";

        std::string normalMap = !mat.normal_texname.empty() ? mat.normal_texname : mat.bump_texname;
        if (mesh.normalTextureData.empty() && !normalMap.empty() &&
            loadTexture(dir + normalMap, mesh.normalTextureData, mesh.normalTextureWidth, mesh.normalTextureHeight))
            std::cout << "OBJ textura de normal: " << mesh.normalTextureWidth << "x" << mesh.normalTextureHeight << "\n";

        if (!mesh.textureData.empty() && !mesh.normalTextureData.empty())
            break;
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
    for (auto &wg : mesh.wedges)
        if (wg.uv.squaredNorm() > 1e-12)
        {
            hasUV = true;
            break;
        }

    std::vector<int> remap(mesh.vertices.size(), -1);
    int idx = 1;
    for (int i = 0; i < (int)mesh.vertices.size(); i++)
    {
        if (!mesh.vertices[i].removed)
        {
            remap[i] = idx++;
            const auto &p = mesh.vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }

    std::vector<int> vtIndex(mesh.wedges.size(), -1);
    if (hasUV)
    {
        int vtIdx = 1;
        for (int i = 0; i < (int)mesh.wedges.size(); i++)
        {
            const Wedge &wg = mesh.wedges[i];
            if (mesh.vertices[wg.vertex].removed)
                continue;
            vtIndex[i] = vtIdx++;
            f << "vt " << wg.uv.x() << " " << 1.0 - wg.uv.y() << "\n";
        }
    }

    for (auto &fc : mesh.faces)
    {
        if (fc.removed)
            continue;
        f << "f ";
        for (int c = 0; c < 3; c++)
        {
            const Wedge &wg = mesh.wedges[fc.w[c]];
            f << remap[wg.vertex];
            if (hasUV)
                f << "/" << vtIndex[fc.w[c]];
            f << (c < 2 ? " " : "\n");
        }
    }
    std::cout << "OBJ salvo: " << path << "\n";
    return true;
}

} // namespace mesh::io::obj
