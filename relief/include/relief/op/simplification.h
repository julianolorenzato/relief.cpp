/**
 * @file simplification.h
 * @brief op::Op wrapping simplification::Simplifier.
 */
#pragma once
#include "relief/op.h"
#include "relief/simplification.h"

namespace op::simplification {

/**
 * @brief Runs Quadric Error Metrics edge-collapse simplification down to a
 *        target face count.
 */
class SimplifyOp : public op::Op {
public:
    /// @param targetFaces Desired number of faces to stop at.
    /// @param boundaryMode How mesh boundaries are treated during simplification.
    /// @param useOptimalCandidate Whether to consider the quadric's unconstrained
    ///        optimum as a collapse-target candidate.
    explicit SimplifyOp(int targetFaces,
                         ::simplification::BoundaryMode boundaryMode = ::simplification::BoundaryMode::Constraint,
                         bool useOptimalCandidate = false)
        : targetFaces_(targetFaces), boundaryMode_(boundaryMode), useOptimalCandidate_(useOptimalCandidate) {}

    void apply(mesh::Mesh& mesh) const override;

private:
    int targetFaces_;
    ::simplification::BoundaryMode boundaryMode_;
    bool useOptimalCandidate_;
};

} // namespace op::simplification
