/**
 * @file mesh.cpp
 * @brief Mesh implementation: counts and edge-to-faces adjacency.
 */
#include "relief/mesh.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include "mesh/io.h"

namespace mesh {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ─── Offset-map baking helpers ───────────────────────────────────────────────

/// @return Shortest distance from point `p` to segment [a, b].
double pointSegmentDistance(const Eigen::Vector2d& p, const Eigen::Vector2d& a,
                            const Eigen::Vector2d& b) {
    Eigen::Vector2d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = len2 > 1e-18 ? (p - a).dot(ab) / len2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    Eigen::Vector2d proj = a + t * ab;
    return (p - proj).norm();
}

/**
 * @brief Computes 2D barycentric coordinates of (px, py) in triangle (a, b, c).
 * @param px,py Query point.
 * @param ax,ay,bx,by,cx,cy Triangle vertices.
 * @param[out] w0,w1,w2 Barycentric weights on success.
 * @return false if the triangle is degenerate (near-zero area).
 */
bool bary2D(double px, double py, double ax, double ay, double bx, double by, double cx, double cy,
            double& w0, double& w1, double& w2) {
    double denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::abs(denom) < 1e-10) return false;
    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denom;
    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denom;
    w2 = 1.0 - w0 - w1;
    return true;
}

/**
 * @brief Rasterizes the band of texels near segment [p0,p1] (in "this"
 *        island's UV space), writing the per-texel translation that maps
 *        each texel's own UV position into the neighboring island via the
 *        rigid transform (R, t). Nearest-seam-wins: only overwrites a texel
 *        if this segment is closer than whatever previously claimed it.
 *
 *        Texels that `islandAt` says are already covered by `islandId`
 *        itself are skipped — the band must not eat into the island's own
 *        valid footprint (see Mesh::buildOffsetMap).
 * @param p0,p1 Seam segment endpoints, in UV space.
 * @param theta Rotation (radians) encoded into the texel's z channel (as turns).
 * @param R,t Rigid transform mapping a UV point on this segment to the neighbor island.
 * @param islandId Island id this band is being written for (the "this" island).
 * @param islandAt Per-texel island coverage, from Mesh::buildOffsetMap.
 * @param width,height Offset map dimensions.
 * @param bandWidthUV Half-width of the band, in UV units.
 * @param[out] outData Offset map RGBA buffer being written into.
 * @param[in,out] distBuf Per-texel nearest-seam distance, used for the nearest-seam-wins test.
 */
void rasterizeBand(const Eigen::Vector2d& p0, const Eigen::Vector2d& p1, double theta,
                   const Eigen::Matrix2d& R, const Eigen::Vector2d& t, int islandId,
                   const std::vector<int>& islandAt, int width, int height, double bandWidthUV,
                   std::vector<float>& outData, std::vector<float>& distBuf) {
    double minU = std::min(p0.x(), p1.x()) - bandWidthUV;
    double maxU = std::max(p0.x(), p1.x()) + bandWidthUV;
    double minV = std::min(p0.y(), p1.y()) - bandWidthUV;
    double maxV = std::max(p0.y(), p1.y()) + bandWidthUV;

    int ix0 = std::max(0, (int)std::floor(minU * width));
    int ix1 = std::min(width - 1, (int)std::ceil(maxU * width));
    int iy0 = std::max(0, (int)std::floor(minV * height));
    int iy1 = std::min(height - 1, (int)std::ceil(maxV * height));
    if (ix0 > ix1 || iy0 > iy1) return;

    float thetaTurns = (float)(theta / (2.0 * kPi));

    for (int iy = iy0; iy <= iy1; iy++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            Eigen::Vector2d p((ix + 0.5) / width, (iy + 0.5) / height);
            double dist = pointSegmentDistance(p, p0, p1);
            if (dist > bandWidthUV) continue;

            size_t idx = (size_t)iy * width + ix;
            if (islandAt[idx] == islandId) continue;
            if (dist >= distBuf[idx]) continue;
            distBuf[idx] = (float)dist;

            Eigen::Vector2d mapped = R * p + t;
            Eigen::Vector2d offset = mapped - p;
            outData[idx * 4 + 0] = (float)offset.x();
            outData[idx * 4 + 1] = (float)offset.y();
            outData[idx * 4 + 2] = thetaTurns;
            outData[idx * 4 + 3] = 1.0f;
        }
    }
}

}  // namespace

