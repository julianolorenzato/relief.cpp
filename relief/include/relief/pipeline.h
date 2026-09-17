/**
 * @file pipeline.h
 * @brief An ordered, parameterized sequence of mesh operations (simplify,
 *        inflate, smooth) applicable to a Mesh, shared by manual validation
 *        setups and the automatic pipeline search.
 */
#pragma once
#include <string>
#include <vector>
#include "relief/mesh.h"
#include "relief/simplification.h"

namespace pipeline {

/// The mesh operations a Pipeline can chain together.
enum class OpType {
    Simplify,
    Inflate,
    Smooth,
};

/// Parameters for every op type; only the fields relevant to a given Op's
/// `type` are used.
struct OpParams {
    // Simplify
    int targetFaces = 0;
    simplification::BoundaryMode boundaryMode = simplification::BoundaryMode::Constraint;
    bool useOptimalCandidate = false;

    // Inflate
    double inflateOffset = 0.0;

    // Smooth
    int smoothIterations = 1;
    double smoothLambda = 0.5;
};

/// One configured step of a Pipeline.
struct Op {
    OpType type;
    OpParams params;
};

/// An ordered sequence of Ops, applied left to right.
using Pipeline = std::vector<Op>;

/// @brief Applies a single op to `mesh` in place.
void applyOp(mesh::Mesh& mesh, const Op& op);

/// @brief Applies every op in `pipeline` to `mesh` in place, in order.
void applyPipeline(mesh::Mesh& mesh, const Pipeline& pipeline);

/// @brief Human-readable summary, e.g. "Simplify(1200 faces) -> Inflate(+0.012) -> Smooth(2, 0.50)".
std::string describe(const Pipeline& pipeline);

} // namespace pipeline
