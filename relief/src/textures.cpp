#include "relief/textures.h"
#include <algorithm>
#include <cmath>

namespace {

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

} // namespace Textures
