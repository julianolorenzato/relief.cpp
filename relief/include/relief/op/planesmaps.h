/**
 * @file planesmaps.h
 * @brief op::Op that bakes the X/Y/Z planes maps: per-texel color/normal-map UVs
 *        of the surface visible from each face of the mesh's bounding box.
 */
#pragma once

#include "relief/mesh.h"
#include "relief/op.h"
#include "relief/op/bboxproj.h"

namespace op::planesmaps {

/**
 * @brief Bakes mesh::Mesh::xPlanesMap, yPlanesMap and zPlanesMap.
 *
 * The mesh is projected onto its axis-aligned bounding box exactly as
 * op::bboxproj::BBoxProjectionOp does (occluded parts clipped away), but the
 * geometry is left untouched. For each axis, a square RGBA16 map spans the
 * box's two quads perpendicular to it: texel (x, y) is sampled at its center
 * against the quad's local (u, v) = ((axis + 1) % 3, (axis + 2) % 3) frame,
 * normalized to [0, 1] over the box extents. RG store the color/normal-map UV
 * of the surface seen on the +axis quad, BA those of the -axis quad, as
 * round(uv * 65535). Texels no projected face covers store UV (0, 0).
 */
class PlanesMapsOp : public op::Op {
   public:
    /// Default side length, in texels, of each planes map.
    static constexpr int DEFAULT_MAP_SIZE = 1024;

    /**
     * @param mapSize    Side length, in texels, of each planes map.
     * @param resolution Side length, in pixels, of each quad's depth buffer used to
     *                   resolve occlusion (see BBoxProjectionOp).
     */
    explicit PlanesMapsOp(int mapSize = DEFAULT_MAP_SIZE,
                          int resolution = bboxproj::BBoxProjectionOp::DEFAULT_RESOLUTION)
        : mapSize_(mapSize), resolution_(resolution) {}

    void apply(mesh::Mesh& mesh) const override;

   private:
    int mapSize_;
    int resolution_;
};
}  // namespace op::planesmaps
