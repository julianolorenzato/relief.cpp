#pragma once
#include "relief/qem.h"
#include "relief/uv_atlas.h"
#include <vector>
#include <functional>
#include <cstdint>

using TexturePrepProgressCb = std::function<void(int)>;

// Raw uncompressed image (uint8, row-major). channels: 1=grey, 3=RGB, 4=RGBA.
struct RawImage {
    const uint8_t* data = nullptr;
    int width    = 0;
    int height   = 0;
    int channels = 0;

    bool valid() const { return data && width > 0 && height > 0 && channels > 0; }
};

// One channel-set mip pyramid: mips[0] is full res, each subsequent level is half
// the resolution. Each level stores `channels` floats per texel, row-major.
// Normal-map pyramids store raw unit-vector components in [-1,1].
struct MipPyramid {
    std::vector<std::vector<float>> mips;
    int width    = 0;
    int height   = 0;
    int channels = 4;

    int levelCount() const { return (int)mips.size(); }
};

struct TexturePrepResult {
    MipPyramid colorMap;      // RGBA, average-downsampled
    MipPyramid reliefMap;     // R=min depth, G=max depth, B=seam/offset mask, A=reserved(0)
    MipPyramid normalMap;     // XYZ unit vector in [-1,1], average-downsampled + renormalized
    OffsetMapResult offsetMap; // mip0 only

    bool valid = false;
};

class TextureBaker {
public:
    // colorImg: RGBA8888, depthImg: Grayscale8 (channel 0 used as depth),
    // normalImg: RGB888 or RGBA8888 (XYZ encoded in [0,1]).
    // workRes: square resolution for baking. seamBandTexels: UV seam dilation width.
    static TexturePrepResult bake(
        const QEMSimplifier& mesh,
        const RawImage& colorImg,
        const RawImage& depthImg,
        const RawImage& normalImg,
        int workRes,
        int seamBandTexels,
        TexturePrepProgressCb cb = {});
};
