/**
 * @file obj.cpp
 * @brief Wavefront OBJ import/export for Mesh.
 */
#include "relief/mesh/io/obj.h"

#include "stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_DOUBLE
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "tiny_obj_loader.h"

namespace fs = std::filesystem;

namespace {

/**
 * @brief Decodes an image file into a flat RGBA8 buffer via stb_image.
 * @param path Path to the image file.
 * @param[out] outData Resized to width*height*4 and filled with RGBA bytes.
 * @param[out] outW Decoded image width.
 * @param[out] outH Decoded image height.
 * @return true on success.
 */
bool loadTexture(const fs::path &path, std::vector<uint8_t> &outData, int &outW, int &outH) {
    int comp;
    unsigned char *pixels = stbi_load(path.c_str(), &outW, &outH, &comp, 4);
    if (!pixels) {
        std::cerr << "Could not load texture: " << path.string() << "\n";
        return false;
    }
    outData.assign(pixels, pixels + (size_t)outW * outH * 4);
    stbi_image_free(pixels);
    return true;
}

}  // namespace

namespace mesh::io::obj {

bool loadOBJ(Mesh &mesh, const std::string &path) {
    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();
    mesh.textureData.clear();
    mesh.textureWidth = mesh.textureHeight = 0;
    mesh.normalTextureData.clear();
    mesh.normalTextureWidth = mesh.normalTextureHeight = 0;

    fs::path dir = fs::path(path).parent_path();

    tinyobj::ObjReaderConfig readerConfig;
    readerConfig.mtl_search_path = dir.string();

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, readerConfig)) {
        if (!reader.Error().empty()) std::cerr << "OBJ error: " << reader.Error();
        return false;
    }
    if (!reader.Warning().empty()) std::cerr << "OBJ warning: " << reader.Warning();

    const tinyobj::attrib_t &attrib = reader.GetAttrib();

    // Weld vertices by actual 3D position, not by raw OBJ vertex index: some
    // exporters emit a separate "v" line per UV-seam/hard-edge split instead
    // of reusing one position index across multiple "vt" indices, so
    // index-based dedup alone would leave geometrically-identical corners as
    // distinct Mesh::Vertex entries (invisible to seam detection, and free
    // to drift apart under simplification since they'd never share
    // adjacency). Epsilon is relative to the whole model's bounding box.
    size_t numPositions = attrib.vertices.size() / 3;
    Eigen::Vector3d bmin(1e18, 1e18, 1e18), bmax(-1e18, -1e18, -1e18);
    for (size_t i = 0; i < numPositions; i++) {
        Eigen::Vector3d p(attrib.vertices[3 * i + 0], attrib.vertices[3 * i + 1],
                          attrib.vertices[3 * i + 2]);
        bmin = bmin.cwiseMin(p);
        bmax = bmax.cwiseMax(p);
    }
    double diag = (bmax - bmin).norm();
    if (!(diag > 1e-12)) diag = 1.0;
    double weldEps = diag * 1e-6;

    struct PosKey {
        int64_t x, y, z;
        bool operator<(const PosKey &o) const {
            return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
        }
    };
    auto quant = [&](double v) { return (int64_t)std::llround(v / weldEps); };
    std::map<PosKey, int> posToVertex;
    std::vector<int> rawToVertex(numPositions, -1);  // raw obj vertex_index → welded mesh vertex

    auto weldedVertex = [&](int rawIdx) -> int {
        if (rawToVertex[rawIdx] >= 0) return rawToVertex[rawIdx];
        Eigen::Vector3d p(attrib.vertices[3 * rawIdx + 0], attrib.vertices[3 * rawIdx + 1],
                          attrib.vertices[3 * rawIdx + 2]);
        PosKey k{quant(p.x()), quant(p.y()), quant(p.z())};
        auto [it, inserted] = posToVertex.emplace(k, (int)mesh.vertices.size());
        if (inserted) {
            Vertex vx;
            vx.pos = p;
            mesh.vertices.push_back(vx);
        }
        rawToVertex[rawIdx] = it->second;
        return it->second;
    };

    std::map<std::pair<int, int>, int> wedgeMap;  // (welded vertex, texcoord_index) → wedge
    for (const auto &shape : reader.GetShapes()) {
        const auto &indices = shape.mesh.indices;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            Face fc;
            for (int c = 0; c < 3; c++) {
                const tinyobj::index_t &ii = indices[i + c];
                int vIdx = weldedVertex(ii.vertex_index);

                auto wkey = std::make_pair(vIdx, ii.texcoord_index);
                auto [wit, wInserted] = wedgeMap.emplace(wkey, (int)mesh.wedges.size());
                if (wInserted) {
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
    for (const auto &mat : reader.GetMaterials()) {
        if (mesh.textureData.empty() && !mat.diffuse_texname.empty())
            loadTexture(dir / mat.diffuse_texname, mesh.textureData, mesh.textureWidth,
                        mesh.textureHeight);

        std::string normalMap = !mat.normal_texname.empty() ? mat.normal_texname : mat.bump_texname;
        if (mesh.normalTextureData.empty() && !normalMap.empty())
            loadTexture(dir / normalMap, mesh.normalTextureData, mesh.normalTextureWidth,
                        mesh.normalTextureHeight);

        if (!mesh.textureData.empty() && !mesh.normalTextureData.empty()) break;
    }

    return true;
}

bool saveOBJ(const Mesh &mesh, const std::string &path) {
    std::ofstream f(path);
    if (!f) {
        std::cerr << "Erro ao salvar: " << path << "\n";
        return false;
    }

    bool hasUV = false;
    for (auto &wg : mesh.wedges)
        if (wg.uv.squaredNorm() > 1e-12) {
            hasUV = true;
            break;
        }

    std::vector<int> remap(mesh.vertices.size(), -1);
    int idx = 1;
    for (int i = 0; i < (int)mesh.vertices.size(); i++) {
        if (!mesh.vertices[i].removed) {
            remap[i] = idx++;
            const auto &p = mesh.vertices[i].pos;
            f << "v " << p.x() << " " << p.y() << " " << p.z() << "\n";
        }
    }

    std::vector<int> vtIndex(mesh.wedges.size(), -1);
    if (hasUV) {
        int vtIdx = 1;
        for (int i = 0; i < (int)mesh.wedges.size(); i++) {
            const Wedge &wg = mesh.wedges[i];
            if (mesh.vertices[wg.vertex].removed) continue;
            vtIndex[i] = vtIdx++;
            f << "vt " << wg.uv.x() << " " << 1.0 - wg.uv.y() << "\n";
        }
    }

    for (auto &fc : mesh.faces) {
        if (fc.removed) continue;
        f << "f ";
        for (int c = 0; c < 3; c++) {
            const Wedge &wg = mesh.wedges[fc.w[c]];
            f << remap[wg.vertex];
            if (hasUV) f << "/" << vtIndex[fc.w[c]];
            f << (c < 2 ? " " : "\n");
        }
    }
    std::cout << "OBJ salvo: " << path << "\n";
    return true;
}

}  // namespace mesh::io::obj
