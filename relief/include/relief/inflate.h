/**
 * @file inflate.h
 * @brief Inflate/deflate a mesh along its per-vertex normals, with
 *        duplicate-position (UV-seam) vertices grouped so seams don't open.
 */
#pragma once
#include <vector>
#include <Eigen/Dense>
#include "relief/mesh.h"

namespace inflate {

/// Base positions and unit outward normals to inflate/deflate from, one
/// entry per Mesh::vertices index. Vertices at the same 3D position (e.g.
/// UV-seam duplicates) share the same averaged normal so they move together.
struct Baseline {
    std::vector<Eigen::Vector3d> basePositions;
    std::vector<Eigen::Vector3d> vertexNormals;
};

/// @brief Computes an inflate baseline from `mesh`'s current geometry.
/// @param mesh Mesh to read positions/faces from (not modified).
/// @return Baseline usable with apply() until the mesh's topology or
///         positions change again.
Baseline computeBaseline(const mesh::Mesh& mesh);

/// @brief Sets every non-removed vertex of `mesh` to `baseline.basePositions
///        + offset * baseline.vertexNormals`.
/// @param mesh Mesh to modify in place. Must match the mesh `baseline` was
///        computed from (same vertex count/order).
/// @param baseline Baseline positions/normals, from computeBaseline().
/// @param offset Signed distance to move each vertex along its normal.
void apply(mesh::Mesh& mesh, const Baseline& baseline, double offset);

/// @brief One-shot convenience: computeBaseline(mesh) then apply(mesh, ..., offset).
/// @param mesh Mesh to inflate/deflate in place.
/// @param offset Signed distance to move each vertex along its normal.
void applyOffset(mesh::Mesh& mesh, double offset);

} // namespace inflate
