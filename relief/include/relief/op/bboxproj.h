/**
 * @file bboxproj.h
 * @brief op::Op that replaces a mesh's geometry with a plain axis-aligned
 *        bounding box.
 */
#pragma once

#include <array>
#include <limits>
#include <vector>

#include "relief/mesh.h"
#include "relief/op.h"
namespace op::bboxproj {

/// One of a BBox's 6 quads: an independently triangulated planar patch,
/// with the same vertex/wedge/face shape as mesh::Mesh -- i.e. a quad is a
/// small, self-contained mesh of its own, unwelded from its neighbors.
/// `faces[i].w` indexes `wedges`, and `wedges[j].vertex` indexes
/// `vertices`, exactly as in mesh::Mesh. `vertices[k].pos` stores local 2D
/// quad-plane coordinates in x/y (z is unused/zero), since every vertex on
/// a given quad lies in that quad's plane; the enclosing BBox's
/// `min`/`max`, plus which of the 6 quads this is, determine how these
/// local coordinates map into 3D. `Vertex::Q` and `Vertex::removed` are
/// unused here.
struct BBoxQuad {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;
};

/// @brief Axis-aligned bounding box: min/max corners, plus each of its 6
///        quads' own vertex/wedge/face data.
struct BBox {
    Eigen::Vector3d min = Eigen::Vector3d::Constant(std::numeric_limits<double>::max());
    Eigen::Vector3d max = Eigen::Vector3d::Constant(std::numeric_limits<double>::lowest());

    /// The box's 6 quads, in order [-X, +X, -Y, +Y, -Z, +Z].
    std::array<BBoxQuad, 6> quads;
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with a plain axis-aligned
 *        bounding box, re-triangulated per quad by projecting every
 *        outward-facing mesh face onto whichever box quad(s) it faces
 *        (unwelded across quads; original UVs are kept as-is, with no
 *        occlusion resolution -- overlapping projected triangles are all
 *        kept). Quads with no mesh geometry ever facing them (e.g. a
 *        flat/open source mesh) fall back to a flat rectangle spanning the
 *        full quad, with a synthetic unit-square UV, so the box stays
 *        closed everywhere.
 */
class BBoxProjectionOp : public op::Op {
   public:
    explicit BBoxProjectionOp() {}

    void apply(mesh::Mesh &mesh) const override;
};
}  // namespace op::bboxproj
