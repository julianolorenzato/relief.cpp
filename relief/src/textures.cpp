/**
 * @file textures.cpp
 * @brief Mip pyramid downsampling kernels (average, min, max) and their
 *        public builder entry points.
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

} // namespace

namespace Textures {

MipPyramid buildBilinearPyramid(const std::vector<float>& mip0, int width, int height, int channels, bool renormalizeAsNormal) {
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

} // namespace Textures
