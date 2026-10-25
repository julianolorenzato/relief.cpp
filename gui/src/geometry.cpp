/**
 * @file geometry.cpp
 * @brief Implementation of the procedural geometry helpers.
 */
#include "gui/geometry.h"
#include <cmath>

namespace geometry
{

    void buildSphereVerts(int lonSegments, int latSegments,
                          std::vector<float> &verts,
                          std::vector<unsigned int> &idxs)
    {
        const float pi = 3.14159265358979323846f;
        for (int lat = 0; lat <= latSegments; lat++)
        {
            float theta = float(lat) * pi / float(latSegments);
            float sinTheta = sinf(theta), cosTheta = cosf(theta);
            for (int lon = 0; lon <= lonSegments; lon++)
            {
                float phi = float(lon) * 2.0f * pi / float(lonSegments);
                float sinPhi = sinf(phi), cosPhi = cosf(phi);
                verts.push_back(sinTheta * cosPhi);
                verts.push_back(cosTheta);
                verts.push_back(sinTheta * sinPhi);
            }
        }
        int stride = lonSegments + 1;
        for (int lat = 0; lat < latSegments; lat++)
        {
            for (int lon = 0; lon < lonSegments; lon++)
            {
                unsigned int a = lat * stride + lon;
                unsigned int b = a + stride;
                idxs.push_back(a);
                idxs.push_back(b);
                idxs.push_back(a + 1);
                idxs.push_back(a + 1);
                idxs.push_back(b);
                idxs.push_back(b + 1);
            }
        }
    }

} // namespace geometry
