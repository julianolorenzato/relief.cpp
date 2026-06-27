#pragma once
#include "core/qem.h"
#include <vector>

// Per-texel atlas leap data baked for relief mapping across UV-island boundaries.
// Matches the layout expected by RTMA_Functions.ush's Offset_Map / PerformIslandLeap:
//   xy = UV-space translation to add to the ray's current position to jump into
//        the neighboring island
//   z  = rotation (in turns, i.e. angle / (2*pi)) to apply to the tangent direction
//   w  = validity flag (> 0 means a leap is defined at this texel)
struct OffsetMapResult {
    std::vector<float> data; // RGBA32F, row-major, width*height*4
    int width  = 0;
    int height = 0;
};

namespace UVAtlas {

// Assigns each active face an island id via flood fill over 3D-edge-adjacent faces
// whose UV coordinates agree at the shared edge (within epsilon). Faces sharing a
// 3D edge but disagreeing on UV at that edge are considered seam-separated (different
// islands). Removed faces get island id -1.
std::vector<int> detectIslands(const QEMSimplifier& mesh);

// Bakes the Offset_Map: for texels within `seamBandTexels` of a UV seam edge that
// crosses an island boundary, encodes the translation/rotation needed to continue a
// relief-mapping ray into the neighboring island. Assumes uniform UV texel density
// across islands (no per-island UV scale correction).
OffsetMapResult bakeOffsetMap(
    const QEMSimplifier& mesh,
    const std::vector<int>& faceIsland,
    int width, int height,
    int seamBandTexels);

} // namespace UVAtlas
