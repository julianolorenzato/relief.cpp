/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

namespace op::bboxproj {

namespace {

/// Axis index (0=X, 1=Y, 2=Z) of BBox::faces[i].
constexpr int kFaceAxis[6] = {0, 0, 1, 1, 2, 2};

/// Outward sign (-1 or +1 along kFaceAxis[i]) of BBox::faces[i].
constexpr double kFaceSign[6] = {-1, +1, -1, +1, -1, +1};

}  // namespace

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box;
    bool any = false;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        box.min = box.min.cwiseMin(v.pos);
        box.max = box.max.cwiseMax(v.pos);
        any = true;
    }
    if (!any) return;

    Eigen::Vector3d center = 0.5 * (box.min + box.max);
    Eigen::Vector3d halfExtents = 0.5 * (box.max - box.min);

    // Each face gets a flat quad spanning its full rectangle, with a
    // synthetic unit-square UV (no source geometry is projected onto it).
    for (int face = 0; face < 6; face++) {
        int axis = kFaceAxis[face];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        double hu = halfExtents[u];
        double hv = halfExtents[v];

        BBoxFace& patch = box.faces[face];
        patch.vertices = {Eigen::Vector2d(-hu, -hv), Eigen::Vector2d(hu, -hv),
                           Eigen::Vector2d(hu, hv), Eigen::Vector2d(-hu, hv)};
        patch.uvs = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0), Eigen::Vector2d(1, 1),
                     Eigen::Vector2d(0, 1)};
        if (kFaceSign[face] > 0) {
            patch.triangles.push_back({{0, 1, 2}});
            patch.triangles.push_back({{0, 2, 3}});
        } else {
            patch.triangles.push_back({{0, 2, 1}});
            patch.triangles.push_back({{0, 3, 2}});
        }
    }

    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();

    // Each face's local 2D vertices/UVs/triangles are flattened back into
    // 3D independently (no welding across faces), so the result is a
    // disconnected triangle soup at box edges/corners.
    for (int face = 0; face < 6; face++) {
        int axis = kFaceAxis[face];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d faceOrigin =
            center + kFaceSign[face] * halfExtents[axis] * Eigen::Vector3d::Unit(axis);
        Eigen::Vector3d axisU = Eigen::Vector3d::Unit(u);
        Eigen::Vector3d axisV = Eigen::Vector3d::Unit(v);

        const BBoxFace& patch = box.faces[face];
        std::vector<int> wedgeOf(patch.vertices.size());
        for (size_t i = 0; i < patch.vertices.size(); i++) {
            const Eigen::Vector2d& local = patch.vertices[i];
            Eigen::Vector3d pos = faceOrigin + local.x() * axisU + local.y() * axisV;

            mesh::Vertex vertex;
            vertex.pos = pos;
            int vertexIdx = (int)mesh.vertices.size();
            mesh.vertices.push_back(vertex);

            mesh::Wedge wedge;
            wedge.vertex = vertexIdx;
            wedge.uv = patch.uvs[i];
            wedgeOf[i] = (int)mesh.wedges.size();
            mesh.wedges.push_back(wedge);
        }
        for (const BBoxTriangle& tri : patch.triangles) {
            mesh::Face f;
            f.w[0] = wedgeOf[tri.v[0]];
            f.w[1] = wedgeOf[tri.v[1]];
            f.w[2] = wedgeOf[tri.v[2]];
            mesh.faces.push_back(f);
        }
    }

    mesh.logSummary();
}

}  // namespace op::bboxproj
