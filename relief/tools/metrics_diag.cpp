/**
 * @file metrics_diag.cpp
 * @brief Diagnostic tool: sanity-checks metrics::imagePSNR on synthetic
 *        buffers (no GPU needed), then loads a mesh and prints face counts
 *        before/after a couple of hand-picked pipeline::Pipeline runs.
 */
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "relief/mesh.h"
#include "relief/mesh/io.h"
#include "relief/metrics.h"
#include "relief/pipeline.h"

using namespace mesh;
using namespace mesh::io;

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

void checkPipeline(const std::string& path) {
    Mesh original;
    if (!loadMesh(original, path)) {
        std::cerr << "failed to load " << path << "\n";
        return;
    }
    std::cout << "original: " << original.faceCount() << " faces, " << original.vertexCount()
               << " vertices\n";

    {
        Mesh copy = original;
        pipeline::OpParams simplifyOnlyParams;
        simplifyOnlyParams.targetFaces = original.faceCount() / 3;
        pipeline::Pipeline p = {
            {pipeline::OpType::Simplify, simplifyOnlyParams},
        };
        pipeline::applyPipeline(copy, p);
        std::cout << pipeline::describe(p) << " -> " << copy.faceCount() << " faces\n";
    }
    {
        Mesh copy = original;
        pipeline::OpParams simplifyParams;
        simplifyParams.targetFaces = original.faceCount() / 3;
        pipeline::OpParams smoothParams;
        smoothParams.smoothIterations = 2;
        smoothParams.smoothLambda = 0.5;
        pipeline::Pipeline p = {
            {pipeline::OpType::Simplify, simplifyParams},
            {pipeline::OpType::Smooth, smoothParams},
        };
        pipeline::applyPipeline(copy, p);
        std::cout << pipeline::describe(p) << " -> " << copy.faceCount() << " faces\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    checkPSNR();
    if (argc >= 2) checkPipeline(argv[1]);
    else std::cout << "(pass a mesh path to also exercise pipeline::applyPipeline)\n";
    return 0;
}
