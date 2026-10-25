/**
 * @file geometry.h
 * @brief Procedural geometry helpers used by the GUI renderers.
 */
#pragma once

#include <vector>

namespace geometry
{

    /**
     * @brief Builds a unit-radius lat/long sphere (positions only, unlit
     *        glyph) used to render the light orb.
     * @param lonSegments Number of longitudinal subdivisions.
     * @param latSegments Number of latitudinal subdivisions.
     * @param[out] verts Vertex positions, 3 floats per vertex (appended).
     * @param[out] idxs Triangle indices into `verts` (appended).
     */
    void buildSphereVerts(int lonSegments, int latSegments,
                          std::vector<float> &verts,
                          std::vector<unsigned int> &idxs);

} // namespace geometry
