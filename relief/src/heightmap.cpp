#include "core/heightmap.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <iostream>
#include <thread>

using V3 = Eigen::Vector3d;

// Helpers

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

//Rastereização

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
                ts.pos    = w0 * p0 + w1 * p1 + w2 * p2;
                ts.normal = n;
                ts.valid  = true;
            }
        }
    }
    return samples;
}

// 0, 255

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

HeightmapResult HeightmapBaker::bakeUVDistance(
    const QEMSimplifier& simplified,
    const QEMSimplifier& original,
    int W, int H,
    ProgressCb cb)
{
    HeightmapResult result;
    result.width  = W;
    result.height = H;
    result.heights.assign(W * H, std::numeric_limits<float>::quiet_NaN());

    auto simSamples = rasterizeUV(simplified, W, H);
    auto orgSamples = rasterizeUV(original,   W, H);

    int total = W * H;
    int step  = std::max(1, total / 100);
    std::atomic<int> hits{0};
    std::atomic<int> processed{0};

    // Each texel is independent and writes only its own slot in result.heights,
    // so threads need no locking only the hit/progress counters are shared,
    // via atomics.
    auto worker = [&](int begin, int end) {
        for (int i = begin; i < end; i++) {
            const auto& s = simSamples[i];
            const auto& o = orgSamples[i];
            if (s.valid && o.valid) {
                // Signed distance along the simplified surface's normal, not raw
                // magnitude, so the sign still tells "above"/"below" for relief mapping.
                result.heights[i] = (float)(o.pos - s.pos).dot(s.normal);
                hits.fetch_add(1, std::memory_order_relaxed);
            }

            int done = processed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (cb && done % step == 0) cb(done * 100 / total);
        }
    };

    unsigned nThreads = std::max(1u, std::thread::hardware_concurrency());
    int chunk = (total + (int)nThreads - 1) / (int)nThreads;
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < nThreads; t++) {
        int begin = (int)t * chunk;
        int end   = std::min(total, begin + chunk);
        if (begin >= end) break;
        pool.emplace_back(worker, begin, end);
    }
    for (auto& th : pool) th.join();

    if (cb) cb(100);

    std::cout << "UVDistance: " << hits.load() << " texels matched\n";
    normalize(result);
    result.valid = true;
    return result;
}

