/**
 * @file pipeline.cpp
 * @brief Pipeline/Op application: see pipeline.h.
 */
#include "relief/pipeline.h"

#include <sstream>
#include "relief/inflate.h"

namespace pipeline {

void applyOp(mesh::Mesh& mesh, const Op& op) {
    switch (op.type) {
        case OpType::Simplify: {
            simplification::Simplifier simplifier(mesh);
            simplifier.boundaryMode = op.params.boundaryMode;
            simplifier.useOptimalCandidate = op.params.useOptimalCandidate;
            simplifier.run(op.params.targetFaces);
            break;
        }
        case OpType::Inflate:
            inflate::applyOffset(mesh, op.params.inflateOffset);
            break;
        case OpType::Smooth:
            mesh.smooth(op.params.smoothIterations, op.params.smoothLambda);
            break;
    }
}

void applyPipeline(mesh::Mesh& mesh, const Pipeline& pipeline) {
    for (const auto& op : pipeline) applyOp(mesh, op);
}

std::string describe(const Pipeline& pipeline) {
    std::ostringstream out;
    for (size_t i = 0; i < pipeline.size(); i++) {
        if (i > 0) out << " -> ";
        const auto& op = pipeline[i];
        switch (op.type) {
            case OpType::Simplify:
                out << "Simplify(" << op.params.targetFaces << " faces)";
                break;
            case OpType::Inflate:
                out << "Inflate(" << (op.params.inflateOffset >= 0.0 ? "+" : "")
                    << op.params.inflateOffset << ")";
                break;
            case OpType::Smooth:
                out << "Smooth(" << op.params.smoothIterations << ", "
                    << op.params.smoothLambda << ")";
                break;
        }
    }
    if (pipeline.empty()) out << "(empty)";
    return out.str();
}

} // namespace pipeline
