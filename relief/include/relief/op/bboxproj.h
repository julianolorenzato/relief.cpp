/**
 * @file bboxproj.h
 * @brief op::Op that replaces a mesh's geometry with a plain axis-aligned
 *        bounding box.
 */
#pragma once

#include <array>
#include <vector>

#include "relief/op.h"
namespace op::bboxproj {

/// One triangle within a BBoxFace, indexing into that same face's
/// `vertices` array.
struct BBoxTriangle {
    int v[3];  ///< Indices into the enclosing BBoxFace::vertices.
};

/// One of a BBox's 6 faces: an independently triangulated planar patch.
/// Vertices are 2D since every vertex on a given face lies in that face's
/// plane; the enclosing BBox's `min`/`max`, plus which of the 6 faces this
/// is, determine how these local coordinates map into 3D.
struct BBoxFace {
    std::vector<Eigen::Vector2d> vertices;

    /// Synthetic unit-square UV of each entry in `vertices` (same index).
    std::vector<Eigen::Vector2d> uvs;

    std::vector<BBoxTriangle> triangles;
};

/// @brief Axis-aligned bounding box: min/max corners, plus each of its 6
///        faces' own vertex/UV/triangle data.
struct BBox {
    Eigen::Vector3d min = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d max = Eigen::Vector3d::Constant(-1e18);

    /// The box's 6 faces, in order [-X, +X, -Y, +Y, -Z, +Z].
    std::array<BBoxFace, 6> faces;
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with a plain closed
 *        axis-aligned bounding box (24 vertices, 4 per face and unwelded
 *        across faces, 12 triangles), with no projection of the original
 *        mesh's faces onto it and no UV preservation (each box face gets a
 *        synthetic unit-square UV).
 */
class BBoxProjectionOp : public op::Op {
   public:
    explicit BBoxProjectionOp() {}

    void apply(mesh::Mesh &mesh) const override;
};
}  // namespace op::bboxproj