Mesh::Mesh(const std::string &path) {
    if (!io::loadMesh(*this, path)) throw std::runtime_error("failed to load mesh: " + path);
}

bool Mesh::save(const std::string &path) const { return io::saveMesh(*this, path); }

void Mesh::replaceGeometry(std::vector<Vertex> newVertices, std::vector<Wedge> newWedges,
                           std::vector<Face> newFaces) {
    vertices_ = std::move(newVertices);
    wedges_ = std::move(newWedges);
    faces_ = std::move(newFaces);
    computeIslands();
}

int Mesh::faceCount() const {
    int n = 0;
    for (auto &f : faces_)
        if (!f.removed) n++;
    return n;
}

int Mesh::vertexCount() const {
    int n = 0;
    for (auto &v : vertices_)
        if (!v.removed) n++;
    return n;
}

const Wedge &Mesh::faceWedge(const Face &f, int cornerIdx) const {
    return wedges_[f.w[cornerIdx]];
}

const Vertex &Mesh::faceVertex(const Face &f, int cornerIdx) const {
    return vertices_[faceWedge(f, cornerIdx).vertex];
}

std::array<Vertex, 3> Mesh::faceVertices(const Face &f) const {
    return {faceVertex(f, 0), faceVertex(f, 1), faceVertex(f, 2)};
}

Eigen::Vector3d Mesh::faceNormal(const Face &f) const {
    const Eigen::Vector3d &p0 = faceVertex(f, 0).pos;
    const Eigen::Vector3d &p1 = faceVertex(f, 1).pos;
    const Eigen::Vector3d &p2 = faceVertex(f, 2).pos;
    Eigen::Vector3d n = (p1 - p0).cross(p2 - p0);
    double len = n.norm();
    if (len == 0.0) return Eigen::Vector3d::Zero();
    return n / len;
}

std::vector<int> Mesh::getOutwardingFaces(const Eigen::Vector3d &axis) const {
    std::vector<int> result;
    for (int fi = 0; fi < (int)faces_.size(); fi++) {
        if (faces_[fi].removed) continue;
        if (faceNormal(faces_[fi]).dot(axis) > 0.0) result.push_back(fi);
    }
    return result;
}

