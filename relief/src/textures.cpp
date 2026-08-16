/**
 * @file textures.cpp
 * @brief Mip pyramid downsampling kernels (average, min, max) and the public
 *        per-map builder entry points that compose them.
 */
#include "relief/textures.h"
#include <algorithm>
#include <cmath>

namespace {

/// Downsamples `src` by 2x2 box-average, per channel.
/// @param[out] outW,outH Resulting dimensions.
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

/// Downsamples a single-channel `src` by 2x2 minimum pooling.
/// @param[out] outW,outH Resulting dimensions.
std::vector<float> downsampleMin1ch(const std::vector<float>& src, int w, int h, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            float v = std::min({src[(size_t)sy0 * w + sx0], src[(size_t)sy0 * w + sx1],
                                src[(size_t)sy1 * w + sx0], src[(size_t)sy1 * w + sx1]});
            dst[(size_t)y * outW + x] = v;
        }
    }
    return dst;
}

/// Downsamples a single-channel `src` by 2x2 maximum pooling.
/// @param[out] outW,outH Resulting dimensions.
std::vector<float> downsampleMax1ch(const std::vector<float>& src, int w, int h, int& outW, int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    std::vector<float> dst((size_t)outW * outH);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            int sx0 = std::min(w - 1, x * 2), sx1 = std::min(w - 1, x * 2 + 1);
            int sy0 = std::min(h - 1, y * 2), sy1 = std::min(h - 1, y * 2 + 1);
            float v = std::max({src[(size_t)sy0 * w + sx0], src[(size_t)sy0 * w + sx1],
                                src[(size_t)sy1 * w + sx0], src[(size_t)sy1 * w + sx1]});
            dst[(size_t)y * outW + x] = v;
        }
    }
    return dst;
}

/// Bilinearly samples `img` at normalized (u, v), writing up to 4 channels
/// (in [0,1]) to `out`; unused channels are 0, except alpha which defaults to 1.
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

/// Builds a full mip pyramid using 2x2 average (bilinear) downsampling.
/// `renormalizeAsNormal`, if true, renormalizes each downsampled level so
/// every texel remains a unit vector (use for normal maps in [-1,1]).
MipPyramid buildBilinearPyramid(const std::vector<float>& mip0, int width, int height, int channels, bool renormalizeAsNormal = false) {
    MipPyramid pyr;
    pyr.width = width; pyr.height = height; pyr.channels = channels;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = width, ch = height;
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

/// Builds a mip pyramid using 2x2 minimum pooling — single channel. Each
/// coarser level stores the minimum value seen in its 2x2 footprint.
MipPyramid buildMinPyramid(const std::vector<float>& mip0, int width, int height) {
    MipPyramid pyr;
    pyr.width = width; pyr.height = height; pyr.channels = 1;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = width, ch = height;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        pyr.mips.push_back(downsampleMin1ch(cur, cw, ch, nw, nh));
        cur = pyr.mips.back(); cw = nw; ch = nh;
    }
    return pyr;
}

/// Builds a mip pyramid using 2x2 maximum pooling — single channel. Each
/// coarser level stores the maximum value seen in its 2x2 footprint.
MipPyramid buildMaxPyramid(const std::vector<float>& mip0, int width, int height) {
    MipPyramid pyr;
    pyr.width = width; pyr.height = height; pyr.channels = 1;
    pyr.mips.push_back(mip0);

    std::vector<float> cur = mip0;
    int cw = width, ch = height;
    while (cw > 1 || ch > 1) {
        int nw, nh;
        pyr.mips.push_back(downsampleMax1ch(cur, cw, ch, nw, nh));
        cur = pyr.mips.back(); cw = nw; ch = nh;
    }
    return pyr;
}

/// Resamples `img` to outW x outH RGBA float data via bilinear sampling.
std::vector<float> resampleColorRGBA(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 4);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            size_t idx = ((size_t)y * outW + x) * 4;
            out[idx+0] = s[0]; out[idx+1] = s[1]; out[idx+2] = s[2]; out[idx+3] = s[3];
        }
    return out;
}

/// Resamples `img`'s red channel to outW x outH single-channel float data.
std::vector<float> resampleDepthR(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            out[(size_t)y * outW + x] = s[0];
        }
    return out;
}

/// Resamples `img` (encoded as [0,1] RGB) to outW x outH unit-vector XYZ
/// float data in [-1,1], renormalizing each resampled texel.
std::vector<float> resampleNormalXYZ(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH * 3);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            float nx = s[0] * 2.f - 1.f, ny = s[1] * 2.f - 1.f, nz = s[2] * 2.f - 1.f;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            else { nx = 0.f; ny = 0.f; nz = 1.f; }
            size_t idx = ((size_t)y * outW + x) * 3;
            out[idx+0] = nx; out[idx+1] = ny; out[idx+2] = nz;
        }
    return out;
}

/// Extracts one channel out of packed interleaved float data (e.g. the w
/// channel of a baked offset map, used as relief mapping's seam mask).
std::vector<float> extractChannel(const std::vector<float>& data, size_t texelCount, int channels, int channelIndex) {
    std::vector<float> out(texelCount);
    for (size_t i = 0; i < texelCount; i++) out[i] = data[i * channels + channelIndex];
    return out;
}

} // namespace

namespace Textures {

int nextPowerOfTwo(int minSize) {
    int size = 1;
    while (size < minSize) size <<= 1;
    return size;
}

MipPyramid buildColorMap(const RawImage& img, int width, int height) {
    auto mip0 = resampleColorRGBA(img, width, height);
    return buildBilinearPyramid(mip0, width, height, 4);
}

MipPyramid buildNormalMap(const RawImage& img, int width, int height) {
    auto mip0 = resampleNormalXYZ(img, width, height);
    return buildBilinearPyramid(mip0, width, height, 3, /*renormalizeAsNormal=*/true);
}

MipPyramid buildReliefMap(const RawImage& depthImg, int width, int height, const MipPyramid& offsetMap) {
    auto depthMip0 = resampleDepthR(depthImg, width, height);

    std::vector<float> seamMip0;
    if (!offsetMap.mips.empty())
        seamMip0 = extractChannel(offsetMap.mips[0], (size_t)width * height, offsetMap.channels, 3);
    else
        seamMip0.assign((size_t)width * height, 0.f);

    auto minPyr = buildMinPyramid(depthMip0, width, height);
    auto maxPyr = buildMaxPyramid(depthMip0, width, height);
    auto maskPyr = buildMaxPyramid(seamMip0, width, height);

    MipPyramid reliefMap;
    reliefMap.width = width; reliefMap.height = height; reliefMap.channels = 4;
    for (int lvl = 0; lvl < minPyr.levelCount(); lvl++) {
        int w = std::max(1, width >> lvl), h = std::max(1, height >> lvl);
        std::vector<float> mip((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; i++) {
            mip[i*4+0] = minPyr.mips[lvl][i];
            mip[i*4+1] = maxPyr.mips[lvl][i];
            mip[i*4+2] = maskPyr.mips[lvl][i];
            mip[i*4+3] = 0.f;
        }
        reliefMap.mips.push_back(std::move(mip));
    }
    return reliefMap;
}

} // namespace Textures
