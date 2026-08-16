/**
 * @file uv_atlas.cpp
 * @brief UV-island detection (union-find over 3D-edge-adjacent, UV-matching
 *        faces) and Offset_Map baking (per-texel cross-seam leap transforms).
 */
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

/// One face's reference to a shared 3D edge, keyed by the edge's canonical vertex ids.
struct EdgeRef {
    int face;
    int vAtFirst;  ///< Mesh vertex index whose canonical position == the edge key's smaller id.
    int vAtSecond; ///< Mesh vertex index whose canonical position == the edge key's larger id.
};
/// Maps a canonical (small id, large id) edge key to every face referencing it.
using EdgeMap = std::map<std::pair<int, int>, std::vector<EdgeRef>>;

/// Welds vertices that share (approximately) the same 3D position, so faces split
/// across a UV seam can still be recognized as 3D-adjacent.
/// @return Per-vertex canonical id (same id for welded vertices); -1 for removed vertices.
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

/// Builds the shared-edge map for `mesh`, keyed by canonical (welded) vertex ids.
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

/// @return Shortest distance from point `p` to segment [a, b].
double pointSegmentDistance(const Eigen::Vector2d& p, const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
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
bool bary2D(double px, double py,
            double ax, double ay, double bx, double by, double cx, double cy,
            double& w0, double& w1, double& w2) {
    double denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::abs(denom) < 1e-10) return false;
    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denom;
    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denom;
    w2 = 1.0 - w0 - w1;
    return true;
}

/**
 * @brief Rasterizes every active face's UV triangle onto a width x height
 *        grid (same texel-center sampling convention as the rest of the
 *        atlas bake), recording which island truly covers each texel.
 *
 *        This is the ground truth for "is this texel part of the island's
 *        own valid footprint" — the same barycentric test HeightmapBaker
 *        uses to decide which texels get real relief data. rasterizeBand
 *        uses it instead of approximating the footprint from a single seam
 *        edge's local geometry, which breaks down near seam vertices where
 *        consecutive edges meet at an angle (very common after mesh
 *        simplification): the true boundary bends away from either edge's
 *        infinite line there, so a per-edge half-plane test misclassifies
 *        texels that are still genuinely inside the island as "outside",
 *        letting the leap band bleed a texel or two into the island.
 * @param mesh Mesh whose UV layout defines the islands.
 * @param faceIsland Per-face island id, as produced by detectIslands().
 * @param width,height Offset map dimensions.
 * @return Per-texel island id (whichever face's UV triangle covers that
 *         texel's center), or -1 if no face covers it.
 */
std::vector<int> buildIslandTexelMap(
    const QEMSimplifier& mesh, const std::vector<int>& faceIsland,
    int width, int height) {
    std::vector<int> islandAt((size_t)width * height, -1);

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        const Face& f = mesh.faces[fi];
        if (f.removed || faceIsland[fi] < 0) continue;

        Eigen::Vector2d uv0 = mesh.vertices[f.v[0]].uv;
        Eigen::Vector2d uv1 = mesh.vertices[f.v[1]].uv;
        Eigen::Vector2d uv2 = mesh.vertices[f.v[2]].uv;
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
                if (!bary2D(px + 0.5, py + 0.5, u0, v0, u1, v1, u2, v2, w0, w1, w2))
                    continue;
                if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;
                islandAt[(size_t)py * width + px] = faceIsland[fi];
            }
        }
    }
    return islandAt;
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
 *        valid footprint (see buildIslandTexelMap).
 * @param p0,p1 Seam segment endpoints, in UV space.
 * @param theta Rotation (radians) encoded into the texel's z channel (as turns).
 * @param R,t Rigid transform mapping a UV point on this segment to the neighbor island.
 * @param islandId Island id this band is being written for (the "this" island).
 * @param islandAt Per-texel island coverage, from buildIslandTexelMap.
 * @param width,height Offset map dimensions.
 * @param bandWidthUV Half-width of the band, in UV units.
 * @param[out] outData Offset map RGBA buffer being written into.
 * @param[in,out] distBuf Per-texel nearest-seam distance, used for the nearest-seam-wins test.
 */
