#include "relief/uv_atlas.h"
#include <map>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <limits>
#include <functional>

namespace {

constexpr double kPi = 3.14159265358979323846;

// ─── 3D-edge adjacency (shared by island detection and seam baking) ──────────

struct EdgeRef {
    int face;
    int vAtFirst;  // mesh vertex index whose canonical position == the edge key's smaller id
    int vAtSecond; // mesh vertex index whose canonical position == the edge key's larger id
};
using EdgeMap = std::map<std::pair<int, int>, std::vector<EdgeRef>>;

// Welds vertices that share (approximately) the same 3D position, so faces split
// across a UV seam can still be recognized as 3D-adjacent.
std::vector<int> computeCanonicalPositions(const QEMSimplifier& mesh) {
    Eigen::Vector3d bmin(1e18, 1e18, 1e18), bmax(-1e18, -1e18, -1e18);
    bool any = false;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        bmin = bmin.cwiseMin(v.pos);
        bmax = bmax.cwiseMax(v.pos);
        any = true;
    }
    double diag = any ? (bmax - bmin).norm() : 1.0;
    if (diag < 1e-12) diag = 1.0;
    double eps = diag * 1e-6;

    struct Key {
        int64_t x, y, z;
        bool operator<(const Key& o) const { return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); }
    };
    auto quant = [&](double v) { return (int64_t)std::llround(v / eps); };

    std::map<Key, int> posToId;
    std::vector<int> canon(mesh.vertices.size(), -1);
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        if (mesh.vertices[i].removed) continue;
        Key k{quant(mesh.vertices[i].pos.x()), quant(mesh.vertices[i].pos.y()), quant(mesh.vertices[i].pos.z())};
        auto it = posToId.find(k);
        if (it == posToId.end()) {
            int id = (int)posToId.size();
            posToId[k] = id;
            canon[i] = id;
        } else {
            canon[i] = it->second;
        }
    }
    return canon;
}

EdgeMap buildEdgeMap(const QEMSimplifier& mesh, const std::vector<int>& canon) {
    EdgeMap edgeMap;
    for (int f = 0; f < (int)mesh.faces.size(); f++) {
        const auto& face = mesh.faces[f];
        if (face.removed) continue;
        for (int k = 0; k < 3; k++) {
            int va = face.v[k], vb = face.v[(k + 1) % 3];
            int ca = canon[va], cb = canon[vb];
            if (ca < 0 || cb < 0 || ca == cb) continue;
            if (ca < cb) edgeMap[{ca, cb}].push_back({f, va, vb});
            else         edgeMap[{cb, ca}].push_back({f, vb, va});
        }
    }
    return edgeMap;
}

double pointSegmentDistance(const Eigen::Vector2d& p, const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
    Eigen::Vector2d ab = b - a;
    double len2 = ab.squaredNorm();
    double t = len2 > 1e-18 ? (p - a).dot(ab) / len2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    Eigen::Vector2d proj = a + t * ab;
    return (p - proj).norm();
}

// Rasterizes the band of texels near segment [p0,p1] (in "this" island's UV space),
// writing the per-texel translation that maps each texel's own UV position into the
// neighboring island via the rigid transform (R, t). Nearest-seam-wins: only
// overwrites a texel if this segment is closer than whatever previously claimed it.
void rasterizeBand(
    const Eigen::Vector2d& p0, const Eigen::Vector2d& p1,
    double theta, const Eigen::Matrix2d& R, const Eigen::Vector2d& t,
    int width, int height, double bandWidthUV,
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

} // namespace

namespace UVAtlas {

std::vector<int> detectIslands(const QEMSimplifier& mesh) {
    int nf = (int)mesh.faces.size();
    std::vector<int> island(nf, -1);
    if (nf == 0) return island;

    std::vector<int> canon = computeCanonicalPositions(mesh);
    EdgeMap edgeMap = buildEdgeMap(mesh, canon);

    std::vector<int> parent(nf);
    for (int i = 0; i < nf; i++) parent[i] = i;
    std::function<int(int)> find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) {
        a = find(a); b = find(b);
        if (a != b) parent[a] = b;
    };

