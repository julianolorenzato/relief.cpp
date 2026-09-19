/**
 * @file simplification.h
 * @brief Mesh simplification via Quadric Error Metrics (QEM), with optional
 *        boundary/seam constraints.
 */
#pragma once
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <Eigen/Dense>
#include "relief/mesh.h"

namespace simplification {

/**
 * @brief Builds the fundamental error matrix for the plane ax+by+cz+d=0.
 * @param a Plane normal x component.
 * @param b Plane normal y component.
 * @param c Plane normal z component.
 * @param d Plane offset term.
 * @return The 4x4 quadric (p*p^T) associated with the plane.
 */
inline Eigen::Matrix4d quadricFromPlane(double a, double b, double c, double d) {
    Eigen::Vector4d p(a, b, c, d);
    return p * p.transpose();
}

/**
 * @brief Evaluates the quadric error v^T*Q*v for the point (px, py, pz, 1).
 * @param Q Accumulated quadric (sum of plane quadrics).
 * @param px Point x coordinate.
 * @param py Point y coordinate.
 * @param pz Point z coordinate.
 * @return Quadric error at the given point.
 */
inline double evalQuadric(const Eigen::Matrix4d& Q, double px, double py, double pz) {
    Eigen::Vector4d v(px, py, pz, 1.0);
    return v.dot(Q * v);
}

/**
 * @brief Solves the 3x3 system that minimizes the quadric error.
 * @param Q Accumulated quadric.
 * @param[out] ox Optimal position x coordinate, if a solution exists.
 * @param[out] oy Optimal position y coordinate, if a solution exists.
 * @param[out] oz Optimal position z coordinate, if a solution exists.
 * @return true if the linear system has a solution (non-degenerate determinant).
 */
inline bool solveQuadric(const Eigen::Matrix4d& Q, double& ox, double& oy, double& oz) {
    Eigen::Matrix4d A = Q;
    A(0,3) = A(1,3) = A(2,3) = 0.0;
    A(3,0) = A(3,1) = A(3,2) = 0.0;
    A(3,3) = 1.0;
    double det = A.determinant();
    if (std::abs(det) < 1e-10) return false;
    Eigen::Vector4d rhs(-Q(0,3), -Q(1,3), -Q(2,3), 0.0);
    Eigen::Vector4d result = A.inverse() * rhs;
    ox = result(0); oy = result(1); oz = result(2);
    return true;
}

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

/// Controls how mesh boundaries are treated during simplification.
enum class BoundaryMode {
    None,              ///< No boundary constraint at all.
    Constraint,        ///< Soft penalty (perpendicular-plane quadric).
    ConstrainSeams,    ///< Soft penalty on boundary edges AND UV-seam edges (no locking).
    LockSeamVertices,  ///< Hard lock: never collapses an edge touching a boundary or UV-seam vertex.
};

/**
 * @brief Simplifies a triangle mesh via iterative edge collapse driven by
 *        Quadric Error Metrics, with optional boundary/seam handling.
 */
class Simplifier {
public:
    /// @param mesh Mesh to simplify in place. Must outlive the Simplifier.
    explicit Simplifier(mesh::Mesh& mesh) : mesh_(mesh) {}

    BoundaryMode boundaryMode = BoundaryMode::Constraint;

    /// When true, adds the quadric's unconstrained optimum (solveQuadric) as
    /// an extra position candidate, alongside v1, v2, and the midpoint.
    bool useOptimalCandidate = false;

    /// @brief Runs greedy edge-collapse simplification until the target face
    ///        count is reached or no further collapse is cheap enough.
    /// @param targetFaces Desired number of faces to stop at.
    /// @param threshold Maximum acceptable collapse cost; collapses above it are skipped.
    void run(int targetFaces);

