/**
 * @file metrics.h
 * @brief Image-based PSNR (Peak Signal-to-Noise Ratio), used to score how
 *        close a rendered candidate mesh looks to the original.
 */
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace metrics {

/// @brief Computes classic image PSNR, in dB, between two equally-sized images.
/// @param a First image's raw pixel bytes, row-major, `channels` bytes/pixel.
/// @param b Second image's raw pixel bytes, same layout as `a`.
/// @param width Image width in pixels.
/// @param height Image height in pixels.
/// @param channels Bytes per pixel (e.g. 4 for RGBA).
/// @param ignoreAlpha If true and channels == 4, the alpha byte is excluded
///        from the error sum.
/// @return PSNR in dB; +infinity if the images are pixel-identical.
double imagePSNR(const uint8_t* a, const uint8_t* b, int width, int height, int channels,
                  bool ignoreAlpha = true);

/// @brief Averages imagePSNR() across several same-size image pairs (e.g. one
///        pair per camera angle), for a single rotation-robust score.
/// @param pairs Each entry is a (imageA, imageB) raw-byte-buffer pair.
/// @param width Image width in pixels, shared by every pair.
/// @param height Image height in pixels, shared by every pair.
/// @param channels Bytes per pixel, shared by every pair.
/// @param ignoreAlpha Forwarded to imagePSNR() for each pair.
/// @return The arithmetic mean of imagePSNR() over `pairs`; +infinity if
///         `pairs` is empty or every pair is pixel-identical.
double averagePSNR(const std::vector<std::pair<const uint8_t*, const uint8_t*>>& pairs, int width,
                    int height, int channels, bool ignoreAlpha = true);

} // namespace metrics
