/**
 * @file pipeline_search.h
 * @brief Small parameter/order search over Pipelines: tries permutations of
 *        a chosen operation subset plus a bounded parameter grid, and scores
 *        each candidate via a caller-supplied function (typically rendered
 *        PSNR against the original mesh).
 */
#pragma once
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include "relief/mesh.h"
#include "relief/pipeline.h"

namespace pipeline {

/// Configures the search space and budget for search().
struct SearchConfig {
    /// Which op types to include; every permutation of this set is tried.
    std::vector<OpType> ops;

    /// Hard constraint used whenever Simplify is in `ops`.
    std::optional<int> fixedTargetFaces;
    /// Hard constraint used whenever Inflate is in `ops`; if empty, inflate
    /// offset is grid-searched over `inflateRange`.
    std::optional<double> fixedInflate;
    /// Hard constraint (iterations, lambda) used whenever Smooth is in
    /// `ops`; if empty, smoothing is grid-searched over the choice lists
    /// below.
    std::optional<std::pair<int, double>> fixedSmooth;

    /// Range to sample `inflateSamples` offsets from when fixedInflate is
    /// empty. Callers should derive this from the source mesh's bounding
    /// box (e.g. half its diagonal) before calling search().
    std::pair<double, double> inflateRange = {0.0, 0.0};
    int inflateSamples = 5;

    std::vector<int> smoothIterationChoices = {1, 2, 4, 8};
    std::vector<double> smoothLambdaChoices = {0.2, 0.5, 0.8};

    /// Caps the number of candidates actually evaluated. If the full
    /// permutation x grid space is larger, parameter combinations are
    /// uniformly randomly subsampled (every permutation is still tried at
    /// least once) using `randomSeed`.
    int maxEvaluations = 60;
    unsigned randomSeed = 1;
};

/// One evaluated candidate: the pipeline tried and the score it got.
struct SearchCandidate {
    Pipeline pipeline;
    double psnr;
};

/// Scores a candidate pipeline applied to a copy of `source`. Expected to
/// copy the mesh, call applyPipeline, then measure quality (e.g. render both
/// meshes and compute PSNR) — left to the caller since that typically needs
/// Qt/OpenGL, which this header does not depend on.
using ScoreFn = std::function<double(const mesh::Mesh& source, const Pipeline&)>;

/// Reports progress as `done` out of `total` candidates evaluated; returning
/// false cancels the remainder of the search.
using ProgressFn = std::function<bool(int done, int total)>;

/// @brief Runs the search described above.
/// @param source Mesh every candidate pipeline is applied to (via `score`).
/// @param cfg Search space/budget.
/// @param score Scoring callback (see ScoreFn).
/// @param topN Number of best candidates to keep, by descending PSNR.
/// @param progress Optional progress/cancel callback.
/// @return Up to `topN` candidates, sorted by descending PSNR.
std::vector<SearchCandidate> search(const mesh::Mesh& source, const SearchConfig& cfg,
                                     const ScoreFn& score, int topN = 5,
                                     const ProgressFn& progress = {});

} // namespace pipeline
