/**
 * @file textures.h
 * @brief Mip pyramid construction utilities for the color/relief/normal maps
 *        used by relief mapping.
 */
#pragma once
#include <vector>
#include <cstdint>

/// Raw uncompressed image (uint8, row-major). channels: 1=grey, 3=RGB, 4=RGBA.
struct RawImage {
    const uint8_t* data = nullptr;
    int width    = 0;
    int height   = 0;
    int channels = 0;

    /// @return true if the image has non-null data and positive dimensions/channels.
    bool valid() const { return data && width > 0 && height > 0 && channels > 0; }
};

/// One channel-set mip pyramid: mips[0] is full res, each subsequent level is
/// half the resolution. Each level stores `channels` floats per texel, row-major.
/// Normal-map pyramids store raw unit-vector components in [-1,1].
struct MipPyramid {
    std::vector<std::vector<float>> mips;
    int width    = 0;
    int height   = 0;
    int channels = 4;

    /// @return Number of mip levels stored.
    int levelCount() const { return (int)mips.size(); }
};

/// Mip pyramid builders for the different texture kinds used by relief mapping.
namespace Textures {

/**
 * @brief Builds a full mip pyramid using 2x2 average (bilinear) downsampling.
 * @param mip0 Base-level (full resolution) data, row-major, `channels` floats per texel.
 * @param width Base-level width.
 * @param height Base-level height.
 * @param channels Number of float channels per texel.
 * @param renormalizeAsNormal If true, each downsampled level is renormalized
 *        so every texel remains a unit vector (use for normal maps in [-1,1]).
 * @return The built pyramid, coarsest level having size 1x1.
 */
MipPyramid buildBilinearPyramid(
    const std::vector<float>& mip0,
    int width, int height, int channels,
    bool renormalizeAsNormal = false);

/**
 * @brief Builds a mip pyramid using 2x2 minimum pooling — single channel.
 *        Each coarser level stores the minimum value seen in its 2x2 footprint.
 * @param mip0 Base-level data, row-major, single channel.
 * @param width Base-level width.
 * @param height Base-level height.
 * @return The built pyramid.
 */
MipPyramid buildMinPyramid(
    const std::vector<float>& mip0,
    int width, int height);

/**
 * @brief Builds a mip pyramid using 2x2 maximum pooling — single channel.
 *        Each coarser level stores the maximum value seen in its 2x2 footprint.
 * @param mip0 Base-level data, row-major, single channel.
 * @param width Base-level width.
 * @param height Base-level height.
 * @return The built pyramid.
 */
MipPyramid buildMaxPyramid(
    const std::vector<float>& mip0,
    int width, int height);

/// Resamples `img` to outW x outH RGBA float data via bilinear sampling.
std::vector<float> resampleColorRGBA(const RawImage& img, int outW, int outH);

/// Resamples `img`'s red channel to outW x outH single-channel float data.
std::vector<float> resampleDepthR(const RawImage& img, int outW, int outH);

/// Resamples `img` (encoded as [0,1] RGB) to outW x outH unit-vector XYZ float
/// data in [-1,1], renormalizing each resampled texel.
std::vector<float> resampleNormalXYZ(const RawImage& img, int outW, int outH);

} // namespace Textures
