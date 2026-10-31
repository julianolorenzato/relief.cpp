/**
 * @file tex.h
 * @brief Owning row-major image container with an explicit mip chain, used for mesh textures.
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tex {

/// Pixel layout: interleaved channels, each 8-bit or 16-bit normalized, or 32-bit float.
enum class PixelFormat {
    R8, RG8, RGB8, RGBA8,
    R16, RG16, RGB16, RGBA16,
    R32F, RG32F, RGB32F, RGBA32F,
};

/// Channel count and channel size of a PixelFormat.
struct FormatInfo {
    int channels;      ///< 1..4
    int channelBytes;  ///< 1, 2 or 4
    bool isFloat;      ///< true only for the `*32F` formats

    /// @return Size in bytes of one texel (all channels).
    constexpr size_t texelSize() const { return size_t(channels) * channelBytes; }
};

/// @return The channel count and channel size of `f`.
constexpr FormatInfo formatInfo(PixelFormat f) {
    switch (f) {
        case PixelFormat::R8: return {1, 1, false};
        case PixelFormat::RG8: return {2, 1, false};
        case PixelFormat::RGB8: return {3, 1, false};
        case PixelFormat::RGBA8: return {4, 1, false};
        case PixelFormat::R16: return {1, 2, false};
        case PixelFormat::RG16: return {2, 2, false};
        case PixelFormat::RGB16: return {3, 2, false};
        case PixelFormat::RGBA16: return {4, 2, false};
        case PixelFormat::R32F: return {1, 4, true};
        case PixelFormat::RG32F: return {2, 4, true};
        case PixelFormat::RGB32F: return {3, 4, true};
        case PixelFormat::RGBA32F: return {4, 4, true};
    }
    return {0, 0, false};
}

/**
 * @brief Derives one texel of a coarser mip level from its 2x2 block in the finer one.
 *        Always works in float: integer formats arrive normalized to [0,1].
 * @param src The four source texels (row-major 2x2), `channels` floats each.
 * @param dst Output texel, `channels` floats.
 * @param channels Number of channels per texel.
 */
using MipRule = std::function<void(const float* const src[4], float* dst, int channels)>;

/// Ready-made MipRule implementations.
namespace mip {

/// Box average of the 2x2 block, per channel.
void boxRule(const float* const src[4], float* dst, int channels);

/// Maximum of the 2x2 block, per channel.
void maxRule(const float* const src[4], float* dst, int channels);

/// Minimum of the 2x2 block, per channel.
void minRule(const float* const src[4], float* dst, int channels);

/**
 * @brief Mip rule for baked relief planes (RGBA: depth in B, coverage mask in A).
 *        B becomes the minimum depth over the covered texels (A >= 0.5) of the block and
 *        A becomes 1 if any texel is covered, else 0; R and G are zeroed.
 *        Requires 4 channels; `channels` is ignored.
 */
void coverageMinRule(const float* const src[4], float* dst, int channels);

}  // namespace mip

/**
 * @brief A row-major image stored as raw bytes, one buffer per mip level. Empty `mips`
 *        means "no texture".
 *
 *        Level i is max(1, width >> i) x max(1, height >> i); `mips[0]` is the base image.
 */
struct Texture {
    /// One mip level.
    struct Mip {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> pixels;  ///< Raw bytes, `w * h * info().texelSize()` of them.
    };

    PixelFormat format = PixelFormat::RGBA8;
    std::vector<Mip> mips;

    /**
     * @brief Loads an image file as the base level and builds its mip chain.
     * @param path Path to the image file (anything stb_image decodes).
     * @param format Pixel format to convert to. Empty keeps the file's own channel layout
     *        and depth: float for Radiance `.hdr`, 16-bit for 16-bit files, 8-bit otherwise.
     *        Depth conversions are plain rescales (no gamma curve is applied).
     * @param rule Rule used to build the coarser mip levels; empty builds none.
     * @throws std::runtime_error if the file cannot be loaded.
     */
    explicit Texture(const std::string& path, std::optional<PixelFormat> format = std::nullopt,
                     const MipRule& rule = mip::boxRule);

    /**
     * @brief Creates a zero-filled single-level texture.
     * @param width Image width.
     * @param height Image height.
     * @param format Pixel format.
     */
    Texture(int width, int height, PixelFormat format);

    /**
     * @brief Creates a texture from base-level bytes already in memory.
     * @param width Image width.
     * @param height Image height.
     * @param format Format of `basePixels`.
     * @param basePixels `width * height` texels, row-major.
     * @param rule Rule used to build the coarser mip levels; empty builds none.
     */
    Texture(int width, int height, PixelFormat format, const uint8_t* basePixels,
            const MipRule& rule = mip::boxRule);

    /// @return Channel count and channel size of `format`.
    FormatInfo info() const { return formatInfo(format); }

    /// @return Number of mip levels stored.
    int mipCount() const { return int(mips.size()); }

    /// @return Width of the base level (0 if empty).
    int width() const { return mips.empty() ? 0 : mips[0].w; }

    /// @return Height of the base level (0 if empty).
    int height() const { return mips.empty() ? 0 : mips[0].h; }

    /// @return true if the image has base-level pixels and positive dimensions.
    bool valid() const { return !mips.empty() && mips[0].w > 0 && mips[0].h > 0 && !mips[0].pixels.empty(); }

    /// @return The base-level bytes (null if empty).
    uint8_t* data() { return mips.empty() ? nullptr : mips[0].pixels.data(); }
    const uint8_t* data() const { return mips.empty() ? nullptr : mips[0].pixels.data(); }

    /**
     * @brief Rebuilds the mip chain from the base level, replacing any coarser levels.
     *        Each level is max(1, size / 2) per side, down to 1x1; at odd sizes the 2x2
     *        block is clamped at the edge.
     * @param rule Rule used to derive each texel from its 2x2 block; empty keeps only
     *        the base level.
     */
    void generateMips(const MipRule& rule = mip::boxRule);
};

}  // namespace tex