void Mesh::bake(int resolution) {
    Eigen::Vector3d min = Eigen::Vector3d::Constant(std::numeric_limits<double>::max());
    Eigen::Vector3d max = Eigen::Vector3d::Constant(std::numeric_limits<double>::lowest());
    for (const Vertex &v : vertices_) {
        if (v.removed) continue;
        min = min.cwiseMin(v.pos);
        max = max.cwiseMax(v.pos);
    }
    const Eigen::Vector3d center = 0.5 * (min + max);
    const Eigen::Vector3d half = 0.5 * (max - min);

    // Smooth vertex normals: area-weighted average of the incident face normals.
    std::vector<Eigen::Vector3d> vertexNormals(vertices_.size(), Eigen::Vector3d::Zero());
    for (const Face &f : faces_) {
        if (f.removed) continue;
        const int i0 = wedges_[f.w[0]].vertex, i1 = wedges_[f.w[1]].vertex, i2 = wedges_[f.w[2]].vertex;
        const Eigen::Vector3d n =
            (vertices_[i1].pos - vertices_[i0].pos).cross(vertices_[i2].pos - vertices_[i0].pos);
        vertexNormals[i0] += n;
        vertexNormals[i1] += n;
        vertexNormals[i2] += n;
    }

    auto cross = [](const Eigen::Vector2d &a, const Eigen::Vector2d &b) {
        return a.x() * b.y() - a.y() * b.x();
    };

    for (int axis = 0; axis < 3; axis++) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;

        for (int side = 0; side < 2; side++) {
            // Planes are ordered X+, X-, Y+, Y-, Z+, Z-.
            planes_[axis * 2 + (side ? 0 : 1)].emplace(resolution, resolution, tex::PixelFormat::RGBA16);
            normalPlanes_[axis * 2 + (side ? 0 : 1)].emplace(resolution, resolution,
                                                             tex::PixelFormat::RGBA16);
        }

        // A flat box has no area to bake on this axis.
        if (half[u] <= 0.0 || half[v] <= 0.0) continue;

        for (int side = 0; side < 2; side++) {
            const double sign = side ? 1.0 : -1.0;
            uint16_t *plane = reinterpret_cast<uint16_t *>(planes_[axis * 2 + (side ? 0 : 1)]->data());
            uint16_t *normalPlane = reinterpret_cast<uint16_t *>(normalPlanes_[axis * 2 + (side ? 0 : 1)]->data());
            const double thickness = 2 * half[axis];
            std::vector<float> depth((size_t)resolution * resolution,
                                     std::numeric_limits<float>::infinity());

            for (int fi : getOutwardingFaces(sign * Eigen::Vector3d::Unit(axis))) {
                const Face &f = faces_[fi];
                std::array<Eigen::Vector2d, 3> t, uv;
                std::array<double, 3> d;
                std::array<Eigen::Vector3d, 3> nrm;
                for (int i = 0; i < 3; i++) {
                    const Wedge &wedge = wedges_[f.w[i]];
                    const Eigen::Vector3d p = vertices_[wedge.vertex].pos - center;
                    t[i] = Eigen::Vector2d((p[u] + half[u]) / (2 * half[u]) * resolution,
                                           (p[v] + half[v]) / (2 * half[v]) * resolution);
                    // Distance from the plane: smaller is nearer the viewer.
                    d[i] = half[axis] - sign * p[axis];
                    uv[i] = wedge.uv;
                    nrm[i] = vertexNormals[wedge.vertex];
                }

                const double area = cross(t[1] - t[0], t[2] - t[0]);
                if (std::abs(area) <= 1e-12) continue;

                const int minX = std::max(0, (int)std::floor(std::min({t[0].x(), t[1].x(), t[2].x()})));
                const int maxX = std::min(resolution - 1, (int)std::ceil(std::max({t[0].x(), t[1].x(), t[2].x()})));
                const int minY = std::max(0, (int)std::floor(std::min({t[0].y(), t[1].y(), t[2].y()})));
                const int maxY = std::min(resolution - 1, (int)std::ceil(std::max({t[0].y(), t[1].y(), t[2].y()})));

                for (int y = minY; y <= maxY; y++) {
                    for (int x = minX; x <= maxX; x++) {
                        const Eigen::Vector2d c(x + 0.5, y + 0.5);
                        const double l0 = cross(t[1] - c, t[2] - c) / area;
                        const double l1 = cross(t[2] - c, t[0] - c) / area;
                        const double l2 = cross(t[0] - c, t[1] - c) / area;
                        if (l0 < -1e-4 || l1 < -1e-4 || l2 < -1e-4) continue;

                        const size_t texel = (size_t)y * resolution + x;
                        const float z = (float)(l0 * d[0] + l1 * d[1] + l2 * d[2]);
                        if (z >= depth[texel]) continue;
                        depth[texel] = z;

                        const Eigen::Vector2d value = l0 * uv[0] + l1 * uv[1] + l2 * uv[2];
                        for (int k = 0; k < 2; k++) {
                            plane[texel * 4 + k] =
                                (uint16_t)std::lround(std::clamp(value[k], 0.0, 1.0) * 65535.0);
                        }

                        Eigen::Vector3d n = l0 * nrm[0] + l1 * nrm[1] + l2 * nrm[2];
                        if (n.squaredNorm() > 1e-24) n.normalize();
                        else n = sign * Eigen::Vector3d::Unit(axis);
                        for (int k = 0; k < 3; k++) {
                            normalPlane[texel * 4 + k] =
                                (uint16_t)std::lround((0.5 * n[k] + 0.5) * 65535.0);
                        }
                        normalPlane[texel * 4 + 3] = 65535;
                    }
                }
            }

            // Depth goes in B and the silhouette mask in A, only for covered texels.
            for (size_t texel = 0; texel < depth.size(); texel++) {
                if (std::isinf(depth[texel])) continue;
                const double d = thickness > 0.0 ? std::clamp(depth[texel] / thickness, 0.0, 1.0) : 0.0;
                plane[texel * 4 + 2] = (uint16_t)std::lround(d * 65535.0);
                plane[texel * 4 + 3] = 65535;
            }
        }
    }
}

