#pragma once
#include <vector>
#include <cstdint>

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

namespace Textures {

// Build a full mip pyramid using 2x2 average (bilinear) downsampling.
// If renormalizeAsNormal is true, each downsampled level is renormalized
// so every texel remains a unit vector (use for normal maps in [-1,1]).
MipPyramid buildBilinearPyramid(
    const std::vector<float>& mip0,
    int width, int height, int channels,
    bool renormalizeAsNormal = false);

// Build a mip pyramid using 2x2 minimum pooling — single channel.
// Each coarser level stores the minimum value seen in its 2x2 footprint.
MipPyramid buildMinPyramid(
    const std::vector<float>& mip0,
    int width, int height);

// Build a mip pyramid using 2x2 maximum pooling — single channel.
// Each coarser level stores the maximum value seen in its 2x2 footprint.
MipPyramid buildMaxPyramid(
    const std::vector<float>& mip0,
    int width, int height);

} // namespace Textures
