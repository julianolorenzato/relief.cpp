#include "heightmap.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

using V3 = Eigen::Vector3d;

// ─── Helpers (file-local) ────────────────────────────────────────────────────

static bool bary2D(double px, double py,
                   double ax, double ay,
                   double bx, double by,
                   double cx, double cy,
                   double& w0, double& w1, double& w2)
{
    double denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::abs(denom) < 1e-10) return false;
    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denom;
    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denom;
    w2 = 1.0 - w0 - w1;
    return true;
}

// ─── UV rasterisation ────────────────────────────────────────────────────────

std::vector<HeightmapBaker::TexelSample>
HeightmapBaker::rasterizeUV(const QEMSimplifier& mesh, int W, int H)
{
    std::vector<TexelSample> samples(W * H);

    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
        const auto& fc = mesh.faces[fi];
        if (fc.removed) continue;

        const V3& p0 = mesh.vertices[fc.v[0]].pos;
        const V3& p1 = mesh.vertices[fc.v[1]].pos;
        const V3& p2 = mesh.vertices[fc.v[2]].pos;

        V3 n = (p1 - p0).cross(p2 - p0);
        if (n.norm() < 1e-10) continue;
        n.normalize();

        const auto& uv0 = mesh.vertices[fc.v[0]].uv;
        const auto& uv1 = mesh.vertices[fc.v[1]].uv;
        const auto& uv2 = mesh.vertices[fc.v[2]].uv;

        // Tangent frame from UV differentials (Gram-Schmidt orthogonalized against n)
        V3 tangent = V3::Zero(), bitangent = V3::Zero();
        {
            V3 e1 = p1 - p0, e2 = p2 - p0;
            Eigen::Vector2d duv1 = uv1 - uv0, duv2 = uv2 - uv0;
            double det = duv1.x() * duv2.y() - duv1.y() * duv2.x();
            if (std::abs(det) > 1e-10) {
                double inv = 1.0 / det;
                tangent = (e1 * duv2.y() - e2 * duv1.y()) * inv;
                tangent -= n * n.dot(tangent);
                if (tangent.norm() > 1e-10) {
                    tangent.normalize();
                    bitangent = n.cross(tangent);
                } else {
                    tangent = V3::Zero();
                }
            }
        }

        double u0 = uv0.x() * W, v0 = uv0.y() * H;
        double u1 = uv1.x() * W, v1 = uv1.y() * H;
        double u2 = uv2.x() * W, v2 = uv2.y() * H;

        int minX = std::max(0,   (int)std::floor(std::min({u0, u1, u2})));
        int maxX = std::min(W-1, (int)std::ceil (std::max({u0, u1, u2})));
        int minY = std::max(0,   (int)std::floor(std::min({v0, v1, v2})));
        int maxY = std::min(H-1, (int)std::ceil (std::max({v0, v1, v2})));

        for (int py = minY; py <= maxY; py++) {
            for (int px = minX; px <= maxX; px++) {
                double w0, w1, w2;
                if (!bary2D(px + 0.5, py + 0.5,
                            u0, v0, u1, v1, u2, v2,
                            w0, w1, w2)) continue;
                if (w0 < -1e-4 || w1 < -1e-4 || w2 < -1e-4) continue;

                w0 = std::max(0.0, w0);
                w1 = std::max(0.0, w1);
                w2 = std::max(0.0, w2);
                double s = w0 + w1 + w2;
                if (s < 1e-10) continue;
                w0 /= s; w1 /= s; w2 /= s;

                TexelSample& ts = samples[py * W + px];
                ts.pos       = w0 * p0 + w1 * p1 + w2 * p2;
                ts.normal    = n;
                ts.tangent   = tangent;
                ts.bitangent = bitangent;
                ts.vi[0]  = fc.v[0]; ts.vi[1] = fc.v[1]; ts.vi[2] = fc.v[2];
                ts.w[0]   = (float)w0;
                ts.w[1]   = (float)w1;
                ts.w[2]   = (float)w2;
                ts.valid  = true;
            }
        }
    }
    return samples;
}

// ─── Ray-triangle intersection (Möller-Trumbore) ────────────────────────────

