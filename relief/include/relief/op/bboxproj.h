/**
 * @file bboxproj.h
 * @brief op::Op that replaces a mesh's geometry with a plain axis-aligned
 *        bounding box.
 */
#pragma once

#include "relief/op.h"
namespace op::bboxproj {

/// @brief Axis-aligned bounding box: min/max corners.
struct BBox {
    Eigen::Vector3d min = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d max = Eigen::Vector3d::Constant(-1e18);
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with a plain closed
 *        axis-aligned bounding box (8 vertices, 12 triangles), with no
 *        projection of the original mesh's faces onto it and no UV
 *        preservation (each box face gets a synthetic unit-square UV).
 */
class BBoxProjectionOp : public op::Op {
   public:
    explicit BBoxProjectionOp() {}

    void apply(mesh::Mesh &mesh) const override;
};
}  // namespace op::bboxproj
