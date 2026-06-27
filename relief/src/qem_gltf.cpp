#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "relief/qem.h"
#include <iostream>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static bool endsWithGlb(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".glb";
}

// Retorna ponteiro tipado para o início dos dados de um accessor
template<typename T>
static const T* accessorData(const tinygltf::Model& model, int accessorIdx) {
    const auto& acc  = model.accessors[accessorIdx];
    const auto& view = model.bufferViews[acc.bufferView];
    const auto& buf  = model.buffers[view.buffer];
    return reinterpret_cast<const T*>(
        buf.data.data() + view.byteOffset + acc.byteOffset);
}

// Copia a imagem GLTF de uma textura para um buffer RGBA8 linear.
static bool extractTexture(const tinygltf::Model& model, int texIdx,
                            std::vector<uint8_t>& outData, int& outW, int& outH) {
    if (texIdx < 0 || texIdx >= (int)model.textures.size()) return false;
    int imgIdx = model.textures[texIdx].source;
    if (imgIdx < 0 || imgIdx >= (int)model.images.size()) return false;
    const auto& img = model.images[imgIdx];
    if (img.image.empty() || img.width <= 0 || img.height <= 0) return false;

    outW = img.width;
    outH = img.height;
    int comp = img.component; // 3=RGB, 4=RGBA
    outData.resize((size_t)img.width * img.height * 4);
    for (int p = 0; p < img.width * img.height; p++) {
        outData[p*4+0] = img.image[p*comp+0];
        outData[p*4+1] = img.image[p*comp+1];
        outData[p*4+2] = img.image[p*comp+2];
        outData[p*4+3] = (comp == 4) ? img.image[p*comp+3] : 255;
    }
    return true;
}

// ─── loadGLTF ────────────────────────────────────────────────────────────────

bool QEMSimplifier::loadGLTF(const std::string& path) {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = endsWithGlb(path)
        ? loader.LoadBinaryFromFile(&model, &err, &warn, path)
        : loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if (!warn.empty()) std::cerr << "GLTF warn: " << warn << "\n";
    if (!ok) { std::cerr << "GLTF erro: " << err << "\n"; return false; }

    vertices.clear();
    faces.clear();

    textureData.clear();
    textureWidth = textureHeight = 0;
    normalTextureData.clear();
    normalTextureWidth = normalTextureHeight = 0;

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
                prim.mode != -1) continue; // -1 = padrão (triângulos)

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;

            const auto& posAcc = model.accessors[posIt->second];
            if (posAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                posAcc.type != TINYGLTF_TYPE_VEC3) continue;

            const float* posPtr = accessorData<float>(model, posIt->second);
            const auto& posView = model.bufferViews[posAcc.bufferView];
            size_t posStride = posView.byteStride == 0
                ? 3 * sizeof(float)
                : posView.byteStride;

            int vertexOffset = (int)vertices.size();

            for (size_t i = 0; i < posAcc.count; i++) {
                const float* p = reinterpret_cast<const float*>(
                    reinterpret_cast<const uint8_t*>(posPtr) + i * posStride);
                Vertex v;
                v.pos = Eigen::Vector3d(p[0], p[1], p[2]);
                vertices.push_back(v);
            }

            // UV (TEXCOORD_0)
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                const auto& uvAcc = model.accessors[uvIt->second];
                if (uvAcc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
                    uvAcc.type == TINYGLTF_TYPE_VEC2) {
                    const float* uvPtr = accessorData<float>(model, uvIt->second);
                    const auto& uvView = model.bufferViews[uvAcc.bufferView];
                    size_t uvStride = uvView.byteStride == 0
                        ? 2 * sizeof(float)
                        : uvView.byteStride;
                    for (size_t i = 0; i < uvAcc.count && (vertexOffset + (int)i) < (int)vertices.size(); i++) {
                        const float* uv = reinterpret_cast<const float*>(
                            reinterpret_cast<const uint8_t*>(uvPtr) + i * uvStride);
                        vertices[vertexOffset + i].uv = Eigen::Vector2d(uv[0], uv[1]);
                    }
                }
            }

            if (prim.indices < 0) {
                // Sem buffer de índices: assume triângulos sequenciais
                for (size_t i = 0; i + 2 < posAcc.count; i += 3) {
                    Face f;
                    f.v[0] = vertexOffset + (int)i;
                    f.v[1] = vertexOffset + (int)i + 1;
                    f.v[2] = vertexOffset + (int)i + 2;
                    faces.push_back(f);
                }
            } else {
                const auto& idxAcc = model.accessors[prim.indices];
                size_t idxCount = idxAcc.count;

                auto addFaces = [&](auto* idx) {
                    for (size_t i = 0; i + 2 < idxCount; i += 3) {
                        Face f;
                        f.v[0] = vertexOffset + (int)idx[i];
                        f.v[1] = vertexOffset + (int)idx[i + 1];
                        f.v[2] = vertexOffset + (int)idx[i + 2];
                        faces.push_back(f);
                    }
                };

                switch (idxAcc.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    addFaces(accessorData<uint8_t>(model, prim.indices)); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    addFaces(accessorData<uint16_t>(model, prim.indices)); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    addFaces(accessorData<uint32_t>(model, prim.indices)); break;
                default:
                    std::cerr << "GLTF: tipo de índice não suportado\n"; break;
                }
            }
        }
    }

    // ── Extrai texturas de cor base e de normal (primeiro material que as tiver) ──
    {
        bool gotColor = false, gotNormal = false;
        for (const auto& mesh : model.meshes) {
            for (const auto& prim : mesh.primitives) {
                if (prim.material < 0 || prim.material >= (int)model.materials.size()) continue;
                const auto& mat = model.materials[prim.material];

                if (!gotColor && extractTexture(model, mat.pbrMetallicRoughness.baseColorTexture.index,
                                                 textureData, textureWidth, textureHeight)) {
                    gotColor = true;
                    std::cout << "GLTF textura de cor: " << textureWidth << "x" << textureHeight << "\n";
                }
                if (!gotNormal && extractTexture(model, mat.normalTexture.index,
                                                  normalTextureData, normalTextureWidth, normalTextureHeight)) {
                    gotNormal = true;
                    std::cout << "GLTF textura de normal: " << normalTextureWidth << "x" << normalTextureHeight << "\n";
                }
                if (gotColor && gotNormal) goto textures_done;
            }
        }
        textures_done:;
    }

    std::cout << "GLTF carregado: " << vertices.size()
              << " vértices, " << faces.size() << " faces\n";
    return !vertices.empty();
}

