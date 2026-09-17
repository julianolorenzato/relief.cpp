/**
 * @file metrics.cpp
 * @brief PSNR implementation: see metrics.h.
 */
#include "relief/metrics.h"

#include <cmath>
#include <limits>

namespace metrics {

double imagePSNR(const uint8_t* a, const uint8_t* b, int width, int height, int channels,
                  bool ignoreAlpha) {
    const int alphaChannel = (ignoreAlpha && channels == 4) ? 3 : -1;
    double sumSquaredError = 0.0;
    long long sampleCount = 0;

    const long long pixelCount = (long long)width * height;
    for (long long p = 0; p < pixelCount; p++) {
        for (int c = 0; c < channels; c++) {
            if (c == alphaChannel) continue;
            long long idx = p * channels + c;
            double diff = (double)a[idx] - (double)b[idx];
            sumSquaredError += diff * diff;
            sampleCount++;
        }
    }

    if (sampleCount == 0 || sumSquaredError == 0.0) return std::numeric_limits<double>::infinity();

    double meanSquaredError = sumSquaredError / (double)sampleCount;
    constexpr double kPeak = 255.0;
    return 10.0 * std::log10((kPeak * kPeak) / meanSquaredError);
}

double averagePSNR(const std::vector<std::pair<const uint8_t*, const uint8_t*>>& pairs, int width,
                    int height, int channels, bool ignoreAlpha) {
    if (pairs.empty()) return std::numeric_limits<double>::infinity();

    double sum = 0.0;
    for (const auto& [a, b] : pairs) sum += imagePSNR(a, b, width, height, channels, ignoreAlpha);
    return sum / (double)pairs.size();
}

} // namespace metrics
