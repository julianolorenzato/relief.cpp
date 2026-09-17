/**
 * @file pipeline_search.cpp
 * @brief Pipeline search implementation: see pipeline_search.h.
 */
#include "relief/pipeline_search.h"

#include <algorithm>
#include <random>

namespace pipeline {

namespace {

std::vector<double> linspace(double lo, double hi, int n) {
    std::vector<double> out;
    if (n <= 1) {
        out.push_back((lo + hi) / 2.0);
        return out;
    }
    for (int i = 0; i < n; i++) out.push_back(lo + (hi - lo) * (double)i / (double)(n - 1));
    return out;
}

std::vector<double> inflateChoices(const SearchConfig& cfg) {
    if (cfg.fixedInflate) return {*cfg.fixedInflate};
    return linspace(cfg.inflateRange.first, cfg.inflateRange.second, std::max(1, cfg.inflateSamples));
}

std::vector<std::pair<int, double>> smoothChoices(const SearchConfig& cfg) {
    if (cfg.fixedSmooth) return {*cfg.fixedSmooth};
    std::vector<std::pair<int, double>> out;
    for (int iterations : cfg.smoothIterationChoices)
        for (double lambda : cfg.smoothLambdaChoices) out.push_back({iterations, lambda});
    if (out.empty()) out.push_back({1, 0.5});
    return out;
}

Pipeline buildPipeline(const std::vector<OpType>& order, const SearchConfig& cfg, double inflateVal,
                       std::pair<int, double> smoothVal) {
    Pipeline p;
    for (OpType type : order) {
        Op op;
        op.type = type;
        switch (type) {
            case OpType::Simplify:
                op.params.targetFaces = cfg.fixedTargetFaces.value_or(0);
                op.params.boundaryMode = simplification::BoundaryMode::Constraint;
                op.params.useOptimalCandidate = true;
                break;
            case OpType::Inflate:
                op.params.inflateOffset = inflateVal;
                break;
            case OpType::Smooth:
                op.params.smoothIterations = smoothVal.first;
                op.params.smoothLambda = smoothVal.second;
                break;
        }
        p.push_back(op);
    }
    return p;
}

/// Identifies one candidate to evaluate: which op order and which
/// free-parameter choice indices to use.
struct Combo {
    int permIdx, inflateIdx, smoothIdx;
};

} // namespace

std::vector<SearchCandidate> search(const mesh::Mesh& source, const SearchConfig& cfg,
                                     const ScoreFn& score, int topN, const ProgressFn& progress) {
    auto byUnderlying = [](OpType a, OpType b) { return (int)a < (int)b; };

    std::vector<OpType> order = cfg.ops;
    std::sort(order.begin(), order.end(), byUnderlying);

    std::vector<std::vector<OpType>> permutations;
    do {
        permutations.push_back(order);
    } while (std::next_permutation(order.begin(), order.end(), byUnderlying));

    bool hasInflate = std::find(cfg.ops.begin(), cfg.ops.end(), OpType::Inflate) != cfg.ops.end();
    bool hasSmooth = std::find(cfg.ops.begin(), cfg.ops.end(), OpType::Smooth) != cfg.ops.end();

    std::vector<double> inflateVals = hasInflate ? inflateChoices(cfg) : std::vector<double>{0.0};
    std::vector<std::pair<int, double>> smoothVals =
        hasSmooth ? smoothChoices(cfg) : std::vector<std::pair<int, double>>{{1, 0.5}};

    std::vector<Combo> combos;
    for (size_t pi = 0; pi < permutations.size(); pi++)
        for (size_t ii = 0; ii < inflateVals.size(); ii++)
            for (size_t si = 0; si < smoothVals.size(); si++)
                combos.push_back({(int)pi, (int)ii, (int)si});

    std::vector<Combo> selected;
    if ((int)combos.size() <= cfg.maxEvaluations) {
        selected = combos;
    } else {
        // Budget is smaller than the full grid: guarantee every permutation
        // is tried at least once (with a random parameter choice), then fill
        // the remaining budget with randomly shuffled combos from the grid.
        std::mt19937 rng(cfg.randomSeed);
        std::uniform_int_distribution<int> inflatePick(0, (int)inflateVals.size() - 1);
        std::uniform_int_distribution<int> smoothPick(0, (int)smoothVals.size() - 1);
        for (size_t pi = 0; pi < permutations.size(); pi++)
            selected.push_back({(int)pi, inflatePick(rng), smoothPick(rng)});

        std::shuffle(combos.begin(), combos.end(), rng);
        for (const auto& c : combos) {
            if ((int)selected.size() >= cfg.maxEvaluations) break;
            selected.push_back(c);
        }
        if ((int)selected.size() > cfg.maxEvaluations) selected.resize(cfg.maxEvaluations);
    }

    std::vector<SearchCandidate> results;
    results.reserve(selected.size());
    int total = (int)selected.size();
    for (int i = 0; i < total; i++) {
        const auto& c = selected[i];
        Pipeline p =
            buildPipeline(permutations[c.permIdx], cfg, inflateVals[c.inflateIdx], smoothVals[c.smoothIdx]);
        results.push_back({p, score(source, p)});
        if (progress && !progress(i + 1, total)) break;
    }

    std::sort(results.begin(), results.end(),
              [](const SearchCandidate& a, const SearchCandidate& b) { return a.psnr > b.psnr; });
    if ((int)results.size() > topN) results.resize(topN);
    return results;
}

} // namespace pipeline
