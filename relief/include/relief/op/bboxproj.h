/**
 * @file bboxproj.h
 * @brief op::Op that replaces a mesh's geometry with a plain axis-aligned
 *        bounding box.
 */
#pragma once

#include <array>
#include <vector>

#include "relief/mesh.h"
#include "relief/op.h"
namespace op::bboxproj {

/// One of a BBox's 6 faces: an independently triangulated planar patch,
/// with the same vertex/wedge/face shape as mesh::Mesh -- i.e. a face is a
/// small, self-contained mesh of its own, unwelded from its neighbors.
/// `faces[i].w` indexes `wedges`, and `wedges[j].vertex` indexes
/// `vertices`, exactly as in mesh::Mesh. `vertices[k].pos` stores local 2D
/// face-plane coordinates in x/y (z is unused/zero), since every vertex on
/// a given face lies in that face's plane; the enclosing BBox's
/// `min`/`max`, plus which of the 6 faces this is, determine how these
/// local coordinates map into 3D. `Vertex::Q` and `Vertex::removed` are
/// unused here.
struct BBoxFace {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;
};

/// @brief Axis-aligned bounding box: min/max corners, plus each of its 6
///        faces' own vertex/wedge/face data.
struct BBox {
    Eigen::Vector3d min = Eigen::Vector3d::Constant(1e18);
    Eigen::Vector3d max = Eigen::Vector3d::Constant(-1e18);

    /// The box's 6 faces, in order [-X, +X, -Y, +Y, -Z, +Z].
    std::array<BBoxFace, 6> faces;
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with a plain axis-aligned
 *        bounding box, re-triangulated per face by projecting every
 *        outward-facing mesh face onto whichever box face(s) it faces
 *        (unwelded across faces; original UVs are kept as-is, with no
 *        occlusion resolution -- overlapping projected triangles are all
 *        kept). Faces with no mesh geometry ever facing them (e.g. a
 *        flat/open source mesh) fall back to a flat quad spanning the full
 *        face rectangle, with a synthetic unit-square UV, so the box stays
 *        closed everywhere.
 */
class BBoxProjectionOp : public op::Op {
   public:
    explicit BBoxProjectionOp() {}

    void apply(mesh::Mesh &mesh) const override;
};
}  // namespace op::bboxproj
