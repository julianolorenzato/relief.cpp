/**
 * @file mesh_io.cpp
 * @brief OBJ and glTF/GLB import/export for Mesh, via tinygltf. Also hosts
 *        the tinygltf/stb_image single-header implementations.
 */
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "relief/mesh_io.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <tuple>
#include <cmath>

// ─── OBJ ──────────────────────────────────────────────────────────────────────

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

// ─── glTF helpers ───────────────────────────────────────────────────────────

/// @return true if `path` ends in ".glb" (case-insensitive).
static bool endsWithGlb(const std::string &path)
{
    if (path.size() < 4)
        return false;
    std::string ext = path.substr(path.size() - 4);
    for (auto &c : ext)
        c = (char)std::tolower((unsigned char)c);
    return ext == ".glb";
}

/// @return Typed pointer to the start of an accessor's data.
template <typename T>
static const T *accessorData(const tinygltf::Model &model, int accessorIdx)
{
    const auto &acc = model.accessors[accessorIdx];
    const auto &view = model.bufferViews[acc.bufferView];
    const auto &buf = model.buffers[view.buffer];
    return reinterpret_cast<const T *>(
        buf.data.data() + view.byteOffset + acc.byteOffset);
}

/// @brief Copies a glTF texture's image data into a linear RGBA8 buffer.
/// @param model Loaded glTF model.
/// @param texIdx Index into `model.textures`.
/// @param[out] outData RGBA8 pixel buffer, row-major.
/// @param[out] outW,outH Image dimensions.
/// @return false if `texIdx` is invalid or the referenced image has no data.
static bool extractTexture(const tinygltf::Model &model, int texIdx,
                           std::vector<uint8_t> &outData, int &outW, int &outH)
{
    if (texIdx < 0 || texIdx >= (int)model.textures.size())
        return false;
    int imgIdx = model.textures[texIdx].source;
    if (imgIdx < 0 || imgIdx >= (int)model.images.size())
        return false;
    const auto &img = model.images[imgIdx];
    if (img.image.empty() || img.width <= 0 || img.height <= 0)
        return false;

    outW = img.width;
    outH = img.height;
    int comp = img.component; // 3=RGB, 4=RGBA
    outData.resize((size_t)img.width * img.height * 4);
    for (int p = 0; p < img.width * img.height; p++)
    {
        outData[p * 4 + 0] = img.image[p * comp + 0];
        outData[p * 4 + 1] = img.image[p * comp + 1];
        outData[p * 4 + 2] = img.image[p * comp + 2];
        outData[p * 4 + 3] = (comp == 4) ? img.image[p * comp + 3] : 255;
    }
    return true;
}

// ─── loadGLTF ────────────────────────────────────────────────────────────────

