/**
 * @file bvol.h
 * @brief op::Op that replaces a mesh's geometry with its bounding volume
 *        (axis-aligned or oriented bounding box).
 */
#pragma once
#include <array>
#include <vector>

#include "relief/op.h"

namespace op::bvol {

/// Selects which bounding volume BoundingVolumeOp replaces the mesh with.
enum class BoundingVolumeType {
    AABB,  ///< Axis-aligned bounding box.
    OBB,   ///< Oriented bounding box (PCA-fitted).
};

/// One triangle within a BoundingBoxFace, indexing into that same face's
/// `vertices` array.
struct BoundingBoxTriangle {
    int v[3];  ///< Indices into the enclosing BoundingBoxFace::vertices.
};

/// One of a BoundingBox's 6 faces: an independently triangulated planar
/// patch. Vertices are 2D since every vertex on a given face lies in that
/// face's plane; the enclosing BoundingBox's `axes` and `halfExtents`, plus
/// which of the 6 faces this is, determine how these local coordinates map
/// into 3D.
struct BoundingBoxFace {
    std::vector<Eigen::Vector2d> vertices;

    /// Original mesh texture UV of each entry in `vertices` (same index).
    std::vector<Eigen::Vector2d> uvs;

    std::vector<BoundingBoxTriangle> triangles;
};

/**
 * @brief A box-shaped outer volume: a center, an orthonormal axis frame,
 *        and half-extents along each axis define its overall shape, while
 *        each of its 6 faces carries its own independently triangulated
 *        vertex distribution (see BoundingBoxFace). Axis-aligned when
 *        `axes` is the identity frame, oriented otherwise.
 */
struct BoundingBox {
    /// Box center.
    Eigen::Vector3d center = Eigen::Vector3d::Zero();

    /// Orthonormal right-handed frame the box is aligned to.
    std::array<Eigen::Vector3d, 3> axes = {Eigen::Vector3d::UnitX(), Eigen::Vector3d::UnitY(),
                                           Eigen::Vector3d::UnitZ()};

    /// Half-extent along each axis.
    Eigen::Vector3d halfExtents = Eigen::Vector3d::Zero();

    /// The box's 6 faces, in order [-X, +X, -Y, +Y, -Z, +Z] relative to
    /// `axes`, each with its own vertex/triangle distribution.
    std::array<BoundingBoxFace, 6> faces;
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with its bounding volume:
 *        an axis-aligned or oriented (PCA-fitted) bounding box, re-
 *        triangulated per face by projecting each outward-facing,
 *        unoccluded mesh face onto whichever box face(s) it faces (since
 *        surviving faces are exact copies of original mesh faces, no
 *        clipping, their original UVs and texture data remain valid and
 *        are kept as-is).
 */
class BoundingVolumeOp : public op::Op {
   public:
    /// @param type Which bounding volume to compute.
    explicit BoundingVolumeOp(BoundingVolumeType type = BoundingVolumeType::AABB) : type(type) {}

    void apply(mesh::Mesh& mesh) const override;

   private:
    BoundingVolumeType type;

    void applyAABB(mesh::Mesh& mesh) const;
    void applyOBB(mesh::Mesh& mesh) const;

    /// @return The mesh's principal axes (right-handed, orthonormal),
    ///         computed via PCA over its non-removed vertices.
    static std::array<Eigen::Vector3d, 3> computeOBBAxes(const mesh::Mesh& mesh);

    /// Projects the mesh's non-removed vertices onto `axes` and derives
    /// `center`/`halfExtents` from their extent along each axis. With
    /// `axes` set to the world axes, this gives the AABB; with PCA axes
    /// (see computeOBBAxes), the OBB.
    static void computeBoxExtents(const mesh::Mesh& mesh, const std::array<Eigen::Vector3d, 3>& axes,
                                   Eigen::Vector3d& center, Eigen::Vector3d& halfExtents);

    /// Fills `box.faces` by projecting every outward-facing mesh face onto
    /// each of `box`'s 6 face planes it faces (a face can face more than
    /// one box face, e.g. towards a corner, in which case it's duplicated
    /// onto each). Assumes `box.center`/`axes`/`halfExtents` are already
    /// set; does not touch them. Faces with no surviving candidate (e.g. no
    /// mesh face ever faces that direction) are filled with a flat quad
    /// spanning the full face rectangle, with a synthetic unit-square UV,
    /// so the box stays closed everywhere.
    static void projectMeshOntoBoxFaces(const mesh::Mesh& mesh, BoundingBox& box);

    /// Flattens `box.faces`' local 2D vertices/UVs/triangles back into 3D,
    /// overwriting mesh.vertices/wedges/faces. Texture data is left as-is.
    /// Corners are welded by position into a shared Vertex, and further by
    /// UV into a shared Wedge, both across a single face and across faces
    /// (e.g. at shared box edges) -- so genuine UV discontinuities (most box
    /// edges, since adjacent faces are independently parameterized) surface
    /// as real seams per mesh::Wedge's contract, and buildEdgeToFaces can
    /// see true face adjacency instead of a fully disconnected triangle
    /// soup.
    static void flattenBoxFaces(const BoundingBox& box, mesh::Mesh& mesh);
};

}  // namespace op::bvol
