/**
 * @file simplification.h
 * @brief Mesh simplification via Quadric Error Metrics (QEM), with optional
 *        boundary/seam and envelope constraints.
 */
#pragma once
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <Eigen/Dense>
#include "relief/mesh.h"

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

    /// Per-incident-face UV pairing for this edge: (uv slot of v1, uv slot of
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
    LockSeamVertices,  ///< Hard lock: never collapses an edge touching a boundary or UV-seam vertex.
};

/**
 * @brief Simplifies a triangle mesh via iterative edge collapse driven by
 *        Quadric Error Metrics, with optional boundary/seam handling and
 *        envelope (non-penetration) constraints.
 */
class Simplifier {
public:
    /// @param mesh Mesh to simplify in place. Must outlive the Simplifier.
    explicit Simplifier(Mesh& mesh) : mesh_(mesh) {}

    BoundaryMode boundaryMode = BoundaryMode::Constraint;

    /// When true, no collapse may produce a vertex that violates the
    /// accumulated envelope planes of v1/v2: guarantees the simplified mesh
    /// stays on or outside the original surface.
    bool envelopeConstraint = false;

    /// When true, adds the quadric's unconstrained optimum (solveQuadric) as
    /// an extra position candidate, alongside v1, v2, and the midpoint. With
    /// envelopeConstraint also enabled, the optimum is only accepted if it
    /// respects the accumulated planes; otherwise it falls back to the other
    /// 3 candidates as before.
    bool useOptimalCandidate = false;

    /// @brief Runs greedy edge-collapse simplification until the target face
    ///        count is reached or no further collapse is cheap enough.
    /// @param targetFaces Desired number of faces to stop at.
    /// @param threshold Maximum acceptable collapse cost; collapses above it are skipped.
    void run(int targetFaces);

private:
    Mesh& mesh_;

    std::map<std::pair<int,int>, EdgeCollapse> edgeMap;

    /// Computes the initial per-vertex quadric from adjacent face planes.
    void computeQ();
    /// Computes mesh_.vertices[i].envelope from the current faces (once,
    /// before the collapse loop). Only called when envelopeConstraint == true.
    void computeEnvelope();
    /// Builds the collapse candidate between v1, v2, the midpoint, and (if
    /// useOptimalCandidate) the quadric's unconstrained optimum. When
    /// envelopeConstraint == true, discards candidates that violate the
    /// accumulated planes of v1/v2, returning false if none remain viable
    /// (the edge can't collapse at this step).
    bool computeCollapse(int v1, int v2, EdgeCollapse& out) const;
    /// @return The distinct (uv slot of v1, uv slot of v2) pairs actually
    ///         used together by some face incident to edge (v1,v2).
    std::vector<std::pair<int,int>> edgeUVPairs(int v1, int v2) const;
    /// Merges `remove` into `keep` at the given position, updating faces
    /// (remapping their per-corner UV slot into `keep`'s UV list) and adjacency.
    void mergeVertexPair(int keep, int remove, const Eigen::Vector3d& pos,
                          const std::vector<std::tuple<int,int,Eigen::Vector2d>>& uvTargets);
    /// Rebuilds the priority queue of candidate collapses from current adjacency.
    void rebuildQueue(std::priority_queue<EdgeCollapse,
                                         std::vector<EdgeCollapse>,
                                         std::greater<EdgeCollapse>>& pq);
    /// Orders (a, b) into a canonical (a < b) pair; returns the canonicalized first index.
    int canonicalize(int& a, int& b) const;
    std::vector<std::set<int>> adjacency;
    /// Rebuilds the vertex adjacency sets from the current face list.
    void buildAdjacency();
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
    /// @return true if the edge (a,b) is locked from collapsing under lockSeamEdges.
    bool edgeLocked(int a, int b) const {
        return lockSeamEdges && (boundaryVertex[a] || boundaryVertex[b]);
    }

    /// Builds the collapse candidate for edge (p,q), deciding whether it
    /// should be locked or handled normally.
    /// @return false if the edge can't be collapsed (locked).
    bool buildCandidate(int p, int q, EdgeCollapse& out) const;
};
