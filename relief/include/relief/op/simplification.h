/**
 * @file simplification.h
 * @brief op::Op that simplifies a mesh via Quadric Error Metrics (QEM)
 *        edge-collapse, with optional boundary/seam constraints.
 */
#pragma once
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <vector>

#include "relief/mesh.h"
#include "relief/op.h"

namespace op::simplification {

/// Controls how mesh boundaries are treated during simplification.
enum class BoundaryMode {
    None,              ///< No boundary constraint at all.
    Constraint,        ///< Soft penalty (perpendicular-plane quadric).
    ConstrainSeams,    ///< Soft penalty on boundary edges AND UV-seam edges (no locking).
    LockSeamVertices,  ///< Hard lock: never collapses an edge touching a boundary or UV-seam vertex.
};

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
    /// @param lockedEdges User-supplied edges (e.g. from brush selection) that must
    ///        never collapse, independent of boundaryMode.
    explicit SimplifyOp(int targetFaces, BoundaryMode boundaryMode = BoundaryMode::Constraint,
                         bool useOptimalCandidate = false, std::set<mesh::Edge> lockedEdges = {})
        : targetFaces_(targetFaces), boundaryMode_(boundaryMode),
          useOptimalCandidate_(useOptimalCandidate), lockedEdges_(std::move(lockedEdges)) {}

    void apply(mesh::Mesh& mesh) const override;

private:
    /// A candidate edge collapse: which vertices merge, where, at what cost.
    struct EdgeCollapse {
        int             v1, v2;
        Eigen::Vector3d target   = Eigen::Vector3d::Zero();
        double          cost     = 0.0;

        /// Per-incident-face UV pairing for this edge: (wedge at v1, wedge at
        /// v2, interpolated UV at `target`). Usually one entry; two if the edge
        /// is a UV seam (v1/v2 each carry a different UV per side).
        std::vector<std::tuple<int, int, Eigen::Vector2d>> uvTargets;

        /// Orders collapses cheapest-first when used with std::greater in a priority_queue.
        bool operator>(const EdgeCollapse& o) const { return cost > o.cost; }
    };
    /// Min-heap of candidate collapses, ordered by cost (EdgeCollapse::operator> above).
    using PQ = std::priority_queue<EdgeCollapse, std::vector<EdgeCollapse>, std::greater<EdgeCollapse>>;

    /// @brief Builds the fundamental error matrix for the plane ax+by+cz+d=0.
    /// @return The 4x4 quadric (p*p^T) associated with the plane.
    static Eigen::Matrix4d quadricFromPlane(double a, double b, double c, double d);
    /// @brief Evaluates the quadric error v^T*Q*v for the point (px, py, pz, 1).
    static double evalQuadric(const Eigen::Matrix4d& Q, double px, double py, double pz);
    /// @brief Solves the 3x3 system that minimizes the quadric error.
    /// @return true if the linear system has a solution (non-degenerate determinant).
    static bool solveQuadric(const Eigen::Matrix4d& Q, double& ox, double& oy, double& oz);
    /// @brief Interpolates UV coordinates along the segment (a, b) at the point `p`.
    static Eigen::Vector2d interpolateUVAlongSegment(const Eigen::Vector3d& p, const Eigen::Vector3d& a,
                                                      const Eigen::Vector2d& uvA, const Eigen::Vector3d& b,
                                                      const Eigen::Vector2d& uvB);

    /// @brief Runs the greedy edge-collapse loop against `mesh_`/`targetFaces_`.
    ///        Called by apply() after per-run state has been (re)initialized.
    void run() const;
    /// Computes the initial per-vertex quadric from adjacent face planes.
    void computeQ() const;
    /// Builds the collapse candidate between v1, v2, the midpoint, and (if
    /// useOptimalCandidate_) the quadric's unconstrained optimum, picking the
    /// one with lowest quadric error.
    bool computeCollapse(int v1, int v2, EdgeCollapse& out) const;
    /// @return The distinct (wedge at v1, wedge at v2) pairs actually
    ///         used together by some face incident to edge (v1,v2).
    std::vector<std::pair<int, int>> edgeUVPairs(int v1, int v2) const;
    /// Merges `remove` into `keep` at the given position: retargets every
    /// wedge belonging to `remove` onto `keep` in place (no Face needs to
    /// change which wedge it references) and updates `vertexToVertices_`.
    void mergeVertexPair(int keep, int remove, const Eigen::Vector3d& pos,
                          const std::vector<std::tuple<int, int, Eigen::Vector2d>>& uvTargets) const;
    /// Builds the priority queue of candidate collapses from current adjacency.
    void buildQueue(PQ& pq) const;
    /// Recomputes and re-enqueues the collapse cost of every edge touching
    /// `keep`, using `vertexToVertices_[keep]` (called right after `keep` inherits
    /// the faces of a removed vertex).
    void refreshAround(int keep, PQ& pq, std::set<mesh::Edge>& invalidEdges) const;
    /// Adds perpendicular-plane quadrics along boundary edges so boundary
    /// vertices resist being pulled off the mesh silhouette.
    /// @param weight Relative strength of the boundary quadric.
    void addBoundaryConstraints(double weight = 1000.0) const;
    /// Marks boundaryVertex_[i] for every vertex touching a boundary edge or a UV seam.
    void markBoundaryVertices() const;
    /// @return true if the edge (a,b) is locked from collapsing, either by
    ///         lockSeamEdges_, or because either endpoint touches a user-locked
    ///         edge (see userLockedVertex_).
    bool edgeLocked(int a, int b) const;
    /// Builds the collapse candidate for edge (p,q), deciding whether it
    /// should be locked or handled normally.
    /// @return false if the edge can't be collapsed (locked).
    bool buildCandidate(mesh::Edge edge, EdgeCollapse& out) const;

    // ── Config, fixed at construction ──────────────────────────────────────
    int targetFaces_;
    BoundaryMode boundaryMode_;
    bool useOptimalCandidate_;
    std::set<mesh::Edge> lockedEdges_;

    // ── Per-apply() scratch state ────────────────────────────────────────────
    // apply() is const (op::Op's interface), but the greedy edge-collapse
    // algorithm below needs mutable working state scoped to a single call;
    // apply() resets all of it before calling run().
    mutable mesh::Mesh* mesh_ = nullptr;
    mutable std::map<mesh::Edge, EdgeCollapse> edgeMap_;
    mutable std::vector<std::set<int>> vertexToVertices_;
    mutable bool lockSeamEdges_ = false;
    mutable std::vector<bool> boundaryVertex_;
    /// Per-vertex flag: true if the vertex is an endpoint of any edge in
    /// lockedEdges_. Protects that vertex from being removed by *any*
    /// collapse, not just the specific locked edge -- see edgeLocked().
    mutable std::vector<bool> userLockedVertex_;
};

} // namespace op::simplification