    constexpr double kUVEps2 = 1e-10;
    for (const auto& [key, refs] : edgeMap) {
        if (refs.size() != 2) continue; // boundary or non-manifold edge: no weld across it
        const EdgeRef& e0 = refs[0];
        const EdgeRef& e1 = refs[1];
        if (mesh.faces[e0.face].removed || mesh.faces[e1.face].removed) continue;

        bool uvMatch =
            (mesh.vertices[e0.vAtFirst].uv  - mesh.vertices[e1.vAtFirst].uv ).squaredNorm() < kUVEps2 &&
            (mesh.vertices[e0.vAtSecond].uv - mesh.vertices[e1.vAtSecond].uv).squaredNorm() < kUVEps2;

        if (uvMatch) unite(e0.face, e1.face);
    }

    std::map<int, int> rootToId;
    for (int f = 0; f < nf; f++) {
        if (mesh.faces[f].removed) continue;
        int r = find(f);
        auto it = rootToId.find(r);
        if (it == rootToId.end()) {
            int id = (int)rootToId.size();
            rootToId[r] = id;
            island[f] = id;
        } else {
            island[f] = it->second;
        }
    }
    return island;
}

OffsetMapResult bakeOffsetMap(
    const QEMSimplifier& mesh,
    const std::vector<int>& faceIsland,
    int width, int height,
    int seamBandTexels) {
    OffsetMapResult result;
    result.width = width;
    result.height = height;
    result.data.assign((size_t)width * height * 4, 0.0f);

    if (width <= 0 || height <= 0 || mesh.faces.empty()) return result;

    std::vector<float> distBuf((size_t)width * height, std::numeric_limits<float>::max());
    double bandWidthUV = (double)std::max(1, seamBandTexels) / (double)std::min(width, height);

    std::vector<int> canon = computeCanonicalPositions(mesh);
    EdgeMap edgeMap = buildEdgeMap(mesh, canon);

    for (const auto& [key, refs] : edgeMap) {
        if (refs.size() != 2) continue;
        const EdgeRef& e0 = refs[0];
        const EdgeRef& e1 = refs[1];
        if (mesh.faces[e0.face].removed || mesh.faces[e1.face].removed) continue;

        int islandA = faceIsland[e0.face];
        int islandB = faceIsland[e1.face];
        if (islandA < 0 || islandB < 0 || islandA == islandB) continue; // not a cross-island seam

        Eigen::Vector2d uvA0 = mesh.vertices[e0.vAtFirst].uv,  uvA1 = mesh.vertices[e0.vAtSecond].uv;
        Eigen::Vector2d uvB0 = mesh.vertices[e1.vAtFirst].uv,  uvB1 = mesh.vertices[e1.vAtSecond].uv;

        Eigen::Vector2d dirA = uvA1 - uvA0;
        Eigen::Vector2d dirB = uvB1 - uvB0;
        if (dirA.norm() < 1e-9 || dirB.norm() < 1e-9) continue;

        double angA = std::atan2(dirA.y(), dirA.x());
        double angB = std::atan2(dirB.y(), dirB.x());
        double theta = angB - angA;

        Eigen::Matrix2d R;
        R << std::cos(theta), -std::sin(theta),
             std::sin(theta),  std::cos(theta);
        Eigen::Vector2d t = uvB0 - R * uvA0;

        // Band on island A's side: jump A -> B
        rasterizeBand(uvA0, uvA1, theta, R, t, width, height, bandWidthUV, result.data, distBuf);

        // Band on island B's side: jump B -> A (inverse transform)
        Eigen::Matrix2d Rinv = R.transpose();
        Eigen::Vector2d tInv = uvA0 - Rinv * uvB0;
        rasterizeBand(uvB0, uvB1, -theta, Rinv, tInv, width, height, bandWidthUV, result.data, distBuf);
    }

    return result;
}

} // namespace UVAtlas
