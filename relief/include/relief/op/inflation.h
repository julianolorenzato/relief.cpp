/**
 * @file inflation.h
 * @brief op::Op that moves every mesh vertex along its normal by a fixed offset.
 */
#pragma once
#include "relief/op.h"

namespace op::inflation {

/**
 * @brief Moves every mesh vertex along its normal by a fixed signed offset.
 *        Normals are recomputed from the mesh's current geometry on every
 *        apply(), with duplicate-position (UV-seam) vertices grouped so
 *        seams don't open.
 */
class InflateOp : public op::Op {
public:
    /// @param offset Signed distance to move each vertex along its normal.
    explicit InflateOp(double offset) : offset_(offset) {}

    void apply(mesh::Mesh& mesh) const override;

private:
    /// Per-vertex unit outward normal, with duplicate-position (UV-seam)
    /// vertices grouped so seams don't open when inflating: each copy would
    /// otherwise use only its own incident faces, the normals would diverge,
    /// and the seam would open a hole when inflating even with seam vertices
    /// locked.
    static std::vector<Eigen::Vector3d> computeVertexNormals(const mesh::Mesh& mesh);

    double offset_;
};

} // namespace op::inflation