std::array<Edge, 3> Mesh::faceEdges(const Face &f) const {
    int v0 = wedges_[f.w[0]].vertex, v1 = wedges_[f.w[1]].vertex, v2 = wedges_[f.w[2]].vertex;
    return {Edge(v0, v1), Edge(v1, v2), Edge(v2, v0)};
}

std::map<Edge, std::vector<int>> Mesh::buildEdgeToFaces() const {
    std::map<Edge, std::vector<int>> edgeToFaces;
    for (int fi = 0; fi < (int)faces_.size(); fi++) {
        if (faces_[fi].removed) continue;
        for (const Edge &e : faceEdges(faces_[fi])) edgeToFaces[e].push_back(fi);
    }
    return edgeToFaces;
}

std::vector<std::set<int>> Mesh::buildVertexToVertices() const {
    std::vector<std::set<int>> vertexToVertices(vertices_.size());
    for (const auto &entry : buildEdgeToFaces()) {
        const auto &edge = entry.first;
        vertexToVertices[edge.first].insert(edge.second);
        vertexToVertices[edge.second].insert(edge.first);
    }
    return vertexToVertices;
}

void Mesh::addQuadric(int index, const Eigen::Matrix4d &Q) { vertices_[index].Q += Q; }

void Mesh::clearQuadrics() {
    for (auto &v : vertices_) v.Q.setZero();
}

void Mesh::mergeVertices(
    int keep, int remove, const Eigen::Vector3d &pos,
    const std::vector<std::tuple<int, int, Eigen::Vector2d>> &uvTargets) {
    Vertex &kv = vertices_[keep];
    Vertex &rv = vertices_[remove];

    kv.pos = pos;
    kv.Q += rv.Q;

    // Each uvTarget pairing collapses onto one surviving wedge (wKeep): if
    // wRemove stayed a separate (but now identical-valued) wedge, faces on
    // either side of the old edge would explode into two distinct GPU
    // vertices at the same spot (explodeForGPU dedups by wedge id, not
    // value), each only accumulating its own half of the normal -- faceting
    // the shading right along every collapsed edge. So faces still pointing
    // at wRemove are repointed at wKeep below, folded into the face pass
    // already needed for degeneracy checking.
    std::map<int, int> wedgeRemap;  // wRemove -> wKeep
    for (auto &[wKeep, wRemove, mergedUV] : uvTargets) {
        wedges_[wKeep].uv = mergedUV;
        wedgeRemap[wRemove] = wKeep;
    }

    // Any other wedge still belonging to `remove` (parts of its fan that
    // don't touch this edge) simply moves to `keep`, UV unchanged.
    for (auto &wg : wedges_)
        if (wg.vertex == remove) wg.vertex = keep;

    rv.removed = true;

    // Repoint faces off any merged-away wedge, and mark now-degenerate faces
    // (two corners collapsed onto the same vertex) removed.
    for (auto &fc : faces_) {
        if (fc.removed) continue;
        if (!wedgeRemap.empty())
            for (int i = 0; i < 3; i++) {
                auto it = wedgeRemap.find(fc.w[i]);
                if (it != wedgeRemap.end()) fc.w[i] = it->second;
            }
        int a = wedges_[fc.w[0]].vertex;
        int b = wedges_[fc.w[1]].vertex;
        int c = wedges_[fc.w[2]].vertex;
        if (a == b || b == c || a == c) fc.removed = true;
    }
}