bool loadGLTF(Mesh &mesh, const std::string &path)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = endsWithGlb(path)
                  ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
                  : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty())
        std::cerr << "GLTF warn: " << warn << "\n";
    if (!ok)
    {
        std::cerr << "GLTF erro: " << err << "\n";
        return false;
    }

    mesh.vertices.clear();
    mesh.faces.clear();

    mesh.textureData.clear();
    mesh.textureWidth = mesh.textureHeight = 0;
    mesh.normalTextureData.clear();
    mesh.normalTextureWidth = mesh.normalTextureHeight = 0;

    // ── Weld vertices by position (glTF buffers are already GPU-exploded: a
    //    UV seam duplicates the position). Epsilon is relative to the
    //    combined bounding box of every POSITION accessor we'll read.
    Eigen::Vector3d bmin(1e18, 1e18, 1e18), bmax(-1e18, -1e18, -1e18);
    for (const auto &gltfMesh : model.meshes)
        for (const auto &prim : gltfMesh.primitives)
        {
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            const auto &acc = model.accessors[posIt->second];
            if (acc.minValues.size() == 3 && acc.maxValues.size() == 3)
            {
                bmin = bmin.cwiseMin(Eigen::Vector3d(acc.minValues[0], acc.minValues[1], acc.minValues[2]));
                bmax = bmax.cwiseMax(Eigen::Vector3d(acc.maxValues[0], acc.maxValues[1], acc.maxValues[2]));
            }
        }
    double diag = (bmax - bmin).norm();
    if (!(diag > 1e-12)) diag = 1.0;
    double weldEps = diag * 1e-6;

    struct PosKey
    {
        int64_t x, y, z;
        bool operator<(const PosKey &o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
    };
    auto quant = [&](double v) { return (int64_t)std::llround(v / weldEps); };
    std::map<PosKey, int> posToVertex;
    auto weldVertex = [&](const Eigen::Vector3d &p) -> int
    {
        PosKey k{quant(p.x()), quant(p.y()), quant(p.z())};
        auto it = posToVertex.find(k);
        if (it != posToVertex.end())
            return it->second;
        int id = (int)mesh.vertices.size();
        posToVertex[k] = id;
        Vertex v;
        v.pos = p;
        mesh.vertices.push_back(v);
        return id;
    };

    for (const auto &gltfMesh : model.meshes)
    {
        for (const auto &prim : gltfMesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
                prim.mode != -1)
                continue; // -1 = padrão (triângulos)

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
                continue;

            const auto &posAcc = model.accessors[posIt->second];
            if (posAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                posAcc.type != TINYGLTF_TYPE_VEC3)
                continue;

            const float *posPtr = accessorData<float>(model, posIt->second);
            const auto &posView = model.bufferViews[posAcc.bufferView];
            size_t posStride = posView.byteStride == 0
                                   ? 3 * sizeof(float)
                                   : posView.byteStride;

            // Raw (un-welded) local index -> welded mesh vertex index.
            std::vector<int> localToMesh(posAcc.count);
            for (size_t i = 0; i < posAcc.count; i++)
            {
                const float *p = reinterpret_cast<const float *>(
                    reinterpret_cast<const uint8_t *>(posPtr) + i * posStride);
                localToMesh[i] = weldVertex(Eigen::Vector3d(p[0], p[1], p[2]));
            }

            // UV (TEXCOORD_0): raw local index -> UV value, if present.
            std::vector<Eigen::Vector2d> localUV;
            bool hasUV = false;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
            {
                const auto &uvAcc = model.accessors[uvIt->second];
                if (uvAcc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
                    uvAcc.type == TINYGLTF_TYPE_VEC2)
                {
                    hasUV = true;
                    localUV.resize(posAcc.count, Eigen::Vector2d::Zero());
                    const float *uvPtr = accessorData<float>(model, uvIt->second);
                    const auto &uvView = model.bufferViews[uvAcc.bufferView];
                    size_t uvStride = uvView.byteStride == 0
                                          ? 2 * sizeof(float)
                                          : uvView.byteStride;
                    for (size_t i = 0; i < uvAcc.count && i < posAcc.count; i++)
                    {
                        const float *uv = reinterpret_cast<const float *>(
                            reinterpret_cast<const uint8_t *>(uvPtr) + i * uvStride);
                        localUV[i] = Eigen::Vector2d(uv[0], uv[1]);
                    }
                }
            }

            // Builds a Face from 3 raw local indices, welding vertices and
            // registering each corner's UV on its (welded) vertex.
            auto makeFace = [&](size_t i0, size_t i1, size_t i2)
            {
                Face f;
                size_t li[3] = {i0, i1, i2};
                for (int k = 0; k < 3; k++)
                {
                    f.v[k] = localToMesh[li[k]];
                    f.uv[k] = hasUV ? mesh.vertices[f.v[k]].uvIndex(localUV[li[k]]) : 0;
                }
                mesh.faces.push_back(f);
            };

            if (prim.indices < 0)
            {
                // Sem buffer de índices: assume triângulos sequenciais
                for (size_t i = 0; i + 2 < posAcc.count; i += 3)
                    makeFace(i, i + 1, i + 2);
            }
            else
            {
                const auto &idxAcc = model.accessors[prim.indices];
                size_t idxCount = idxAcc.count;

                auto addFaces = [&](auto *idx)
                {
                    for (size_t i = 0; i + 2 < idxCount; i += 3)
                        makeFace((size_t)idx[i], (size_t)idx[i + 1], (size_t)idx[i + 2]);
                };

                switch (idxAcc.componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    addFaces(accessorData<uint8_t>(model, prim.indices));
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    addFaces(accessorData<uint16_t>(model, prim.indices));
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    addFaces(accessorData<uint32_t>(model, prim.indices));
                    break;
                default:
                    std::cerr << "GLTF: tipo de índice não suportado\n";
                    break;
                }
            }
        }
    }

    // ── Extrai texturas de cor base e de normal (primeiro material que as tiver) ──
    {
        bool gotColor = false, gotNormal = false;
        for (const auto &gltfMesh : model.meshes)
        {
            for (const auto &prim : gltfMesh.primitives)
            {
                if (prim.material < 0 || prim.material >= (int)model.materials.size())
                    continue;
                const auto &mat = model.materials[prim.material];

                if (!gotColor && extractTexture(model, mat.pbrMetallicRoughness.baseColorTexture.index,
                                                mesh.textureData, mesh.textureWidth, mesh.textureHeight))
                {
                    gotColor = true;
                    std::cout << "GLTF textura de cor: " << mesh.textureWidth << "x" << mesh.textureHeight << "\n";
                }
                if (!gotNormal && extractTexture(model, mat.normalTexture.index,
                                                 mesh.normalTextureData, mesh.normalTextureWidth, mesh.normalTextureHeight))
                {
                    gotNormal = true;
                    std::cout << "GLTF textura de normal: " << mesh.normalTextureWidth << "x" << mesh.normalTextureHeight << "\n";
                }
                if (gotColor && gotNormal)
                    goto textures_done;
            }
        }
    textures_done:;
    }

    std::cout << "GLTF carregado: " << mesh.vertices.size()
              << " vértices, " << mesh.faces.size() << " faces\n";
    return !mesh.vertices.empty();
}

