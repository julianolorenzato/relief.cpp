/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

namespace op::bboxproj {

namespace {

/// @return The world corner of `box` selected by `bits`, where bit `i` set
///         picks `box.max[i]`, cleared picks `box.min[i]`.
Eigen::Vector3d corner(const BBox& box, int bits) {
    Eigen::Vector3d c;
    for (int i = 0; i < 3; i++) c[i] = (bits & (1 << i)) ? box.max[i] : box.min[i];
    return c;
}

}  // namespace

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    bool any = false;
    BBox box;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        box.min = box.min.cwiseMin(v.pos);
        box.max = box.max.cwiseMax(v.pos);
        any = true;
    }
    if (!any) return;

    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();

    // 8 shared corner vertices, indexed by a 3-bit mask (bit i set -> max[i],
    // cleared -> min[i]).
    for (int bits = 0; bits < 8; bits++) {
        mesh::Vertex vertex;
        vertex.pos = corner(box, bits);
        mesh.vertices.push_back(vertex);
    }

    static const Eigen::Vector2d kQuadUV[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    // Each of the 6 faces gets its own 4 wedges (unwelded across faces) with
    // a synthetic unit-square UV, matching the box-face convention used by
    // op::bvol::BoundingVolumeOp for faces with no source geometry to
    // project.
    for (int axis = 0; axis < 3; axis++) {
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        for (int sign = -1; sign <= 1; sign += 2) {
            int axisBit = sign > 0 ? (1 << axis) : 0;

            int base = (int)mesh.wedges.size();
            const int uvBits[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (int i = 0; i < 4; i++) {
                int bits = axisBit | (uvBits[i][0] << u) | (uvBits[i][1] << v);

                mesh::Wedge wedge;
                wedge.vertex = bits;
                wedge.uv = kQuadUV[i];
                mesh.wedges.push_back(wedge);
            }

            mesh::Face f0, f1;
            if (sign > 0) {
                f0.w[0] = base;
                f0.w[1] = base + 1;
                f0.w[2] = base + 2;
                f1.w[0] = base;
                f1.w[1] = base + 2;
                f1.w[2] = base + 3;
            } else {
                f0.w[0] = base;
                f0.w[1] = base + 2;
                f0.w[2] = base + 1;
                f1.w[0] = base;
                f1.w[1] = base + 3;
                f1.w[2] = base + 2;
            }
            mesh.faces.push_back(f0);
            mesh.faces.push_back(f1);
        }
    }

    mesh.logSummary();
}

}  // namespace op::bboxproj