bool HeightmapBaker::rayTriangle(
    const V3& orig, const V3& dir,
    const V3& a,    const V3& b, const V3& c,
    double& t)
{
    constexpr double EPS = 1e-8;
    V3 e1 = b - a, e2 = c - a;
    V3 h  = dir.cross(e2);
    double det = e1.dot(h);
    if (std::abs(det) < EPS) return false;

    double inv = 1.0 / det;
    V3 s = orig - a;
    double u = inv * s.dot(h);
    if (u < 0.0 || u > 1.0) return false;

    V3 q = s.cross(e1);
    double v = inv * dir.dot(q);
    if (v < 0.0 || u + v > 1.0) return false;

    t = inv * e2.dot(q);
    return true;
}

// ─── Normalise to [0, 255] ───────────────────────────────────────────────────

void HeightmapBaker::normalize(HeightmapResult& r)
{
    std::vector<float> valid;
    valid.reserve(r.heights.size());
    for (float h : r.heights)
        if (!std::isnan(h) && !std::isinf(h))
            valid.push_back(h);

    if (valid.empty()) {
        r.minH = r.maxH = 0.0f;
        r.image.assign(r.width * r.height, 0);
        return;
    }

    std::sort(valid.begin(), valid.end());

    // Clip 1%-99% to avoid outliers (distant/errant hits) from washing out contrast.
    size_t p1  = valid.size() / 100;
    size_t p99 = valid.size() - 1 - p1;
    r.minH = valid[p1];
    r.maxH = valid[p99];

    r.image.assign(r.width * r.height, 0);
    float range = r.maxH - r.minH;
    if (range < 1e-10f) range = 1.0f;

    for (int i = 0; i < (int)r.heights.size(); i++) {
        float h = r.heights[i];
        if (std::isnan(h) || std::isinf(h)) { r.image[i] = 0; continue; }
        r.image[i] = (uint8_t)(std::clamp((h - r.minH) / range, 0.0f, 1.0f) * 255.0f);
    }
}

// ─── Strategy 0: Ray casting (no cage) ───────────────────────────────────────

HeightmapResult HeightmapBaker::bakeRayCast(
    const QEMSimplifier& simplified,
    const QEMSimplifier& original,
    int W, int H,
    ProgressCb cb)
{
    HeightmapResult result;
    result.width  = W;
    result.height = H;
    result.heights.assign(W * H, std::numeric_limits<float>::quiet_NaN());

    auto samples = rasterizeUV(simplified, W, H);

    struct OTri { V3 a, b, c; };
    std::vector<OTri> otris;
    otris.reserve(original.faces.size());
    for (const auto& fc : original.faces) {
        if (fc.removed) continue;
        otris.push_back({original.vertices[fc.v[0]].pos,
                         original.vertices[fc.v[1]].pos,
                         original.vertices[fc.v[2]].pos});
    }

    int hits  = 0;
    int total = W * H;
    int step  = std::max(1, total / 100);

    for (int i = 0; i < total; i++) {
        const auto& s = samples[i];
        if (!s.valid) { if (cb && i % step == 0) cb(i * 100 / total); continue; }

        V3 orig = s.pos + s.normal * 1e-5;

        // Track fwd/bwd separately so a nearby backface hit in one direction
        // cannot steal bestT from the correct hit in the other direction.
        double bestFwdT = std::numeric_limits<double>::max();
        double bestBwdT = std::numeric_limits<double>::max();

        for (const auto& tri : otris) {
            double t;
            if (rayTriangle(orig,  s.normal, tri.a, tri.b, tri.c, t) && t > 0.0)
                bestFwdT = std::min(bestFwdT, t);
            if (rayTriangle(orig, -s.normal, tri.a, tri.b, tri.c, t) && t > 0.0)
                bestBwdT = std::min(bestBwdT, t);
        }

        bool hasFwd = bestFwdT < 1e15;
        bool hasBwd = bestBwdT < 1e15;
        if (hasFwd || hasBwd) {
            float h;
            if (hasFwd && hasBwd)
                h = (bestFwdT <= bestBwdT) ? (float)bestFwdT : -(float)bestBwdT;
            else if (hasFwd)
                h = (float)bestFwdT;
            else
                h = -(float)bestBwdT;
            result.heights[i] = h;
            ++hits;
        }

        if (cb && i % step == 0) cb(i * 100 / total);
    }
    if (cb) cb(100);

    std::cout << "RayCast: " << hits << " texels hit\n";
    normalize(result);
    result.valid = true;
    return result;
}