// ─── saveGLTF ────────────────────────────────────────────────────────────────

bool saveGLTF(const Mesh &mesh, const std::string &path)
{
    // Explode em vértices GPU-friendly: cada (vértice, slot de UV) distinto
    // usado por algum canto de face vira um vértice único aqui.
    Mesh::GPUMesh gpu = mesh.explodeForGPU();

    if (gpu.positions.empty() || gpu.indices.empty())
    {
        std::cerr << "GLTF: malha vazia, nada a salvar\n";
        return false;
    }

    bool hasUV = false;
    for (auto &uv : gpu.uvs)
        if (uv.squaredNorm() > 1e-12)
        {
            hasUV = true;
            break;
        }

    std::vector<float> positions;
    positions.reserve(gpu.positions.size() * 3);
    for (auto &p : gpu.positions)
    {
        positions.push_back((float)p.x());
        positions.push_back((float)p.y());
        positions.push_back((float)p.z());
    }

    std::vector<float> uvs;
    if (hasUV)
    {
        uvs.reserve(gpu.uvs.size() * 2);
        for (auto &uv : gpu.uvs)
        {
            uvs.push_back((float)uv.x());
            uvs.push_back((float)uv.y());
        }
    }

    const std::vector<uint32_t> &indices = gpu.indices;

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "QEM Simplifier";

    // ── Buffer único: [positions | uvs? | indices] ────────────────────────────
    size_t posBytes = positions.size() * sizeof(float);
    size_t uvBytes = uvs.size() * sizeof(float);
    size_t idxBytes = indices.size() * sizeof(uint32_t);

    tinygltf::Buffer buf;
    buf.data.resize(posBytes + uvBytes + idxBytes);
    std::memcpy(buf.data.data(), positions.data(), posBytes);
    if (hasUV)
        std::memcpy(buf.data.data() + posBytes, uvs.data(), uvBytes);
    std::memcpy(buf.data.data() + posBytes + uvBytes, indices.data(), idxBytes);
    model.buffers.push_back(std::move(buf));

    // ── Buffer views ──────────────────────────────────────────────────────────
    tinygltf::BufferView posView;
    posView.buffer = 0;
    posView.byteOffset = 0;
    posView.byteLength = posBytes;
    posView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
    model.bufferViews.push_back(posView);

    int uvViewIdx = -1;
    if (hasUV)
    {
        tinygltf::BufferView uvView;
        uvView.buffer = 0;
        uvView.byteOffset = posBytes;
        uvView.byteLength = uvBytes;
        uvView.target = TINYGLTF_TARGET_ARRAY_BUFFER;
        uvViewIdx = (int)model.bufferViews.size();
        model.bufferViews.push_back(uvView);
    }

    tinygltf::BufferView idxView;
    idxView.buffer = 0;
    idxView.byteOffset = posBytes + uvBytes;
    idxView.byteLength = idxBytes;
    idxView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    int idxViewIdx = (int)model.bufferViews.size();
    model.bufferViews.push_back(idxView);

    // ── Bounding box para o accessor de posições ──────────────────────────────
    float minX = positions[0], minY = positions[1], minZ = positions[2];
    float maxX = minX, maxY = minY, maxZ = minZ;
    for (size_t i = 0; i < positions.size(); i += 3)
    {
        minX = std::min(minX, positions[i]);
        minY = std::min(minY, positions[i + 1]);
        minZ = std::min(minZ, positions[i + 2]);
        maxX = std::max(maxX, positions[i]);
        maxY = std::max(maxY, positions[i + 1]);
        maxZ = std::max(maxZ, positions[i + 2]);
    }

    // ── Accessors ──────────────────────────────────────────────────────────────
    tinygltf::Accessor posAcc;
    posAcc.bufferView = 0;
    posAcc.byteOffset = 0;
    posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    posAcc.count = gpu.positions.size();
    posAcc.type = TINYGLTF_TYPE_VEC3;
    posAcc.minValues = {(double)minX, (double)minY, (double)minZ};
    posAcc.maxValues = {(double)maxX, (double)maxY, (double)maxZ};
    int posAccIdx = (int)model.accessors.size();
    model.accessors.push_back(posAcc);

    int uvAccIdx = -1;
    if (hasUV)
    {
        tinygltf::Accessor uvAcc;
        uvAcc.bufferView = uvViewIdx;
        uvAcc.byteOffset = 0;
        uvAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        uvAcc.count = gpu.uvs.size();
        uvAcc.type = TINYGLTF_TYPE_VEC2;
        uvAccIdx = (int)model.accessors.size();
        model.accessors.push_back(uvAcc);
    }

    tinygltf::Accessor idxAcc;
    idxAcc.bufferView = idxViewIdx;
    idxAcc.byteOffset = 0;
    idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    idxAcc.count = indices.size();
    idxAcc.type = TINYGLTF_TYPE_SCALAR;
    int idxAccIdx = (int)model.accessors.size();
    model.accessors.push_back(idxAcc);

    // ── Mesh ──────────────────────────────────────────────────────────────────
    tinygltf::Primitive prim;
    prim.attributes["POSITION"] = posAccIdx;
    if (hasUV)
        prim.attributes["TEXCOORD_0"] = uvAccIdx;
    prim.indices = idxAccIdx;
    prim.mode = TINYGLTF_MODE_TRIANGLES;

    tinygltf::Mesh gltfMesh;
    gltfMesh.name = "simplified";
    gltfMesh.primitives.push_back(prim);
    model.meshes.push_back(gltfMesh);

    // ── Node + Scene ──────────────────────────────────────────────────────────
    tinygltf::Node node;
    node.mesh = 0;
    model.nodes.push_back(node);

    tinygltf::Scene scene;
    scene.nodes.push_back(0);
    model.scenes.push_back(scene);
    model.defaultScene = 0;

    // ── Escrita ───────────────────────────────────────────────────────────────
    tinygltf::TinyGLTF writer;
    bool binary = endsWithGlb(path);
    bool ok = binary
                  ? writer.WriteGltfSceneToFile(&model, path, true, true, false, true)
                  : writer.WriteGltfSceneToFile(&model, path, true, true, false, false);

    if (ok)
        std::cout << "GLTF salvo: " << path << "\n";
    else
        std::cerr << "GLTF: falha ao salvar " << path << "\n";
    return ok;
}
