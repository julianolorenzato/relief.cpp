/**
 * @file heightmap.h
 * @brief Bakes a per-texel displacement (height) map that encodes the signed
 *        distance between a simplified mesh and its original, high-detail source.
 */
#pragma once
#include "relief/qem.h"
#include <vector>
#include <cstdint>
#include <functional>

/// Progress callback invoked with a value in [0, 100] while baking runs.
using ProgressCb = std::function<void(int)>;

/// Output of HeightmapBaker::bakeUVDistance: the raw signed displacement per
/// pixel plus a normalized grayscale preview image.
struct HeightmapResult {
    std::vector<float>   heights; ///< Signed displacement per pixel (raw).
    std::vector<uint8_t> image;   ///< Normalized 0-255 grayscale preview.
    int   width  = 0;
    int   height = 0;
    float minH   = 0.0f; ///< Minimum raw height value (used to normalize `image`).
    float maxH   = 0.0f; ///< Maximum raw height value (used to normalize `image`).
    bool  valid  = false;
};

/// @brief Bakes displacement maps by measuring, per UV texel, the distance
///        between the simplified mesh's surface and the original mesh.
class HeightmapBaker {
public:
    /**
     * @brief Rasterizes the simplified mesh's UV layout and, for each covered
     *        texel, measures the signed distance to the original mesh along
     *        the simplified surface normal, producing a displacement map.
     * @param simplified Low-poly mesh whose UVs define the output layout.
     * @param original High-detail mesh used as the distance reference.
     * @param texWidth Output map width in texels.
     * @param texHeight Output map height in texels.
     * @param cb Optional progress callback, invoked with 0-100.
     * @return Baked HeightmapResult; `valid` is false if baking failed.
     */
    static HeightmapResult bakeUVDistance(
        const QEMSimplifier& simplified,
        const QEMSimplifier& original,
        int texWidth, int texHeight,
        ProgressCb cb = {});

private:
    using V3 = Eigen::Vector3d;

    /// Per-texel sample of the simplified mesh's surface: interpolated
    /// position, normal, and whether the texel actually falls inside a UV triangle.
    struct TexelSample {
        V3    pos;
        V3    normal;
        bool  valid = false;
    };

    /// Rasterizes `mesh`'s UV triangles onto a WxH grid, interpolating
    /// position and normal per texel via barycentric coordinates.
    static std::vector<TexelSample> rasterizeUV(
        const QEMSimplifier& mesh, int W, int H);

    /// Fills `r.image` by remapping `r.heights` into normalized 0-255 grayscale.
    static void normalize(HeightmapResult& r);
};
