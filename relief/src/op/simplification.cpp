#include "relief/op/simplification.h"

namespace op::simplification {

void SimplifyOp::apply(mesh::Mesh& mesh) const {
    ::simplification::Simplifier simplifier(mesh);
    simplifier.boundaryMode = boundaryMode_;
    simplifier.useOptimalCandidate = useOptimalCandidate_;
    simplifier.run(targetFaces_);
}

} // namespace op::simplification
