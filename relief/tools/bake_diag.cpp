/**
 * @file bake_diag.cpp
 * @brief Diagnostic tool: bakes a mesh's impostor planes and checks the result's invariants
 *        (depth range and silhouette sentinel, decodable unit normals, dilated color/normals
 *        past the silhouette), printing the coverage and memory per plane. Exits nonzero on
 *        a violated invariant.
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

    try {
        Mesh mesh(argv[1]);
        mesh.bake(resolution);

        static const char *const names[6] = {"X+", "X-", "Y+", "Y-", "Z+", "Z-"};
        int failures = 0;
        size_t bytes = 0;
        for (int i = 0; i < 6; i++) {
            const auto &depthTex = mesh.depthPlanes()[i];
            const auto &colorTex = mesh.colorPlanes()[i];
            const auto &normalTex = mesh.normalPlanes()[i];
            if (!depthTex || !colorTex || !normalTex) {
                std::cerr << names[i] << ": missing plane\n";
                failures++;
                continue;
            }
            bytes += depthTex->mips[0].pixels.size() + colorTex->mips[0].pixels.size() +
                     normalTex->mips[0].pixels.size();

            const size_t count = (size_t)resolution * resolution;
            const uint8_t *depthBytes = depthTex->data();
            const uint8_t *color = colorTex->data();
            const uint8_t *normal = normalTex->data();

            size_t covered = 0, badDepth = 0, badNormal = 0, undilated = 0;
            for (size_t t = 0; t < count; t++) {
                uint16_t d;
                std::memcpy(&d, depthBytes + t * 2, 2);
                if (d != Mesh::kDepthEmpty) covered++;
                if (d > Mesh::kDepthMax && d != Mesh::kDepthEmpty) badDepth++;

                float n[3];
                octDecode(normal[t * 2] / 255.0f * 2.0f - 1.0f, normal[t * 2 + 1] / 255.0f * 2.0f - 1.0f, n);
                if (!std::isfinite(n[0] + n[1] + n[2])) badNormal++;
                if (color[t * 4 + 3] != 255 && d != Mesh::kDepthEmpty) badNormal++;
            }

            // A covered plane must dilate past its silhouette: with at least one covered texel and
            // one empty one, some empty texel must carry color (the border grew into it).
            if (covered > 0 && covered < count) {
                for (size_t t = 0; t < count && !undilated; t++) {
                    uint16_t d;
                    std::memcpy(&d, depthBytes + t * 2, 2);
                    if (d == Mesh::kDepthEmpty && color[t * 4 + 3] == 255) undilated = 1;
                }
                undilated = !undilated;
            }

            std::cout << names[i] << ": covered " << covered << "/" << count << " ("
                      << (100.0 * covered / count) << "%)"
                      << (badDepth ? "  BAD DEPTH" : "") << (badNormal ? "  BAD NORMAL" : "")
                      << (undilated ? "  NOT DILATED" : "") << "\n";
            if (badDepth || badNormal || undilated) failures++;
        }
        std::cout << "memory: " << bytes / 1024.0 << " KiB at " << resolution << "x" << resolution << "\n";
        return failures ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
