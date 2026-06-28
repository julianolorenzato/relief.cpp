#include "relief/textures.h"
#include <algorithm>
#include <cmath>

namespace {

// Bilinear sample from a raw uint8 image. Returns up to 4 float channels in [0,1].
// Missing channels are filled with 0 (or 1 for alpha when channels < 4).
void bilinearSampleF(const RawImage& img, double u, double v, float out[4]) {
    double x = u * img.width  - 0.5;
    double y = v * img.height - 0.5;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    int x1 = x0 + 1, y1 = y0 + 1;
    double fx = x - x0, fy = y - y0;

    auto cx = [&](int xx) { return std::clamp(xx, 0, img.width  - 1); };
    auto cy = [&](int yy) { return std::clamp(yy, 0, img.height - 1); };

    int c = img.channels;
    const uint8_t* p00 = img.data + ((size_t)cy(y0) * img.width + cx(x0)) * c;
    const uint8_t* p10 = img.data + ((size_t)cy(y0) * img.width + cx(x1)) * c;
    const uint8_t* p01 = img.data + ((size_t)cy(y1) * img.width + cx(x0)) * c;
    const uint8_t* p11 = img.data + ((size_t)cy(y1) * img.width + cx(x1)) * c;

    for (int i = 0; i < c && i < 4; i++) {
        float v00 = p00[i] / 255.0f, v10 = p10[i] / 255.0f;
        float v01 = p01[i] / 255.0f, v11 = p11[i] / 255.0f;
        float top = v00 + (v10 - v00) * (float)fx;
        float bot = v01 + (v11 - v01) * (float)fx;
        out[i] = top + (bot - top) * (float)fy;
    }
    for (int i = c; i < 4; i++) out[i] = (i == 3) ? 1.0f : 0.0f;
}

std::vector<float> resampleColorRGBA(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 4);
    float s[4];
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            size_t idx = ((size_t)y * outW + x) * 4;
            out[idx+0] = s[0]; out[idx+1] = s[1]; out[idx+2] = s[2]; out[idx+3] = s[3];
        }
    }
    return out;
}

std::vector<float> resampleDepthR(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH);
    float s[4];
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            out[(size_t)y * outW + x] = s[0];
        }
    }
    return out;
}

std::vector<float> resampleNormalXYZ(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 3);
    float s[4];
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            float nx = s[0] * 2.f - 1.f;
            float ny = s[1] * 2.f - 1.f;
            float nz = s[2] * 2.f - 1.f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.f; ny = 0.f; nz = 1.f; }
            size_t idx = ((size_t)y * outW + x) * 3;
            out[idx+0] = nx; out[idx+1] = ny; out[idx+2] = nz;
        }
    }
    return out;
}

// ─── Mip downsampling ──────────────────────────────────────────────────────────

std::vector<float> downsampleAvg(const std::vector<float>& src, int w, int h, int channels, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH * channels, 0.0f);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            for (int c = 0; c < channels; c++) {
                float sum = src[((size_t)sy0 * w + sx0) * channels + c]
                          + src[((size_t)sy0 * w + sx1) * channels + c]
                          + src[((size_t)sy1 * w + sx0) * channels + c]
                          + src[((size_t)sy1 * w + sx1) * channels + c];
                dst[((size_t)y * outW + x) * channels + c] = sum * 0.25f;
            }
        }
    }
    return dst;
}

