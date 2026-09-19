/**
 * @file edgesel.h
 * @brief Interactive brush-based edge selection: raycasting, face
 *        adjacency, and normal-gated flood fill over a Mesh's edges. Used
 *        to let a user "paint" feature edges to lock during simplification.
 */
#pragma once
#include <array>
#include <set>
#include <utility>
#include <vector>
#include <Eigen/Dense>
#include "relief/mesh.h"

namespace mesh::edgesel {

/// How the flood fill decides whether to cross from one face to its
/// neighbor while propagating a brush touch.
enum class PropagationMode {
    Chained,        ///< Compare each face's normal to the face it was reached from.
    AnchoredToSeed, ///< Always compare to the original seed face's normal.
};

/// Face-adjacency across shared edges. `neighborFace[i][k]` is the index of
/// the face sharing face `i`'s edge `k` (edge (corner k, corner k+1)), or -1
/// if that edge is a mesh boundary (exactly 1 incident face).
struct FaceAdjacency {
    std::vector<std::array<int, 3>> neighborFace;

    /// @return Face adjacency derived from mesh.buildEdgeToFaces().
    static FaceAdjacency build(const Mesh& mesh);
};

/// @return One outward unit normal per face (zero vector for degenerate/zero-area faces).
std::vector<Eigen::Vector3d> computeFaceNormals(const Mesh& mesh);

/// Result of a CPU ray-mesh intersection.
struct RayHit {
    bool found = false;
    int faceIdx = -1;
    double t = 0.0;
    Eigen::Vector3d point = Eigen::Vector3d::Zero();
};

/// @brief Casts a ray against every non-removed face (Möller–Trumbore) and
///        returns the closest hit.
/// @param origin Ray origin, in the same space as mesh vertex positions.
/// @param dir Ray direction (need not be normalized).
RayHit raycastMesh(const Mesh& mesh, const Eigen::Vector3d& origin, const Eigen::Vector3d& dir);

/// @return true iff the 3D segment [mesh.vertices[v0].pos, mesh.vertices[v1].pos]
///         passes within `radius` of `center` (point-to-segment distance).
bool touchesEdge(const Mesh& mesh, int v0, int v1, const Eigen::Vector3d& center, double radius);

/// Accumulating, persistent edge-selection state for one brush "owner"
/// (e.g. one viewport). A brush stroke is a sequence of applyBrush() calls
/// that union (or, with erase=true, subtract) touched edges into the set.
class BrushSelection {
public:
    /// @brief Applies one brush touch: flood-fills outward from `seedFace`
    ///        across face adjacency, adding (or removing, if `erase`) every
    ///        edge that comes within `radius` of `hitPoint` on a visited
    ///        face, as long as propagation across each face boundary passes
    ///        the `angleThresholdRad` normal-similarity test under `mode`.
    /// @param adj Face adjacency for `mesh` (see FaceAdjacency::build).
    /// @param faceNormals Per-face normals for `mesh` (see computeFaceNormals).
    /// @param seedFace Index of the face the brush ray hit.
    /// @param hitPoint World/mesh-space brush center.
    /// @param radius Mesh-space brush radius.
    /// @param angleThresholdRad Max allowed normal angle (radians) to cross a face boundary.
    /// @param erase If true, touched edges are removed from the selection instead of added.
    void applyBrush(const Mesh& mesh, const FaceAdjacency& adj,
                     const std::vector<Eigen::Vector3d>& faceNormals,
                     int seedFace, const Eigen::Vector3d& hitPoint,
                     double radius, double angleThresholdRad,
                     PropagationMode mode, bool erase = false);

    void clear() { edges_.clear(); }
    const std::set<Edge>& edges() const { return edges_; }
    size_t size() const { return edges_.size(); }

private:
    std::set<Edge> edges_;
};

} // namespace mesh::edgesel