    /// User-supplied edges (e.g. from interactive brush selection) that must
    /// never collapse, independent of boundaryMode/lockSeamEdges. Keys use
    /// the same (small,large) vertex-id convention as Mesh::buildEdgeToFaces().
    /// Also marks both endpoints of every locked edge as fully protected
    /// (see userLockedVertex_): otherwise a collapse of some other, unlocked
    /// edge sharing an endpoint would silently remove that vertex -- and the
    /// locked edge along with it -- without ever failing the lock check.
    void setUserLockedEdges(std::set<mesh::Edge> edges) {
        userLockedEdges_ = std::move(edges);
        userLockedVertex_.assign(mesh_.vertices.size(), false);
        for (const auto& [a, b] : userLockedEdges_) {
            userLockedVertex_[a] = true;
            userLockedVertex_[b] = true;
        }
    }
    const std::set<mesh::Edge>& userLockedEdges() const { return userLockedEdges_; }

private:
    mesh::Mesh& mesh_;

    std::map<mesh::Edge, EdgeCollapse> edgeMap;

    /// Computes the initial per-vertex quadric from adjacent face planes.
    void computeQ();
    /// Builds the collapse candidate between v1, v2, the midpoint, and (if
    /// useOptimalCandidate) the quadric's unconstrained optimum, picking the
    /// one with lowest quadric error.
    bool computeCollapse(int v1, int v2, EdgeCollapse& out) const;
    /// @return The distinct (wedge at v1, wedge at v2) pairs actually
    ///         used together by some face incident to edge (v1,v2).
    std::vector<std::pair<int,int>> edgeUVPairs(int v1, int v2) const;
    /// Min-heap of candidate collapses, ordered by cost (EdgeCollapse::operator> above).
    using PQ = std::priority_queue<EdgeCollapse, std::vector<EdgeCollapse>, std::greater<EdgeCollapse>>;

    /// Vertex adjacency sets, kept in sync with the mesh by mergeVertexPair as collapses happen.
    std::vector<std::set<int>> vertexToVertices;

    /// Merges `remove` into `keep` at the given position: retargets every
    /// wedge belonging to `remove` onto `keep` in place (no Face needs to
    /// change which wedge it references) and updates `vertexToVertices`.
    void mergeVertexPair(int keep, int remove, const Eigen::Vector3d& pos,
                          const std::vector<std::tuple<int,int,Eigen::Vector2d>>& uvTargets);
    /// Builds the priority queue of candidate collapses from current adjacency.
    void buildQueue(PQ& pq);
    /// Recomputes and re-enqueues the collapse cost of every edge touching
    /// `keep`, using `vertexToVertices[keep]` (called right after `keep` inherits
    /// the faces of a removed vertex).
    void refreshAround(int keep, PQ& pq, std::set<mesh::Edge>& invalidEdges);
    /// Adds perpendicular-plane quadrics along boundary edges so boundary
    /// vertices resist being pulled off the mesh silhouette.
    /// @param weight Relative strength of the boundary quadric.
    void addBoundaryConstraints(double weight = 1000.0);

    /// Used when boundaryMode == LockSeamVertices: no boundary- or
    /// UV-seam-touching edge may be a collapse candidate.
    bool lockSeamEdges = false;
    std::vector<bool> boundaryVertex;
    /// Marks boundaryVertex[i] for every vertex touching a boundary edge or a UV seam.
    void markBoundaryVertices();

    /// @return true if the edge (a,b) is locked from collapsing, either by
    ///         lockSeamEdges, or because either endpoint touches a user-locked
    ///         edge (see userLockedVertex_).
    bool edgeLocked(int a, int b) const {
        if (lockSeamEdges && (boundaryVertex[a] || boundaryVertex[b])) return true;
        if (!userLockedVertex_.empty() && (userLockedVertex_[a] || userLockedVertex_[b])) return true;
        return false;
    }

    /// Builds the collapse candidate for edge (p,q), deciding whether it
    /// should be locked or handled normally.
    /// @return false if the edge can't be collapsed (locked).
    bool buildCandidate(mesh::Edge edge, EdgeCollapse& out) const;

    /// Backing storage for setUserLockedEdges()/userLockedEdges().
    std::set<mesh::Edge> userLockedEdges_;
    /// Per-vertex flag: true if the vertex is an endpoint of any edge in
    /// userLockedEdges_. Protects that vertex from being removed by *any*
    /// collapse, not just the specific locked edge -- see edgeLocked().
    std::vector<bool> userLockedVertex_;
};

} // namespace simplification