// ─── Strategy 1: Ray casting with cage ───────────────────────────────────────

HeightmapResult HeightmapBaker::bakeRayCastCage(
    const QEMSimplifier& simplified,
    const QEMSimplifier& original,
    int W, int H,
    double cageOffset,
    ProgressCb cb)
{
    HeightmapResult result;
    result.width  = W;
    result.height = H;
    result.heights.assign(W * H, std::numeric_limits<float>::quiet_NaN());

    auto samples = rasterizeUV(simplified, W, H);

    struct OTri { V3 a, b, c; };
    std::vector<OTri> otris;
    otris.reserve(original.faces.size());
    for (const auto& fc : original.faces) {
        if (fc.removed) continue;
        otris.push_back({original.vertices[fc.v[0]].pos,
                         original.vertices[fc.v[1]].pos,
                         original.vertices[fc.v[2]].pos});
    }

    int hits  = 0;
    int total = W * H;
    int step  = std::max(1, total / 100);

    for (int i = 0; i < total; i++) {
        const auto& s = samples[i];
        if (!s.valid) { if (cb && i % step == 0) cb(i * 100 / total); continue; }

        // Ray starts at cage (simplified inflated by cageOffset) and shoots inward.
        // At t=cageOffset the ray passes through the simplified surface itself.
        // Only hits within the cage volume (t < 2*cageOffset) are considered.
        V3 cageOrig = s.pos + s.normal * cageOffset;
        V3 dir      = -s.normal;

        double bestT = std::numeric_limits<double>::max();

        for (const auto& tri : otris) {
            double t;
            if (rayTriangle(cageOrig, dir, tri.a, tri.b, tri.c, t)
                    && t > 0.0 && t < 2.0 * cageOffset)
                if (t < bestT) bestT = t;
        }

        if (bestT < std::numeric_limits<double>::max()) {
            // Positive: original is above simplified (between cage and simplified surface).
            // Negative: original is below simplified (past the simplified surface).
            result.heights[i] = (float)(cageOffset - bestT);
            ++hits;
        }

        if (cb && i % step == 0) cb(i * 100 / total);
    }
    if (cb) cb(100);

    std::cout << "RayCastCage: " << hits << " texels hit\n";
    normalize(result);
    result.valid = true;
    return result;
}

// ─── Möller-Trumbore with barycentric output ──────────────────────────────────

bool HeightmapBaker::rayTriangleBarycentric(
    const V3& orig, const V3& dir,
    const V3& a,    const V3& b, const V3& c,
    double& t, double& u, double& v)
{
    constexpr double EPS = 1e-8;
    V3 e1 = b - a, e2 = c - a;
    V3 h  = dir.cross(e2);
    double det = e1.dot(h);
    if (std::abs(det) < EPS) return false;

    double inv = 1.0 / det;
    V3 s = orig - a;
    u = inv * s.dot(h);
    if (u < 0.0 || u > 1.0) return false;

    V3 q = s.cross(e1);
    v = inv * dir.dot(q);
    if (v < 0.0 || u + v > 1.0) return false;

    t = inv * e2.dot(q);
    return true;
}

// ─── Strategy 2: Tangent-space normal map ────────────────────────────────────

