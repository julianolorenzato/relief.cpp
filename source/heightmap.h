#pragma once
#include "qem.h"
#include <vector>
#include <cstdint>
#include <functional>

using ProgressCb = std::function<void(int)>;

struct HeightmapResult {
    std::vector<float>   heights; // signed displacement per pixel (raw)
    std::vector<uint8_t> image;   // normalized 0-255 grayscale
    int   width  = 0;
    int   height = 0;
    float minH   = 0.0f;
    float maxH   = 0.0f;
    bool  valid  = false;
};

struct NormalmapResult {
    std::vector<uint8_t> image; // RGB, 3 bytes per pixel; flat normal = (128,128,255)
    int  width  = 0;
    int  height = 0;
    bool valid  = false;
};

class HeightmapBaker {
public:
    // Cast ray from simplified surface along face normal (bidirectional, unlimited range).
    static HeightmapResult bakeRayCast(
        const QEMSimplifier& simplified,
        const QEMSimplifier& original,
        int texWidth, int texHeight,
        ProgressCb cb = {});

    // Same as bakeRayCast but uses an explicit cage: the simplified surface is inflated
    // by cageOffset along each face normal. Rays shoot inward from the cage, limiting
    // the search to ±cageOffset around the simplified surface. Prevents erroneous hits
    // from distant geometry in complex/concave meshes.
    static HeightmapResult bakeRayCastCage(
        const QEMSimplifier& simplified,
        const QEMSimplifier& original,
        int texWidth, int texHeight,
        double cageOffset,
        ProgressCb cb = {});

    // Bakes a tangent-space normal map: for each texel on the simplified mesh's UV layout,
    // casts a bidirectional ray and samples the original mesh's smooth surface normal at
    // the hit point, then encodes it in the simplified mesh's tangent space as RGB.
    static NormalmapResult bakeNormalMap(
        const QEMSimplifier& simplified,
        const QEMSimplifier& original,
        int texWidth, int texHeight,
        ProgressCb cb = {});

private:
    using V3 = Eigen::Vector3d;

    struct TexelSample {
        V3    pos;
        V3    normal;
        V3    tangent;
        V3    bitangent;
        int   vi[3];
        float w[3];
        bool  valid = false;
    };

    static std::vector<TexelSample> rasterizeUV(
        const QEMSimplifier& mesh, int W, int H);

    static bool rayTriangle(
        const V3& orig, const V3& dir,
        const V3& a,    const V3& b, const V3& c,
        double& t);

    // Möller-Trumbore returning t and barycentric coords u, v at the hit point.
    // The hit position = a*(1-u-v) + b*u + c*v.
    static bool rayTriangleBarycentric(
        const V3& orig, const V3& dir,
        const V3& a,    const V3& b, const V3& c,
        double& t, double& u, double& v);

    static void normalize(HeightmapResult& r);
};
