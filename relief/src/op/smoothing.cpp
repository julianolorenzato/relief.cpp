#include "relief/op/smoothing.h"

namespace op::smoothing {

void SmoothOp::apply(mesh::Mesh& mesh) const {
    mesh.smooth(iterations_, lambda_);
}

} // namespace op::smoothing