NormalmapResult HeightmapBaker::bakeNormalMap(
    const QEMSimplifier& simplified,
    const QEMSimplifier& original,
    int W, int H,
    ProgressCb cb)
{
    NormalmapResult result;
    result.width  = W;
    result.height = H;
    // Default: flat tangent-space normal (0,0,1) -> RGB (128,128,255)
    result.image.assign(W * H * 3, 128);
    for (int i = 0; i < W * H; i++) result.image[i * 3 + 2] = 255;

    auto samples = rasterizeUV(simplified, W, H);

    // Precompute area-weighted smooth vertex normals for the original mesh
    std::vector<V3> origNormals(original.vertices.size(), V3::Zero());
    for (const auto& fc : original.faces) {
        if (fc.removed) continue;
        const V3& a = original.vertices[fc.v[0]].pos;
        const V3& b = original.vertices[fc.v[1]].pos;
        const V3& c = original.vertices[fc.v[2]].pos;
        V3 weighted = (b - a).cross(c - a); // magnitude = 2*area
        origNormals[fc.v[0]] += weighted;
        origNormals[fc.v[1]] += weighted;
        origNormals[fc.v[2]] += weighted;
    }
    for (auto& n : origNormals) {
        double len = n.norm();
        if (len > 1e-10) n /= len;
    }

    struct OTri { V3 a, b, c, na, nb, nc; };
    std::vector<OTri> otris;
    otris.reserve(original.faces.size());
    for (const auto& fc : original.faces) {
        if (fc.removed) continue;
        otris.push_back({
            original.vertices[fc.v[0]].pos,
            original.vertices[fc.v[1]].pos,
            original.vertices[fc.v[2]].pos,
            origNormals[fc.v[0]],
            origNormals[fc.v[1]],
            origNormals[fc.v[2]]
        });
    }

    int hits  = 0;
    int total = W * H;
    int step  = std::max(1, total / 100);

    for (int i = 0; i < total; i++) {
        const auto& s = samples[i];
        if (!s.valid || s.tangent.squaredNorm() < 0.5) {
            if (cb && i % step == 0) cb(i * 100 / total);
            continue;
        }

        V3 rayOrig = s.pos + s.normal * 1e-5;

        double bestFwdT = std::numeric_limits<double>::max();
        double bestBwdT = std::numeric_limits<double>::max();
        int    bestFwdTri = -1, bestBwdTri = -1;
        double bestFwdU = 0, bestFwdV = 0;
        double bestBwdU = 0, bestBwdV = 0;

        for (int ti = 0; ti < (int)otris.size(); ti++) {
            const auto& tri = otris[ti];
            double t, u, v;
            if (rayTriangleBarycentric(rayOrig,  s.normal, tri.a, tri.b, tri.c, t, u, v) && t > 0.0) {
                if (t < bestFwdT) { bestFwdT = t; bestFwdTri = ti; bestFwdU = u; bestFwdV = v; }
            }
            if (rayTriangleBarycentric(rayOrig, -s.normal, tri.a, tri.b, tri.c, t, u, v) && t > 0.0) {
                if (t < bestBwdT) { bestBwdT = t; bestBwdTri = ti; bestBwdU = u; bestBwdV = v; }
            }
        }

        int    hitTri = -1;
        double hitU   = 0, hitV = 0;
        if (bestFwdTri >= 0 || bestBwdTri >= 0) {
            if (bestFwdTri >= 0 && (bestBwdTri < 0 || bestFwdT <= bestBwdT))
                { hitTri = bestFwdTri; hitU = bestFwdU; hitV = bestFwdV; }
            else
                { hitTri = bestBwdTri; hitU = bestBwdU; hitV = bestBwdV; }
        }

        if (hitTri >= 0) {
            // Interpolate smooth normal at hit point: w0=1-u-v, w1=u, w2=v
            double w0 = 1.0 - hitU - hitV;
            V3 worldN = w0 * otris[hitTri].na + hitU * otris[hitTri].nb + hitV * otris[hitTri].nc;
            double len = worldN.norm();
            if (len > 1e-10) worldN /= len;

            // Project into the simplified mesh's tangent space
            double nx = worldN.dot(s.tangent);
            double ny = worldN.dot(s.bitangent);
            double nz = worldN.dot(s.normal);

            result.image[i * 3 + 0] = (uint8_t)(std::clamp(nx * 0.5 + 0.5, 0.0, 1.0) * 255.0);
            result.image[i * 3 + 1] = (uint8_t)(std::clamp(ny * 0.5 + 0.5, 0.0, 1.0) * 255.0);
            result.image[i * 3 + 2] = (uint8_t)(std::clamp(nz * 0.5 + 0.5, 0.0, 1.0) * 255.0);
            ++hits;
        }

        if (cb && i % step == 0) cb(i * 100 / total);
    }
    if (cb) cb(100);

    std::cout << "NormalMap: " << hits << " texels hit\n";
    result.valid = true;
    return result;
}
