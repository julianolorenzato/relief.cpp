/**
 * @file qem.h
 * @brief Mesh simplification via Quadric Error Metrics (QEM), with optional
 *        boundary/seam and envelope constraints.
 */
#pragma once
#include <vector>
#include <set>
#include <map>
#include <queue>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <Eigen/Dense>

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

/// A single mesh vertex: position, accumulated quadric, UV, and (optionally) envelope planes.
struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    Eigen::Vector2d uv   = Eigen::Vector2d::Zero();
    bool            removed = false;

    /// Outward-oriented planes (n.x,n.y,n.z,d) of original faces already
    /// absorbed by this vertex over the course of its collapses (see
    /// QEMSimplifier::envelopeConstraint). Empty when the envelope
    /// constraint is disabled.
    std::vector<Eigen::Vector4d> envelope;
};

/// A single triangular mesh face.
struct Face {
    int  v[3];             ///< Vertex indices.
    bool removed = false;
};

/// A candidate edge collapse: which vertices merge, where, at what cost.
struct EdgeCollapse {
    int             v1, v2;
    Eigen::Vector3d target   = Eigen::Vector3d::Zero();
    Eigen::Vector2d targetUV = Eigen::Vector2d::Zero();
    double          cost     = 0.0;

    /// When v1/v2 is a seam edge with a synchronized twin (SyncSeamTwins),
    /// tv1/tv2 is the mirrored edge on the other side of the seam, collapsed
    /// together to the same 'target' position (but with its own UV, in targetUV2).
    int             tv1 = -1, tv2 = -1;
    Eigen::Vector2d targetUV2 = Eigen::Vector2d::Zero();

    /// Orders collapses cheapest-first when used with std::greater in a priority_queue.
    bool operator>(const EdgeCollapse& o) const { return cost > o.cost; }
};

/// Controls how mesh boundaries and UV seams are treated during simplification.
enum class BoundaryMode {
    None,              ///< No boundary/seam constraint at all.
    Constraint,        ///< Soft penalty (perpendicular-plane quadric).
    LockSeamVertices,  ///< Hard lock: never collapses an edge touching a boundary vertex.
    SyncSeamTwins      ///< Collapses seam edges in sync with their mirrored pair on the other side.
};

/**
 * @brief Simplifies a triangle mesh via iterative edge collapse driven by
 *        Quadric Error Metrics, with optional boundary/seam handling and
 *        envelope (non-penetration) constraints.
 */
class QEMSimplifier {
public:
    std::vector<Vertex> vertices;
    std::vector<Face>   faces;

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

    /// @brief Loads a mesh from a Wavefront OBJ file.
    /// @param path Path to the .obj file.
    /// @return true on success.
    bool loadOBJ(const std::string& path);
    /// @brief Saves the mesh to a Wavefront OBJ file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveOBJ(const std::string& path) const;
    /// @brief Loads a mesh (and its embedded textures) from a glTF/GLB file.
    /// @param path Path to the .gltf/.glb file.
    /// @return true on success.
    bool loadGLTF(const std::string& path);
    /// @brief Saves the mesh to a glTF/GLB file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveGLTF(const std::string& path) const;

    /// Textures extracted from the source GLTF (RGBA, row-major).
    std::vector<uint8_t> textureData;
    int textureWidth  = 0;
    int textureHeight = 0;

    std::vector<uint8_t> normalTextureData;
    int normalTextureWidth  = 0;
    int normalTextureHeight = 0;

    /// @brief Runs greedy edge-collapse simplification until the target face
    ///        count is reached or no further collapse is cheap enough.
    /// @param targetFaces Desired number of faces to stop at.
    /// @param threshold Maximum acceptable collapse cost; collapses above it are skipped.
    void simplify(int targetFaces, double threshold = 0.0);

    /// @return Number of non-removed faces.
    int faceCount() const;
    /// @return Number of non-removed vertices.
    int vertexCount() const;

    /// Edge classification: boundary = referenced by exactly 1 face (same
    /// criterion used by addBoundaryConstraints). Used both internally and by
    /// the viewport so both see the same thing.
    struct EdgeInfo {
        int  v1, v2;
        bool boundary;
        int  faceId; ///< Reference face (always valid; unique when boundary == true).
    };
    /// @brief Classifies every edge of the current mesh as boundary or interior.
    /// @return One EdgeInfo per unique edge.
    std::vector<EdgeInfo> classifyEdges() const;

private:
    std::map<std::pair<int,int>, EdgeCollapse> edgeMap;

    /// Computes the initial per-vertex quadric from adjacent face planes.
    void computeQ();
    /// Computes vertices[i].envelope from the current faces (once, before the
    /// collapse loop). Only called when envelopeConstraint == true.
    void computeEnvelope();
    /// Builds the collapse candidate between v1, v2, the midpoint, and (if
    /// useOptimalCandidate) the quadric's unconstrained optimum. When
    /// envelopeConstraint == true, discards candidates that violate the
    /// accumulated planes of v1/v2, returning false if none remain viable
    /// (the edge can't collapse at this step).
    bool computeCollapse(int v1, int v2, EdgeCollapse& out) const;
    /// Synchronized version: combines the quadrics of (v1,v2) and of the
    /// mirrored pair (tv1,tv2) to pick a single shared target position.
    bool computeCollapse(int v1, int v2, int tv1, int tv2, EdgeCollapse& out) const;
    /// Applies a chosen collapse: merges vertices, removes the collapsed
    /// face(s), and updates adjacency/queue bookkeeping.
    void applyCollapse(const EdgeCollapse& ec);
    /// Merges `remove` into `keep` at the given position/UV, updating faces and adjacency.
    void mergeVertexPair(int keep, int remove, const Eigen::Vector3d& pos, const Eigen::Vector2d& uv);
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

    /// Used when boundaryMode == LockSeamVertices or SyncSeamTwins: no
    /// boundary-touching edge may be a collapse candidate.
    bool lockSeamEdges = false;
    std::vector<bool> boundaryVertex;
    /// Marks boundaryVertex[i] for every vertex touching a boundary edge.
    void markBoundaryVertices();
    /// @return true if the edge (a,b) is locked from collapsing under lockSeamEdges.
    bool edgeLocked(int a, int b) const {
        return lockSeamEdges && (boundaryVertex[a] || boundaryVertex[b]);
    }

    /// Used when boundaryMode == SyncSeamTwins.
    /// seamTwin[i] = vertex on the other side of the seam, same 3D position,
    /// different UV; -1 if there's no unique pair (open boundary or a
    /// junction of 3+ seams), in which case the vertex stays locked as before.
    bool syncSeamTwins = false;
    std::vector<int> seamTwin;
    /// Builds the seamTwin[] mapping by matching boundary vertices that share a 3D position.
    void buildSeamTwins();
    /// Builds the collapse candidate for edge (p,q), deciding whether it
    /// should be locked, synchronized with its twin, or handled normally.
    /// @return false if the edge can't be collapsed (locked).
    bool buildCandidate(int p, int q, EdgeCollapse& out) const;
};
