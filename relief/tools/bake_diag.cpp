/**
 * @file bake_diag.cpp
 * @brief Diagnostic tool: bakes a mesh's layered depth views and checks the result's invariants
 *        (hits sorted with the missing ones last, depth range, decodable unit normals, color and
 *        normals dilated past each layer's silhouette), printing the hit-count histogram and
 *        memory per axis. Exits nonzero on a violated invariant.
 */
#include "relief/mesh.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using namespace mesh;

namespace {

/// Octahedral decode, mirroring octDecode() in relief.frag.
void octDecode(float ex, float ey, float n[3]) {
    n[0] = ex;
    n[1] = ey;
    n[2] = 1.0f - std::abs(ex) - std::abs(ey);
    if (n[2] < 0.0f) {
        const float x = (1.0f - std::abs(n[1])) * (n[0] >= 0.0f ? 1.0f : -1.0f);
        const float y = (1.0f - std::abs(n[0])) * (n[1] >= 0.0f ? 1.0f : -1.0f);
        n[0] = x;
        n[1] = y;
    }
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    for (int k = 0; k < 3; k++) n[k] /= len;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: bake_diag <file.obj|.gltf|.glb> [resolution=256]\n";
        return 1;
    }
    const int resolution = argc > 2 ? std::stoi(argv[2]) : 256;
    constexpr int H = Mesh::kAxisHits;

    try {
        Mesh mesh(argv[1]);
        mesh.bake(resolution);

        static const char *const names[3] = {"X", "Y", "Z"};
        int failures = 0;
        size_t bytes = 0;
        const size_t count = (size_t)resolution * resolution;

        for (int axis = 0; axis < 3; axis++) {
            const auto &depthTex = mesh.depthPlanes()[axis];
            if (!depthTex) {
                std::cerr << names[axis] << ": missing depth plane\n";
                failures++;
                continue;
            }
            bytes += depthTex->mips[0].pixels.size();
            const uint8_t *depthBytes = depthTex->data();

            size_t histogram[H + 1] = {};
            size_t oddRays = 0, unsorted = 0, badDepth = 0, gaps = 0;
            for (size_t t = 0; t < count; t++) {
                uint16_t d[H];
                std::memcpy(d, depthBytes + t * H * 2, H * 2);
                int n = 0;
                while (n < H && d[n] != Mesh::kDepthEmpty) n++;
                histogram[n]++;
                if (n & 1) oddRays++;
                for (int k = 0; k < H; k++) {
                    if (k >= n && d[k] != Mesh::kDepthEmpty) gaps++;
                    if (k < n && d[k] > Mesh::kDepthMax) badDepth++;
                    if (k > 0 && k < n && d[k] < d[k - 1]) unsorted++;
                }
            }

            // Every layer: normals decode, color alpha says whether the ray has that hit, and a
            // layer with both covered and uncovered texels shows its dilated border.
            size_t badLayer = 0, undilated = 0;
            for (int k = 0; k < H; k++) {
                const auto &colorTex = mesh.colorPlanes()[axis * H + k];
                const auto &normalTex = mesh.normalPlanes()[axis * H + k];
                if (!colorTex || !normalTex) {
                    badLayer++;
                    continue;
                }
                bytes += colorTex->mips[0].pixels.size() + normalTex->mips[0].pixels.size();
                const uint8_t *color = colorTex->data();
                const uint8_t *normal = normalTex->data();

                size_t covered = 0;
                bool borderFilled = false;
                for (size_t t = 0; t < count; t++) {
                    uint16_t d;
                    std::memcpy(&d, depthBytes + (t * H + k) * 2, 2);
                    const bool has = d != Mesh::kDepthEmpty;
                    covered += has;
                    if (has && color[t * 4 + 3] != 255) badLayer++;
                    if (!has && color[t * 4 + 3] == 255) borderFilled = true;

                    float n[3];
                    octDecode(normal[t * 2] / 255.0f * 2.0f - 1.0f, normal[t * 2 + 1] / 255.0f * 2.0f - 1.0f, n);
                    if (!std::isfinite(n[0] + n[1] + n[2])) badLayer++;
                }
                if (covered > 0 && covered < count && !borderFilled) undilated++;
            }

            std::cout << names[axis] << ": rays with 0/1/2/3/4 hits:";
            for (int n = 0; n <= H; n++) std::cout << " " << histogram[n];
            std::cout << "  (odd " << oddRays << ")"
                      << (badDepth ? "  BAD DEPTH" : "") << (unsorted ? "  UNSORTED" : "")
                      << (gaps ? "  GAP" : "") << (badLayer ? "  BAD LAYER" : "")
                      << (undilated ? "  NOT DILATED" : "") << "\n";
            if (badDepth || unsorted || gaps || badLayer || undilated) failures++;
        }
        std::cout << "memory: " << bytes / 1024.0 << " KiB at " << resolution << "x" << resolution << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
