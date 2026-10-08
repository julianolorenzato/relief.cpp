/**
 * @file triangulate.h
 * @brief Triangulation of simple 2D polygons into a small planar mesh.
 */
#pragma once
#include <Eigen/Dense>
#include <vector>

#include "relief/mesh.h"

namespace triangulate {

/**
 * @brief A self-contained planar patch with the same vertex/wedge/face shape
 *        as mesh::Mesh (and op::bboxproj::BBoxQuad).
 *
 * `vertices[i].pos` holds the 2D input point in x/y and 0 in z; `wedges[i]`
 * maps one-to-one onto `vertices[i]` (same index) with a zero UV, left for the
 * caller to fill in; every face is counter-clockwise in the x/y plane.
 */
struct Patch {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;
};

/**
 * @brief Triangulates the interior of a simple polygon by ear clipping.
 *
 * Handles either winding and collinear vertices. Holes and
 * self-intersecting outlines are not supported; for those, ear clipping
 * still terminates but the result is unspecified. Runs in O(n^2) for typical
 * input (O(n^3) worst case).
 *
 * @param polygon Ordered outline of the polygon, without repeating the first
 *                point at the end. Fewer than 3 points yields an empty patch.
 * @return Patch with `polygon.size()` vertices and (up to) `n - 2` faces.
 *         Input vertex i is output vertex i, so callers can map results back
 *         to their own data.
 */
Patch triangulatePolygon(const std::vector<Eigen::Vector2d>& polygon);

}  // namespace triangulate
