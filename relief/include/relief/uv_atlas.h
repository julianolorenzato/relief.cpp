/**
 * @file uv_atlas.h
 * @brief UV-island detection and Offset_Map baking, letting relief-mapping
 *        rays "leap" across UV seams between islands.
 */
#pragma once
#include "relief/qem.h"
#include "relief/textures.h"
#include <vector>

/// UV-island detection and cross-seam offset map baking.
namespace UVAtlas {

/**
 * @brief Bakes the offset (atlas-leap) map: for texels within
 *        `seamBandTexels` of a UV seam edge that crosses an island boundary,
 *        encodes the translation/rotation/scale needed to continue a
 *        relief-mapping ray into the neighboring island. The per-edge
 *        transform is a similarity (rotation + uniform scale, fit to the two
 *        islands' respective UV texel density along that edge), not a pure
 *        rigid rotation, so both endpoints of the seam edge land exactly
 *        even when the islands don't share the same UV scale.
 *
 *        Matches the layout expected by RTMA_Functions.ush's Offset_Map /
 *        PerformIslandLeap:
 *          - xy = UV-space translation to add to the ray's current position
 *                 to jump into the neighboring island
 *          - z  = rotation (in turns, i.e. angle / (2*pi)) to apply to the
 *                 tangent direction
 *          - w  = validity flag (> 0 means a leap is defined at this texel)
 * @param mesh Mesh whose UV layout defines the seams.
 * @param width Output map width in texels.
 * @param height Output map height in texels.
 * @param seamBandTexels Half-width, in texels, of the band around each seam
 *        edge within which leap data is baked.
 * @return A single-level (never downsampled), 4-channel MipPyramid holding
 *         the baked offset map.
 */
MipPyramid buildOffsetMap(
    const QEMSimplifier& mesh,
    int width, int height,
    int seamBandTexels);

} // namespace UVAtlas