void Mesh::moveVertex(int index, const Eigen::Vector3d &pos) {
    if (!vertices_[index].removed) vertices_[index].pos = pos;
}

Mesh::GPUMesh Mesh::explodeForGPU() const {
    GPUMesh out;
    std::vector<int> wedgeToIdx(wedges_.size(), -1);
    out.indices.reserve(faces_.size() * 3);
    for (const auto &f : faces_) {
        if (f.removed) continue;
        for (int k = 0; k < 3; k++) {
            int wi = f.w[k];
            if (wedgeToIdx[wi] < 0) {
                wedgeToIdx[wi] = (int)out.positions.size();
                out.positions.push_back(vertices_[wedges_[wi].vertex].pos);
                out.uvs.push_back(wedges_[wi].uv);
            }
            out.indices.push_back((uint32_t)wedgeToIdx[wi]);
        }
    }

    // Normals and tangents are accumulated per exploded vertex from the faces sharing it.
    const size_t n = out.positions.size();
    std::vector<Eigen::Vector3d> normals(n, Eigen::Vector3d::Zero());
    std::vector<Eigen::Vector3d> tangents(n, Eigen::Vector3d::Zero());
    std::vector<Eigen::Vector3d> bitangents(n, Eigen::Vector3d::Zero());

    for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {
        const uint32_t i0 = out.indices[i], i1 = out.indices[i + 1], i2 = out.indices[i + 2];
        const Eigen::Vector3d e1 = out.positions[i1] - out.positions[i0];
        const Eigen::Vector3d e2 = out.positions[i2] - out.positions[i0];
        const Eigen::Vector2d d1 = out.uvs[i1] - out.uvs[i0];
        const Eigen::Vector2d d2 = out.uvs[i2] - out.uvs[i0];

        const Eigen::Vector3d fn = e1.cross(e2);
        normals[i0] += fn;
        normals[i1] += fn;
        normals[i2] += fn;

        const double det = d1.x() * d2.y() - d2.x() * d1.y();
        if (std::abs(det) > 1e-12) {
            const double r = 1.0 / det;
            const Eigen::Vector3d T = r * (d2.y() * e1 - d1.y() * e2);
            const Eigen::Vector3d B = r * (d1.x() * e2 - d2.x() * e1);
            tangents[i0] += T;
            tangents[i1] += T;
            tangents[i2] += T;
            bitangents[i0] += B;
            bitangents[i1] += B;
            bitangents[i2] += B;
        }
    }

    out.normals.resize(n);
    out.tangents.resize(n);
    for (size_t i = 0; i < n; i++) {
        const Eigen::Vector3d normal = normals[i].normalized();
        out.normals[i] = normal;

        // Gram-Schmidt against the normal.
        Eigen::Vector3d t = tangents[i] - normal * normal.dot(tangents[i]);
        const double len = t.norm();
        t = len > 1e-8 ? Eigen::Vector3d(t / len) : Eigen::Vector3d(1.0, 0.0, 0.0);

        // Per-vertex handedness (Lengyel): the accumulated tangent only fixes T up to sign,
        // so cross(N, T) must be compared with the real bitangent from the UV gradient.
        // Otherwise the "V" axis of the tangent frame silently flips for meshes whose UV
        // winding differs (e.g. an OBJ mesh, whose V axis is flipped on load vs. glTF's).
        const double handedness = (normal.cross(t).dot(bitangents[i]) < 0.0) ? -1.0 : 1.0;
        out.tangents[i] = Eigen::Vector4d(t.x(), t.y(), t.z(), handedness);
    }
    return out;
}

Eigen::Vector2d Mesh::vertexUV(int face, int vertexId) const {
    const Face &f = faces_[face];
    for (int k = 0; k < 3; k++) {
        if (wedges_[f.w[k]].vertex == vertexId) return wedges_[f.w[k]].uv;
    }
    return Eigen::Vector2d::Zero();
}

