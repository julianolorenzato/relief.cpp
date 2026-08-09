/**
 * @file uv_atlas.h
 * @brief UV-island detection and Offset_Map baking, letting relief-mapping
 *        rays "leap" across UV seams between islands.
 */
#pragma once
#include "relief/qem.h"
#include <vector>

/// Per-texel atlas leap data baked for relief mapping across UV-island boundaries.
/// Matches the layout expected by RTMA_Functions.ush's Offset_Map / PerformIslandLeap:
///   - xy = UV-space translation to add to the ray's current position to jump into
///          the neighboring island
///   - z  = rotation (in turns, i.e. angle / (2*pi)) to apply to the tangent direction
///   - w  = validity flag (> 0 means a leap is defined at this texel)
struct OffsetMapResult {
    std::vector<float> data; ///< RGBA32F, row-major, width*height*4.
    int width  = 0;
    int height = 0;
};

/// UV-island detection and cross-seam offset map baking.
namespace UVAtlas {

/**
 * @brief Assigns each active face an island id via flood fill over
 *        3D-edge-adjacent faces whose UV coordinates agree at the shared edge
 *        (within epsilon). Faces sharing a 3D edge but disagreeing on UV at
 *        that edge are considered seam-separated (different islands).
 * @param mesh Mesh to partition into UV islands.
 * @return One island id per face, in face order; removed faces get id -1.
 */
std::vector<int> detectIslands(const QEMSimplifier& mesh);

/**
 * @brief Bakes the Offset_Map: for texels within `seamBandTexels` of a UV
 *        seam edge that crosses an island boundary, encodes the
 *        translation/rotation needed to continue a relief-mapping ray into
 *        the neighboring island. Assumes uniform UV texel density across
 *        islands (no per-island UV scale correction).
 * @param mesh Mesh whose UV layout defines the seams.
 * @param faceIsland Per-face island id, as produced by detectIslands().
 * @param width Output map width in texels.
 * @param height Output map height in texels.
 * @param seamBandTexels Half-width, in texels, of the band around each seam
 *        edge within which leap data is baked.
 * @return The baked offset map.
 */
OffsetMapResult bakeOffsetMap(
    const QEMSimplifier& mesh,
    const std::vector<int>& faceIsland,
    int width, int height,
    int seamBandTexels);

} // namespace UVAtlas
