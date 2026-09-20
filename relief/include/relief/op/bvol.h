/**
 * @file bvol.h
 * @brief op::Op that replaces a mesh's geometry with its bounding volume
 *        (axis-aligned or oriented bounding box).
 */
#pragma once
#include <array>
#include "relief/op.h"

namespace op::bvol {

/// Selects which bounding volume BoundingVolumeOp replaces the mesh with.
enum class BoundingVolumeType {
    AABB, ///< Axis-aligned bounding box.
    OBB,  ///< Oriented bounding box (PCA-fitted).
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with an 8-vertex,
 *        12-triangle box mesh bounding the original geometry, either
 *        axis-aligned or oriented via PCA. Clears texture data, since the
 *        new box topology has no relation to the old UVs.
 */
class BoundingVolumeOp : public op::Op {
public:
    /// @param type Which bounding volume to compute.
    explicit BoundingVolumeOp(BoundingVolumeType type = BoundingVolumeType::AABB)
        : type_(type) {}

    void apply(mesh::Mesh& mesh) const override;

private:
    BoundingVolumeType type_;

    void applyAABB(mesh::Mesh& mesh) const;
    void applyOBB(mesh::Mesh& mesh) const;

    /// Builds an 8-vertex/12-triangle box mesh centered at `center`, with
    /// edges along `axes` (must be a right-handed orthonormal frame) and
    /// given `halfExtents` along each axis, overwriting
    /// mesh.vertices/wedges/faces and clearing texture data. UVs are all
    /// (0,0) -- there's no meaningful unwrap for a box.
    static void buildBoxMesh(mesh::Mesh& mesh,
                              const Eigen::Vector3d& center,
                              const std::array<Eigen::Vector3d, 3>& axes,
                              const Eigen::Vector3d& halfExtents);
};

} // namespace op::bvol
