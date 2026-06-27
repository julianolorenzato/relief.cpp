#pragma once
#include "relief/qem.h"
#include <vector>
#include <cstdint>
#include <functional>

using ProgressCb = std::function<void(int)>;

struct HeightmapResult {
    std::vector<float>   heights; // signed displacement per pixel (raw)
    std::vector<uint8_t> image;   // normalized 0-255 grayscale
    int   width  = 0;
    int   height = 0;
    float minH   = 0.0f;
    float maxH   = 0.0f;
    bool  valid  = false;
};

class HeightmapBaker {
public:
    static HeightmapResult bakeUVDistance(
        const QEMSimplifier& simplified,
        const QEMSimplifier& original,
        int texWidth, int texHeight,
        ProgressCb cb = {});

private:
    using V3 = Eigen::Vector3d;

    struct TexelSample {
        V3    pos;
        V3    normal;
        bool  valid = false;
    };

    static std::vector<TexelSample> rasterizeUV(
        const QEMSimplifier& mesh, int W, int H);

    static void normalize(HeightmapResult& r);
};
