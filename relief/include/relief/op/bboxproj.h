/**
 * @file bboxproj.h
 * @brief op::Op that replaces a mesh's geometry with a plain axis-aligned
 *        bounding box.
 */
#pragma once

#include <array>
#include <limits>
#include <utility>
#include <vector>

#include "relief/mesh.h"
#include "relief/op.h"
namespace op::bboxproj {

struct Quad {
    const uint8_t axis;
    const double sign;

    std::vector<mesh::Edge> edges;

    explicit Quad(uint8_t axis, double sign) : axis(axis), sign(sign) {}
};

std::array<Quad, 6> quads = {Quad(0, -1), Quad(0, +1), Quad(1, -1),
                             Quad(1, +1), Quad(2, -1), Quad(2, +1)};

struct BBoxQuadEdge {
    std::pair<int, int> w;
};

/// One of a BBox's 6 quads: an independently triangulated planar patch,
/// with the same vertex/wedge/face shape as mesh::Mesh -- i.e. a quad is a
/// small, self-contained mesh of its own, unwelded from its neighbors.
/// `faces[i].w` indexes `wedges`, and `wedges[j].vertex` indexes
/// `vertices`, exactly as in mesh::Mesh. `vertices[k].pos` stores local 2D
/// quad-plane coordinates in x/y, since every vertex on a given quad lies in
/// that quad's plane; the enclosing BBox's `min`/`max`, plus which of the 6
/// quads this is, determine how these local coordinates map into 3D. z holds
/// the depth of the projected point, i.e. its distance from the quad plane
/// (0 on the plane, growing towards the box's far side); it is only used for
/// occlusion resolution and ignored when the quad is flattened back to 3D.
/// `Vertex::Q` and `Vertex::removed` are unused here.
///
/// `depth` and `owner` form a `resolution` x `resolution` buffer over the
/// quad's rectangle (row-major, pixel (x, y) at `y * resolution + x`) used by
/// BBoxProjectionOp::apply to resolve occlusion.
struct BBoxQuad {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;

    /// Side length, in pixels, of the square depth buffer.
    int resolution;
    /// Depth of the nearest projected surface at each pixel (+infinity if
    /// nothing covers it).
    std::vector<double> depth;
    /// Index into `faces` of the triangle owning each pixel's depth (-1 if
    /// nothing covers it).
    std::vector<int> owner;

    BBoxQuad(int resolution = 1)
        : resolution(resolution),
          depth(resolution * resolution, std::numeric_limits<double>::infinity()),
          owner(resolution * resolution, -1) {}
};

/// @brief Axis-aligned bounding box: min/max corners, plus each of its 6
///        quads' own vertex/wedge/face data.
struct BBox {
    const Eigen::Vector3d min;
    const Eigen::Vector3d max;
    const Eigen::Vector3d center;
    const Eigen::Vector3d halfExtents;

    /// The box's 6 quads, in order [-X, +X, -Y, +Y, -Z, +Z].
    std::array<BBoxQuad, 6> quads;

    /**
     * @brief Builds the box as the bounds of `mesh`'s non-removed vertices.
     * @param mesh       Mesh to bound.
     * @param resolution Side length, in pixels, of each quad's depth buffer.
     */
    BBox(const mesh::Mesh& mesh, int resolution) : BBox(computeBounds(mesh), resolution) {}

    /**
     * @brief Replaces a mesh's geometry with the box's quads.
     *
     * Each quad's local 2D vertices/UVs/triangles are flattened back into 3D
     * independently (no welding across quads), so the result is a
     * disconnected triangle soup at box edges/corners. The mesh's textures
     * are kept.
     *
     * @param mesh Mesh whose geometry is replaced.
     */
    void exportTo(mesh::Mesh& mesh) const;

   private:
    using Bounds = std::pair<Eigen::Vector3d, Eigen::Vector3d>;

    BBox(const Bounds& bounds, int resolution)
        : min(bounds.first),
          max(bounds.second),
          center(0.5 * (min + max)),
          halfExtents(0.5 * (max - min)) {
        quads.fill(BBoxQuad(resolution));
    }

    static Bounds computeBounds(const mesh::Mesh& mesh) {
        Eigen::Vector3d min = Eigen::Vector3d::Constant(std::numeric_limits<double>::max());
        Eigen::Vector3d max = Eigen::Vector3d::Constant(std::numeric_limits<double>::lowest());
        for (const auto& v : mesh.vertices) {
            if (v.removed) continue;
            min = min.cwiseMin(v.pos);
            max = max.cwiseMax(v.pos);
        }
        return {min, max};
    }
};

/**
 * @brief Replaces a mesh's vertices/wedges/faces with a plain axis-aligned
 *        bounding box, re-triangulated per quad by projecting every
 *        outward-facing mesh face onto whichever box quad(s) it faces
 *        (unwelded across quads; original UVs are kept as-is). Occluded
 *        triangles are dropped using a per-quad depth buffer. Quads with no
 *        mesh geometry ever facing them (e.g. a flat/open source mesh) fall
 *        back to a flat rectangle spanning the full quad, with a synthetic
 *        unit-square UV, so the box stays closed everywhere.
 */
class BBoxProjectionOp : public op::Op {
   public:
    /// Default side length, in pixels, of each quad's depth buffer.
    static constexpr int DEFAULT_RESOLUTION = 4096;

    /**
     * @param resolution Side length, in pixels, of each quad's depth buffer.
     *                   Higher values resolve occlusion more precisely.
     */
    explicit BBoxProjectionOp(int resolution = DEFAULT_RESOLUTION) : resolution_(resolution) {}

    void apply(mesh::Mesh& mesh) const override;

   private:
    int resolution_;

    void handleQuad(const Quad quad, BBox& box, mesh::Mesh& mesh);
};
}  // namespace op::bboxproj