void rasterizeBand(
    const Eigen::Vector2d& p0, const Eigen::Vector2d& p1,
    double theta, const Eigen::Matrix2d& R, const Eigen::Vector2d& t,
    int islandId, const std::vector<int>& islandAt,
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

/**
 * @brief Assigns each active face an island id via flood fill over
 *        3D-edge-adjacent faces whose UV coordinates agree at the shared edge
 *        (within epsilon). Faces sharing a 3D edge but disagreeing on UV at
 *        that edge are considered seam-separated (different islands).
 * @param mesh Mesh to partition into UV islands.
 * @return One island id per face, in face order; removed faces get id -1.
 */
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

} // namespace

namespace UVAtlas {

MipPyramid buildOffsetMap(
    const QEMSimplifier& mesh,
    int width, int height,
    int seamBandTexels) {
    std::vector<int> faceIsland = detectIslands(mesh);

    std::vector<float> data((size_t)width * height * 4, 0.0f);

    if (width <= 0 || height <= 0 || mesh.faces.empty()) {
        MipPyramid pyr;
        pyr.width = width; pyr.height = height; pyr.channels = 4;
        pyr.mips.push_back(std::move(data));
        return pyr;
    }

    std::vector<float> distBuf((size_t)width * height, std::numeric_limits<float>::max());
    double bandWidthUV = (double)std::max(1, seamBandTexels) / (double)std::min(width, height);
    std::vector<int> islandAt = buildIslandTexelMap(mesh, faceIsland, width, height);

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
        double lenA = dirA.norm(), lenB = dirB.norm();
        if (lenA < 1e-9 || lenB < 1e-9) continue;

        double angA = std::atan2(dirA.y(), dirA.x());
        double angB = std::atan2(dirB.y(), dirB.x());
        double theta = angB - angA;

        Eigen::Matrix2d Rot;
        Rot << std::cos(theta), -std::sin(theta),
               std::sin(theta),  std::cos(theta);
        Eigen::Matrix2d RotInv = Rot.transpose(); // = Rot(-theta)

        // Islands aren't guaranteed to share the same UV texel density along
        // a seam edge (e.g. independently-scaled unwrap charts), so the
        // transform is a similarity (rotation + uniform scale), not a pure
        // rigid rotation: scaleAB maps island A's edge length onto island
        // B's. Without it, only the anchor vertex (uvA0 -> uvB0) lands
        // exactly and the far vertex of the seam edge drifts by
        // |dirA| - |dirB|, leaving a thin gap/overlap along the leap.
        double scaleAB = lenB / lenA; // A -> B
        double scaleBA = lenA / lenB; // B -> A (inverse of scaleAB)

        Eigen::Matrix2d R = scaleAB * Rot;
        Eigen::Vector2d t = uvB0 - R * uvA0;

        // Band on island A's side: jump A -> B.
        // The baked angle is negated relative to theta because the shader's
        // rotateXY(v, angle) implements R(-angle) (it mirrors RTMA_Functions.ush's
        // mul(v, RotationMatrix) row-vector convention), while the position map
        // above needs the direction vector rotated by R(+theta) to stay consistent
        // with the position transform — so the encoded angle must be -theta.
        rasterizeBand(uvA0, uvA1, -theta, R, t, islandA, islandAt, width, height, bandWidthUV, data, distBuf);

        // Band on island B's side: jump B -> A (inverse transform, hence +theta).
        Eigen::Matrix2d Rinv = scaleBA * RotInv;
        Eigen::Vector2d tInv = uvA0 - Rinv * uvB0;
        rasterizeBand(uvB0, uvB1, theta, Rinv, tInv, islandB, islandAt, width, height, bandWidthUV, data, distBuf);
    }

    MipPyramid pyr;
    pyr.width = width; pyr.height = height; pyr.channels = 4;
    pyr.mips.push_back(std::move(data));
    return pyr;
}

} // namespace UVAtlas
