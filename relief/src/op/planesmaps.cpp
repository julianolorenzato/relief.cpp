/**
 * @file planesmaps.cpp
 */
#include "relief/op/planesmaps.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "relief/mesh.h"
#include "relief/op/bboxproj.h"

namespace op::planesmaps {

namespace {

double cross(const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
    return a.x() * b.y() - a.y() * b.x();
}

/**
 * @brief Rasterizes a quad's faces into a texture, writing each face's interpolated
 *        UV into two channels.
 *
 * Texel (x, y) is sampled at its center against the quad's local coordinates
 * normalized to [0, 1] over its rectangle. Overlapping faces (which clipping leaves
 * only within numerical noise) overwrite each other.
 *
 * @param quad    Quad whose faces are rasterized.
 * @param hu      Half extent of the quad rectangle along local x.
 * @param hv      Half extent of the quad rectangle along local y.
 * @param channel First of the two RGBA channels to write (0 for RG, 2 for BA).
 * @param map     Square RGBA16 texture, updated in place.
 */
void rasterizeUV(const bboxproj::BBoxQuad& quad, double hu, double hv, int channel,
                 mesh::Texture16& map) {
    const int size = map.width;
    for (const mesh::Face& f : quad.faces) {
        std::array<Eigen::Vector2d, 3> t;
        std::array<Eigen::Vector2d, 3> uv;
        for (int i = 0; i < 3; i++) {
            const mesh::Wedge& wedge = quad.wedges[f.w[i]];
            const Eigen::Vector3d& pos = quad.vertices[wedge.vertex].pos;
            t[i] = Eigen::Vector2d((pos.x() + hu) / (2 * hu) * size, (pos.y() + hv) / (2 * hv) * size);
            uv[i] = wedge.uv;
        }

        const double area = cross(t[1] - t[0], t[2] - t[0]);
        if (std::abs(area) <= 1e-12) continue;

        const int minX = std::max(0, (int)std::floor(std::min({t[0].x(), t[1].x(), t[2].x()})));
        const int maxX = std::min(size - 1, (int)std::ceil(std::max({t[0].x(), t[1].x(), t[2].x()})));
        const int minY = std::max(0, (int)std::floor(std::min({t[0].y(), t[1].y(), t[2].y()})));
        const int maxY = std::min(size - 1, (int)std::ceil(std::max({t[0].y(), t[1].y(), t[2].y()})));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                const Eigen::Vector2d c(x + 0.5, y + 0.5);
                const double l0 = cross(t[1] - c, t[2] - c) / area;
                const double l1 = cross(t[2] - c, t[0] - c) / area;
                const double l2 = cross(t[0] - c, t[1] - c) / area;
                if (l0 < -1e-4 || l1 < -1e-4 || l2 < -1e-4) continue;

                const Eigen::Vector2d value = l0 * uv[0] + l1 * uv[1] + l2 * uv[2];
                uint16_t* texel = &map.data[((size_t)y * size + x) * 4 + channel];
                for (int k = 0; k < 2; k++) {
                    texel[k] = (uint16_t)std::lround(std::clamp(value[k], 0.0, 1.0) * 65535.0);
                }
            }
        }
    }
}

}  // namespace

void PlanesMapsOp::apply(mesh::Mesh& mesh) const {
    bboxproj::BBox box(mesh, resolution_);
    bboxproj::projectToQuads(mesh, box);

    std::array<mesh::Texture16, 3> maps;
    for (int axis = 0; axis < 3; axis++) {
        mesh::Texture16& map = maps[axis];
        map.width = mapSize_;
        map.height = mapSize_;
        map.data.assign((size_t)mapSize_ * mapSize_ * 4, 0);

        const double hu = box.halfExtents[(axis + 1) % 3];
        const double hv = box.halfExtents[(axis + 2) % 3];
        // A flat box has no area to project onto on this axis.
        if (hu <= 0.0 || hv <= 0.0) continue;

        // QUADS is [-X, +X, -Y, +Y, -Z, +Z]: the +axis quad goes in RG, the -axis quad in BA.
        rasterizeUV(box.quads[2 * axis + 1], hu, hv, 0, map);
        rasterizeUV(box.quads[2 * axis], hu, hv, 2, map);
    }

    mesh.setXPlanesMap(std::move(maps[0]));
    mesh.setYPlanesMap(std::move(maps[1]));
    mesh.setZPlanesMap(std::move(maps[2]));
}

}  // namespace op::planesmaps
