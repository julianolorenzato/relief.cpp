/**
 * @file op.h
 * @brief Base type for mesh-mutating operations. See op/simplification.h,
 *        op/smoothing.h, and op/inflation.h for the concrete operations.
 */
#pragma once
#include "relief/mesh.h"

namespace op {

/**
 * @brief Base class for an operation that mutates a mesh in place.
 */
class Op {
public:
    virtual ~Op() = default;

    /// @brief Applies this operation to `mesh` in place.
    /// @param mesh Mesh to mutate.
    virtual void apply(mesh::Mesh& mesh) const = 0;
};

} // namespace op
