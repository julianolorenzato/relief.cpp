/**
 * @file smoothing.h
 * @brief op::Op that runs uniform Laplacian smoothing on a mesh.
 */
#pragma once
#include "relief/op.h"

namespace op::smoothing {

/**
 * @brief Runs uniform Laplacian smoothing on a mesh's vertex positions.
 */
class SmoothOp : public op::Op {
public:
    /// @param iterations Number of smoothing rounds to run.
    /// @param lambda Blend factor toward the neighbor average (0 = no movement, 1 = snap).
    explicit SmoothOp(int iterations = 1, double lambda = 0.5)
        : iterations_(iterations), lambda_(lambda) {}

    void apply(mesh::Mesh& mesh) const override;

private:
    int iterations_;
    double lambda_;
};

} // namespace op::smoothing