// ─── saveGLTF ────────────────────────────────────────────────────────────────

bool QEMSimplifier::saveGLTF(const std::string& path) const {
    // Compactar vértices (remover os marcados como removed)
    std::vector<int> remap(vertices.size(), -1);
    std::vector<float> positions;
    int newIdx = 0;
    for (int i = 0; i < (int)vertices.size(); i++) {
        if (!vertices[i].removed) {
            remap[i] = newIdx++;
            positions.push_back(vertices[i].pos.x());
            positions.push_back(vertices[i].pos.y());
            positions.push_back(vertices[i].pos.z());
        }
    }

    std::vector<uint32_t> indices;
    for (const auto& fc : faces) {
        if (fc.removed) continue;
        indices.push_back((uint32_t)remap[fc.v[0]]);
        indices.push_back((uint32_t)remap[fc.v[1]]);
        indices.push_back((uint32_t)remap[fc.v[2]]);
    }

    if (positions.empty() || indices.empty()) {
        std::cerr << "GLTF: malha vazia, nada a salvar\n";
        return false;
    }

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "QEM Simplifier";

    // ── Buffer único: [positions | indices] ──────────────────────────────────
    size_t posBytes = positions.size() * sizeof(float);
    size_t idxBytes = indices.size()   * sizeof(uint32_t);

    tinygltf::Buffer buf;
    buf.data.resize(posBytes + idxBytes);
    std::memcpy(buf.data.data(),            positions.data(), posBytes);
    std::memcpy(buf.data.data() + posBytes, indices.data(),   idxBytes);
    model.buffers.push_back(std::move(buf));

    // ── Buffer views ──────────────────────────────────────────────────────────
    tinygltf::BufferView posView;
    posView.buffer     = 0;
    posView.byteOffset = 0;
    posView.byteLength = posBytes;
    posView.target     = TINYGLTF_TARGET_ARRAY_BUFFER;
    model.bufferViews.push_back(posView);

    tinygltf::BufferView idxView;
    idxView.buffer     = 0;
    idxView.byteOffset = posBytes;
    idxView.byteLength = idxBytes;
    idxView.target     = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
    model.bufferViews.push_back(idxView);

    // ── Bounding box para o accessor de posições ──────────────────────────────
    float minX = positions[0], minY = positions[1], minZ = positions[2];
    float maxX = minX,         maxY = minY,         maxZ = minZ;
    for (size_t i = 0; i < positions.size(); i += 3) {
        minX = std::min(minX, positions[i]);
        minY = std::min(minY, positions[i+1]);
        minZ = std::min(minZ, positions[i+2]);
        maxX = std::max(maxX, positions[i]);
        maxY = std::max(maxY, positions[i+1]);
        maxZ = std::max(maxZ, positions[i+2]);
    }

    // ── Accessors ──────────────────────────────────────────────────────────────
    tinygltf::Accessor posAcc;
    posAcc.bufferView    = 0;
    posAcc.byteOffset    = 0;
    posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    posAcc.count         = (size_t)newIdx;
    posAcc.type          = TINYGLTF_TYPE_VEC3;
    posAcc.minValues     = { (double)minX, (double)minY, (double)minZ };
    posAcc.maxValues     = { (double)maxX, (double)maxY, (double)maxZ };
    model.accessors.push_back(posAcc);

    tinygltf::Accessor idxAcc;
    idxAcc.bufferView    = 1;
    idxAcc.byteOffset    = 0;
    idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    idxAcc.count         = indices.size();
    idxAcc.type          = TINYGLTF_TYPE_SCALAR;
    model.accessors.push_back(idxAcc);

    // ── Mesh ──────────────────────────────────────────────────────────────────
    tinygltf::Primitive prim;
    prim.attributes["POSITION"] = 0;
    prim.indices = 1;
    prim.mode    = TINYGLTF_MODE_TRIANGLES;

    tinygltf::Mesh mesh;
    mesh.name = "simplified";
    mesh.primitives.push_back(prim);
    model.meshes.push_back(mesh);

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

    if (ok) std::cout << "GLTF salvo: " << path << "\n";
    else    std::cerr << "GLTF: falha ao salvar " << path << "\n";
    return ok;
}