// R(min)/G(max)/B(offset mask)/A(reserved 0) mixed downsample for the Relief Map.
// R and G form a true min/max pair over each mip region's footprint (pooled
// recursively) so they bound the entire footprint at the root.
std::vector<float> downsampleReliefMixed(const std::vector<float>& src, int w, int h, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH * 4, 0.0f);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            size_t i00 = ((size_t)sy0 * w + sx0) * 4, i10 = ((size_t)sy0 * w + sx1) * 4;
            size_t i01 = ((size_t)sy1 * w + sx0) * 4, i11 = ((size_t)sy1 * w + sx1) * 4;

            float rMin  = std::min(std::min(src[i00+0], src[i10+0]), std::min(src[i01+0], src[i11+0]));
            float gMax  = std::max(std::max(src[i00+1], src[i10+1]), std::max(src[i01+1], src[i11+1]));
            float bMask = std::max(std::max(src[i00+2], src[i10+2]), std::max(src[i01+2], src[i11+2]));

            size_t di = ((size_t)y * outW + x) * 4;
            dst[di+0] = rMin;
            dst[di+1] = gMax;
            dst[di+2] = bMask;
            dst[di+3] = 0.0f;
        }
    }
    return dst;
}

MipPyramid buildPyramid(const std::vector<float>& mip0, int w, int h, int channels, bool renormalizeAsNormal) {
    MipPyramid pyr;
    pyr.width = w; pyr.height = h; pyr.channels = channels;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = w, ch = h;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        std::vector<float> next = downsampleAvg(cur, cw, ch, channels, nw, nh);
        if (renormalizeAsNormal) {
            for (size_t i = 0; i + 2 < next.size(); i += channels) {
                float x = next[i], y = next[i+1], z = next[i+2];
                float len = std::sqrt(x*x + y*y + z*z);
                if (len > 1e-8f) { next[i] = x/len; next[i+1] = y/len; next[i+2] = z/len; }
            }
        }
        pyr.mips.push_back(next);
        cur = next; cw = nw; ch = nh;
    }
    return pyr;
}

MipPyramid buildReliefPyramid(const std::vector<float>& mip0, int w, int h) {
    MipPyramid pyr;
    pyr.width = w; pyr.height = h; pyr.channels = 4;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = w, ch = h;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        std::vector<float> next = downsampleReliefMixed(cur, cw, ch, nw, nh);
        pyr.mips.push_back(next);
        cur = next; cw = nw; ch = nh;
    }
    return pyr;
}

} // namespace

TexturePrepResult TextureBaker::bake(
    const QEMSimplifier& mesh,
    const RawImage& colorImg,
    const RawImage& depthImg,
    const RawImage& normalImg,
    int workRes,
    int seamBandTexels,
    TexturePrepProgressCb cb)
{
    TexturePrepResult result;
    auto progress = [&](int p) { if (cb) cb(p); };

    if (!colorImg.valid() || !depthImg.valid() || !normalImg.valid()) {
        result.valid = false;
        return result;
    }
    progress(5);

    std::vector<float> colorMip0  = resampleColorRGBA(colorImg,  workRes, workRes);
    progress(15);
    std::vector<float> depthMip0  = resampleDepthR(depthImg,   workRes, workRes);
    progress(25);
    std::vector<float> normalMip0 = resampleNormalXYZ(normalImg, workRes, workRes);
    progress(35);

    std::vector<int> faceIsland = UVAtlas::detectIslands(mesh);
    progress(45);
    result.offsetMap = UVAtlas::bakeOffsetMap(mesh, faceIsland, workRes, workRes, seamBandTexels);
    progress(55);

    // Relief map mip0: R=depth, G=depth (min==max at finest level), B=seam mask, A=reserved.
    std::vector<float> reliefMip0((size_t)workRes * workRes * 4);
    for (size_t i = 0; i < (size_t)workRes * workRes; i++) {
        reliefMip0[i*4+0] = depthMip0[i];
        reliefMip0[i*4+1] = depthMip0[i];
        reliefMip0[i*4+2] = result.offsetMap.data[i*4+3];
        reliefMip0[i*4+3] = 0.0f;
    }

    result.reliefMap = buildReliefPyramid(reliefMip0, workRes, workRes);
    progress(75);
    result.normalMap = buildPyramid(normalMip0, workRes, workRes, 3, /*renormalizeAsNormal=*/true);
    progress(90);
    result.colorMap  = buildPyramid(colorMip0,  workRes, workRes, 4, /*renormalizeAsNormal=*/false);
    progress(100);

    result.valid = true;
    return result;
}
