/**
 * @file obj.cpp
 * @brief Wavefront OBJ import/export for Mesh.
 */
#include "relief/mesh/io/obj.h"

#include "stb_image.h"
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYOBJLOADER_USE_DOUBLE
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
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

/// Hashes a 3D position by its exact bits (no epsilon): used to weld
/// vertices that share bit-identical coordinates (see loadOBJ).
struct PosHash {
    size_t operator()(const std::array<double, 3> &p) const {
        size_t h = 0;
        auto combine = [&h](double v) {
            h ^= std::hash<double>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        combine(p[0]);
        combine(p[1]);
        combine(p[2]);
        return h;
    }
};

/// Hashes a (welded vertex, UV) wedge key by exact bits (no epsilon): used
/// to dedupe wedges by UV value instead of by raw OBJ texcoord index (see
/// loadOBJ). std::pair/std::array already give us operator== for free.
struct WedgeHash {
    size_t operator()(const std::pair<int, std::array<double, 2>> &k) const {
        size_t h = std::hash<int>{}(k.first);
        auto combine = [&h](double v) {
            h ^= std::hash<double>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        combine(k.second[0]);
        combine(k.second[1]);
        return h;
    }
};

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
    // adjacency). Keyed on the exact position bits (no epsilon): duplicated
    // "v" lines from the same export pass carry bit-identical coordinates,
    // so an exact hash catches them without the cost/fuzziness of a
    // tolerance-based match.

    // pos → vertex_index, avoids multiple vertices with same 3D position
    std::unordered_map<std::array<double, 3>, int, PosHash> vertexMap;
    // (vertex_index, uv) → wedge_index,
    // avoids multiple wedges from same vertex with same UV coordinates
    std::unordered_map<std::pair<int, std::array<double, 2>>, int, WedgeHash> wedgeMap;

    for (const auto &shape : reader.GetShapes()) {
        const auto &indices = shape.mesh.indices;
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {  // Iterates through faces
            Face face;
            for (int c = 0; c < 3; c++) {
                const tinyobj::index_t &corner = indices[i + c];

                double x = attrib.vertices[3 * corner.vertex_index + 0];
                double y = attrib.vertices[3 * corner.vertex_index + 1];
                double z = attrib.vertices[3 * corner.vertex_index + 2];
                std::array<double, 3> position{x, y, z};
                auto [vIterator, vInserted] =
                    vertexMap.emplace(position, (int)mesh.vertices.size());
                if (vInserted) {
                    // We are only pushing one vertex per position to the Mesh
                    Vertex vertex{.pos = Eigen::Vector3d(x, y, z)};
                    mesh.vertices.push_back(vertex);
                }
                int vertexIndex = vIterator->second;

                double u = 0.0, v = 0.0;
                if (corner.texcoord_index >= 0) {
                    // OBJ's vt has v=0 at the bottom of the image, but
                    // texture data is uploaded with row 0 = top — flip
                    // here so mesh UV matches texel rows.
                    u = attrib.texcoords[2 * corner.texcoord_index + 0];
                    v = 1.0 - attrib.texcoords[2 * corner.texcoord_index + 1];
                }

                auto wkey = std::make_pair(vertexIndex, std::array<double, 2>{u, v});
                auto [wIterator, wInserted] = wedgeMap.emplace(wkey, (int)mesh.wedges.size());
                if (wInserted) {
                    // We are only pushing one wedge
                    // per vertex per UV to the Mesh
                    Wedge wedge{.uv = Eigen::Vector2d(u, v), .vertex = vertexIndex};
                    mesh.wedges.push_back(wedge);
                }
                face.w[c] = wIterator->second;
            }
            mesh.faces.push_back(face);
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