void Mesh::logSummary() const {
    std::cout << "Mesh: " << vertexCount() << " vertices, " << wedges_.size() << " wedges, "
              << faceCount() << " faces\n";
    if (colorTexture_)
        std::cout << "  color texture: " << colorTexture_->width() << "x" << colorTexture_->height()
                  << "\n";
    if (normalTexture_)
        std::cout << "  normal texture: " << normalTexture_->width() << "x" << normalTexture_->height()
                  << "\n";
}

void Mesh::computeIslands() {
    auto edgeToFaces = buildEdgeToFaces();
    int nf = (int)faces_.size();
    for (auto &f : faces_) f.island = -1;
    if (nf == 0) return;

    std::vector<int> parent(nf);
    for (int i = 0; i < nf; i++) parent[i] = i;
    std::function<int(int)> find = [&](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    constexpr double kUVEps2 = 1e-10;
    for (const auto &[key, faceIds] : edgeToFaces) {
        if (faceIds.size() != 2) continue;  // boundary or non-manifold edge: no weld across it
        int f0 = faceIds[0], f1 = faceIds[1];
        bool uvMatch =
            (vertexUV(f0, key.first) - vertexUV(f1, key.first)).squaredNorm() < kUVEps2 &&
            (vertexUV(f0, key.second) - vertexUV(f1, key.second)).squaredNorm() < kUVEps2;
        if (uvMatch) {
            int a = find(f0), b = find(f1);
            if (a != b) parent[a] = b;
        }
    }

    std::map<int, int> rootToId;
    for (int fi = 0; fi < nf; fi++) {
        if (faces_[fi].removed) continue;
        auto [it, inserted] = rootToId.emplace(find(fi), (int)rootToId.size());
        faces_[fi].island = it->second;
    }
}

std::vector<Edge> Mesh::findSeamEdges() const {
    auto edgeToFaces = buildEdgeToFaces();

    std::vector<Edge> seams;
    for (const auto& [key, faceIds] : edgeToFaces) {
        if (faceIds.size() != 2) continue;  // boundary or non-manifold: not a seam between islands.
        int f0 = faceIds[0], f1 = faceIds[1];
        if (faces_[f0].removed || faces_[f1].removed) continue;

        int islandA = faces_[f0].island;
        int islandB = faces_[f1].island;
        if (islandA < 0 || islandB < 0 || islandA == islandB) continue;

        seams.push_back(key);
    }
    return seams;
}

textures::MipPyramid Mesh::buildOffsetMap(int width, int height, int seamBandTexels) const {
    auto edgeToFaces = buildEdgeToFaces();

    std::vector<float> data((size_t)width * height * 4, 0.0f);

    if (width <= 0 || height <= 0 || faces_.empty()) {
        textures::MipPyramid pyr;
        pyr.width = width;
        pyr.height = height;
        pyr.channels = 4;
        pyr.mips.push_back(std::move(data));
        return pyr;
    }

    std::vector<float> distBuf((size_t)width * height, std::numeric_limits<float>::max());
    double bandWidthUV = (double)std::max(1, seamBandTexels) / (double)std::min(width, height);

    // Rasterize every active face's UV triangle onto the width x height grid
    // (texel-center sampling), recording which island truly covers each texel
    // (-1 if none). This is the ground truth for "is this texel part of the
    // island's own valid footprint" — the same barycentric test HeightmapBaker
    // uses to decide which texels get real relief data. rasterizeBand uses it
    // instead of approximating the footprint from a single seam edge's local
    // geometry, which breaks down near seam vertices where consecutive edges
    // meet at an angle (very common after mesh simplification): the true
    // boundary bends away from either edge's infinite line there, so a
    // per-edge half-plane test would misclassify texels still inside the
    // island as "outside", letting the leap band bleed into the island.
    std::vector<int> islandAt((size_t)width * height, -1);
    for (const Face& f : faces_) {
        if (f.removed || f.island < 0) continue;

        Eigen::Vector2d uv0 = wedges_[f.w[0]].uv;
        Eigen::Vector2d uv1 = wedges_[f.w[1]].uv;
        Eigen::Vector2d uv2 = wedges_[f.w[2]].uv;
        double u0 = uv0.x() * width, v0 = uv0.y() * height;
        double u1 = uv1.x() * width, v1 = uv1.y() * height;
        double u2 = uv2.x() * width, v2 = uv2.y() * height;

        int minX = std::max(0, (int)std::floor(std::min({u0, u1, u2})));
        int maxX = std::min(width - 1, (int)std::ceil(std::max({u0, u1, u2})));
        int minY = std::max(0, (int)std::floor(std::min({v0, v1, v2})));
        int maxY = std::min(height - 1, (int)std::ceil(std::max({v0, v1, v2})));

        for (int py = minY; py <= maxY; py++) {
            for (int px = minX; px <= maxX; px++) {
                double w0, w1, w2;
                if (!bary2D(px + 0.5, py + 0.5, u0, v0, u1, v1, u2, v2, w0, w1, w2)) continue;
                if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;
                islandAt[(size_t)py * width + px] = f.island;
            }
        }
    }

    for (const auto& [key, faceIds] : edgeToFaces) {
        if (faceIds.size() != 2) continue;
        int f0 = faceIds[0], f1 = faceIds[1];
        if (faces_[f0].removed || faces_[f1].removed) continue;

        int islandA = faces_[f0].island;
        int islandB = faces_[f1].island;
        if (islandA < 0 || islandB < 0 || islandA == islandB) continue;  // not a cross-island seam

        Eigen::Vector2d uvA0 = vertexUV(f0, key.first), uvA1 = vertexUV(f0, key.second);
        Eigen::Vector2d uvB0 = vertexUV(f1, key.first), uvB1 = vertexUV(f1, key.second);

        Eigen::Vector2d dirA = uvA1 - uvA0;
        Eigen::Vector2d dirB = uvB1 - uvB0;
        double lenA = dirA.norm(), lenB = dirB.norm();
        if (lenA < 1e-9 || lenB < 1e-9) continue;

        double angA = std::atan2(dirA.y(), dirA.x());
        double angB = std::atan2(dirB.y(), dirB.x());
        double theta = angB - angA;

        Eigen::Matrix2d Rot;
        Rot << std::cos(theta), -std::sin(theta), std::sin(theta), std::cos(theta);
        Eigen::Matrix2d RotInv = Rot.transpose();  // = Rot(-theta)

        // Islands aren't guaranteed to share the same UV texel density along
        // a seam edge (e.g. independently-scaled unwrap charts), so the
        // transform is a similarity (rotation + uniform scale), not a pure
        // rigid rotation: scaleAB maps island A's edge length onto island
        // B's. Without it, only the anchor vertex (uvA0 -> uvB0) lands
        // exactly and the far vertex of the seam edge drifts by
        // |dirA| - |dirB|, leaving a thin gap/overlap along the leap.
        double scaleAB = lenB / lenA;  // A -> B
        double scaleBA = lenA / lenB;  // B -> A (inverse of scaleAB)

        Eigen::Matrix2d R = scaleAB * Rot;
        Eigen::Vector2d t = uvB0 - R * uvA0;

        // Band on island A's side: jump A -> B.
        // The baked angle is negated relative to theta because the shader's
        // rotateXY(v, angle) implements R(-angle) (it mirrors RTMA_Functions.ush's
        // mul(v, RotationMatrix) row-vector convention), while the position map
        // above needs the direction vector rotated by R(+theta) to stay consistent
        // with the position transform — so the encoded angle must be -theta.
        rasterizeBand(uvA0, uvA1, -theta, R, t, islandA, islandAt, width, height, bandWidthUV, data,
                      distBuf);

        // Band on island B's side: jump B -> A (inverse transform, hence +theta).
        Eigen::Matrix2d Rinv = scaleBA * RotInv;
        Eigen::Vector2d tInv = uvA0 - Rinv * uvB0;
        rasterizeBand(uvB0, uvB1, theta, Rinv, tInv, islandB, islandAt, width, height, bandWidthUV,
                      data, distBuf);
    }

    textures::MipPyramid pyr;
    pyr.width = width;
    pyr.height = height;
    pyr.channels = 4;
    pyr.mips.push_back(std::move(data));
    return pyr;
}

}  // namespace mesh
