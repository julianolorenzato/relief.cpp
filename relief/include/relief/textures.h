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
/// Normal-map pyramids store raw unit-vector components in [-1,1]. The offset
/// (UV-atlas leap) map is also represented as a MipPyramid, with exactly one
/// level (it is never downsampled).
struct MipPyramid {
    std::vector<std::vector<float>> mips;
    int width    = 0;
    int height   = 0;
    int channels = 4;

    /// @return Number of mip levels stored.
    int levelCount() const { return (int)mips.size(); }
};

/// Map builders for the different texture kinds used by relief mapping.
namespace Textures {

/// @return The smallest power of two >= minSize (minimum 1).
int nextPowerOfTwo(int minSize);

/**
 * @brief Builds the color-map mip pyramid from a raw RGBA image, resampled
 *        to `width` x `height`.
 * @param img Source image (any channel count; alpha defaults to 1 if absent).
 * @param width Target base-level width.
 * @param height Target base-level height.
 * @return 4-channel MipPyramid, coarsest level having size 1x1.
 */
MipPyramid buildColorMap(const RawImage& img, int width, int height);

/**
 * @brief Builds the normal-map mip pyramid from a raw RGB-encoded ([0,1])
 *        normal image, resampled to `width` x `height`; each level is kept
 *        renormalized to unit-length vectors in [-1,1].
 * @param img Source image.
 * @param width Target base-level width.
 * @param height Target base-level height.
 * @return 3-channel MipPyramid, coarsest level having size 1x1.
 */
MipPyramid buildNormalMap(const RawImage& img, int width, int height);

/**
 * @brief Builds the packed relief map mip pyramid consumed by relief
 *        mapping: per level, min/max depth resampled from `depthImg` plus a
 *        max-pooled seam mask for island leaping.
 * @param depthImg Source depth/heightmap image.
 * @param width Target base-level width.
 * @param height Target base-level height.
 * @param offsetMap Previously baked offset map (see UVAtlas::buildOffsetMap);
 *        its w (validity) channel is used as the seam mask. Pass an empty
 *        MipPyramid{} if no offset map is available yet — the seam mask then
 *        defaults to all-zero (no island leaping).
 * @return 4-channel MipPyramid: channel0=min depth, channel1=max depth,
 *         channel2=seam mask, channel3=unused (0).
 */
MipPyramid buildReliefMap(
    const RawImage& depthImg,
    int width, int height,
    const MipPyramid& offsetMap);

} // namespace Textures
