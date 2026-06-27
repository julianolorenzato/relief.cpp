#pragma once
#include "relief/qem.h"
#include "relief/uv_atlas.h"
#include <QImage>
#include <vector>
#include <string>
#include <functional>

using TexturePrepProgressCb = std::function<void(int)>;

// One channel-set mip pyramid: mips[0] is full res (width x height), each
// subsequent level is half the resolution of the previous (down to 1x1).
// Each level stores `channels` floats per texel, row-major.
// Normal-map pyramids store raw unit-vector components in [-1,1] (not
// remapped to [0,1]) — remap on upload/export.
struct MipPyramid {
    std::vector<std::vector<float>> mips;
    int width  = 0; // mip 0 dimensions
    int height = 0;
    int channels = 4;

    int levelCount() const { return (int)mips.size(); }
};

struct TexturePrepResult {
    MipPyramid colorMap;      // RGBA, average-downsampled
    MipPyramid reliefMap;     // R=min depth (mip bound), G=max depth (mip bound), B=seam/offset mask (dilated max/OR), A=reserved(0)
    MipPyramid normalMap;     // XYZ unit vector in [-1,1], average-downsampled + renormalized per level
    OffsetMapResult offsetMap; // mip0 only — see uv_atlas.h

    bool valid = false;
};

class TexturePrepBaker {
public:
    // colorImg/depthImg/normalImg: the 3 input textures, already in memory —
    // the model's own color/normal maps and the baked heightmap, so no
    // separate file loading is needed.
    // workRes: square working resolution the output textures are baked at.
    // seamBandTexels: width (in texels) of the atlas-leap band baked around UV seams.
    static TexturePrepResult bake(
        const QEMSimplifier& mesh,
        const QImage& colorImg,
        const QImage& depthImg,
        const QImage& normalImg,
        int workRes,
        int seamBandTexels,
        TexturePrepProgressCb cb = {});
};
