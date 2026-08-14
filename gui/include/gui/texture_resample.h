/**
 * @file texture_resample.h
 * @brief Bilinear resampling helpers that turn a RawImage into the float
 *        mip0 buffers consumed by Textures::build*Pyramid(). Shared between
 *        TexturePrepModule (pipeline bake) and ReliefTestModule (standalone bake).
 */
#pragma once
#include "relief/textures.h"
#include <algorithm>
#include <cmath>
#include <vector>

/// Bilinearly samples `img` at normalized (u, v), writing up to 4 channels
/// (in [0,1]) to `out`; unused channels are 0, except alpha which defaults to 1.
inline void bilinearSampleF(const RawImage& img, double u, double v, float out[4]) {
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

/// Resamples `img` to outW x outH RGBA float data via bilinear sampling.
inline std::vector<float> resampleColorRGBA(const RawImage& img, int outW, int outH) {
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
inline std::vector<float> resampleDepthR(const RawImage& img, int outW, int outH) {
    std::vector<float> out((size_t)outW * outH);
    float s[4];
    for (int y = 0; y < outH; y++)
        for (int x = 0; x < outW; x++) {
            bilinearSampleF(img, (x + 0.5) / outW, (y + 0.5) / outH, s);
            out[(size_t)y * outW + x] = s[0];
        }
    return out;
}

/// Resamples `img` (encoded as [0,1] RGB) to outW x outH unit-vector XYZ float
/// data in [-1,1], renormalizing each resampled texel.
inline std::vector<float> resampleNormalXYZ(const RawImage& img, int outW, int outH) {
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
