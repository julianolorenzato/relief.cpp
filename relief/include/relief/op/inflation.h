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
    double offset_;
};

} // namespace op::inflation
