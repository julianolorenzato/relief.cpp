/**
 * @file metrics_diag.cpp
 * @brief Diagnostic tool: sanity-checks metrics::imagePSNR on synthetic
 *        buffers (no GPU needed).
 */
#include <cmath>
#include <iostream>
#include <vector>

#include "relief/metrics.h"

namespace {

void checkPSNR() {
    constexpr int kWidth = 4, kHeight = 4, kChannels = 4;
    constexpr int kPixels = kWidth * kHeight;

    std::vector<uint8_t> identicalA(kPixels * kChannels, 128);
    std::vector<uint8_t> identicalB = identicalA;
    double identicalPSNR = metrics::imagePSNR(identicalA.data(), identicalB.data(), kWidth, kHeight, kChannels);
    std::cout << "identical images PSNR: " << identicalPSNR << " dB (expect +inf)\n";

    std::vector<uint8_t> offsetB = identicalA;
    for (size_t i = 0; i < offsetB.size(); i += kChannels) offsetB[i] += 10; // +10 on the red channel only
    double offsetPSNR = metrics::imagePSNR(identicalA.data(), offsetB.data(), kWidth, kHeight, kChannels);
    // MSE = (10^2 * kPixels) / (kPixels * 3 channels) = 100/3; PSNR = 10*log10(255^2 / (100/3)).
    double expected = 10.0 * std::log10((255.0 * 255.0) / (100.0 / 3.0));
    std::cout << "offset-red-channel PSNR: " << offsetPSNR << " dB (expect ~" << expected << " dB)\n";
}

} // namespace

int main() {
    checkPSNR();
    return 0;
}
