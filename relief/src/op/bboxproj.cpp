/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

namespace op::bboxproj {

/// Axis index (0=X, 1=Y, 2=Z) of BBox::faces[i].
constexpr int FACE_AXIS[6] = {0, 0, 1, 1, 2, 2};

/// Outward sign (-1 or +1 along FACE_AXIS[i]) of BBox::faces[i].
constexpr double FACE_SIGN[6] = {-1, +1, -1, +1, -1, +1};

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box;
    for (const auto& v : mesh.vertices) {
        if (v.removed) continue;
        box.min = box.min.cwiseMin(v.pos);
        box.max = box.max.cwiseMax(v.pos);
    }

    Eigen::Vector3d center = 0.5 * (box.min + box.max);
    Eigen::Vector3d halfExtents = 0.5 * (box.max - box.min);

    // Project every outward-facing mesh face onto each box face it faces (a
    // face can face more than one box face, e.g. towards a corner, in which
    // case it's projected onto each). No occlusion resolution: overlapping
    // projected triangles are all kept, in original mesh order.
    for (const auto& f : mesh.faces) {
        if (f.removed) continue;
        Eigen::Vector3d p[3] = {mesh.vertices[mesh.wedges[f.w[0]].vertex].pos,
                                mesh.vertices[mesh.wedges[f.w[1]].vertex].pos,
                                mesh.vertices[mesh.wedges[f.w[2]].vertex].pos};
        Eigen::Vector2d texUV[3] = {mesh.wedges[f.w[0]].uv, mesh.wedges[f.w[1]].uv,
                                    mesh.wedges[f.w[2]].uv};
        Eigen::Vector3d normal = mesh.faceNormal(f);
        if (normal.isZero()) continue;

        for (int face = 0; face < 6; face++) {
            int axis = FACE_AXIS[face];
            Eigen::Vector3d outward = FACE_SIGN[face] * Eigen::Vector3d::Unit(axis);
            if (normal.dot(outward) <= 0.0) continue;

            int u = (axis + 1) % 3;
            int v = (axis + 2) % 3;
            BBoxFace& patch = box.faces[face];
            mesh::Face triangle;
            for (int i = 0; i < 3; i++) {
                Eigen::Vector3d d = p[i] - center;

                mesh::Vertex vertex;
                vertex.pos = Eigen::Vector3d(d.dot(Eigen::Vector3d::Unit(u)),
                                             d.dot(Eigen::Vector3d::Unit(v)), 0.0);
                int vertexIdx = (int)patch.vertices.size();
                patch.vertices.push_back(vertex);

                mesh::Wedge wedge;
                wedge.vertex = vertexIdx;
                wedge.uv = texUV[i];
                triangle.w[i] = (int)patch.wedges.size();
                patch.wedges.push_back(wedge);
            }
            patch.faces.push_back(triangle);
        }
    }

    // Faces with no mesh geometry ever facing them (e.g. a flat/open source
    // mesh) fall back to a flat quad spanning the full face rectangle, with
    // a synthetic unit-square UV, so the box stays closed.
    for (int face = 0; face < 6; face++) {
        BBoxFace& patch = box.faces[face];
        if (!patch.faces.empty()) continue;

        int axis = FACE_AXIS[face];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        double hu = halfExtents[u];
        double hv = halfExtents[v];

        std::array<Eigen::Vector2d, 4> corners = {Eigen::Vector2d(-hu, -hv),
                                                  Eigen::Vector2d(hu, -hv), Eigen::Vector2d(hu, hv),
                                                  Eigen::Vector2d(-hu, hv)};
        std::array<Eigen::Vector2d, 4> cornerUVs = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0),
                                                    Eigen::Vector2d(1, 1), Eigen::Vector2d(0, 1)};
        for (int i = 0; i < 4; i++) {
            mesh::Vertex vertex;
            vertex.pos = Eigen::Vector3d(corners[i].x(), corners[i].y(), 0.0);
            patch.vertices.push_back(vertex);

            mesh::Wedge wedge;
            wedge.vertex = i;
            wedge.uv = cornerUVs[i];
            patch.wedges.push_back(wedge);
        }
        if (FACE_SIGN[face] > 0) {
            patch.faces.push_back({{0, 1, 2}});
            patch.faces.push_back({{0, 2, 3}});
        } else {
            patch.faces.push_back({{0, 2, 1}});
            patch.faces.push_back({{0, 3, 2}});
        }
    }

    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();

    // Each face's local 2D vertices/UVs/triangles are flattened back into
    // 3D independently (no welding across faces), so the result is a
    // disconnected triangle soup at box edges/corners.
    for (int face = 0; face < 6; face++) {
        int axis = FACE_AXIS[face];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d faceOrigin =
            center + FACE_SIGN[face] * halfExtents[axis] * Eigen::Vector3d::Unit(axis);
        Eigen::Vector3d axisU = Eigen::Vector3d::Unit(u);
        Eigen::Vector3d axisV = Eigen::Vector3d::Unit(v);

        const BBoxFace& patch = box.faces[face];
        int vertexBase = (int)mesh.vertices.size();
        for (const mesh::Vertex& local : patch.vertices) {
            mesh::Vertex vertex;
            vertex.pos = faceOrigin + local.pos.x() * axisU + local.pos.y() * axisV;
            mesh.vertices.push_back(vertex);
        }

        int wedgeBase = (int)mesh.wedges.size();
        for (const mesh::Wedge& localWedge : patch.wedges) {
            mesh::Wedge wedge;
            wedge.vertex = vertexBase + localWedge.vertex;
            wedge.uv = localWedge.uv;
            mesh.wedges.push_back(wedge);
        }

        for (const mesh::Face& tri : patch.faces) {
            mesh::Face f;
            f.w[0] = wedgeBase + tri.w[0];
            f.w[1] = wedgeBase + tri.w[1];
            f.w[2] = wedgeBase + tri.w[2];
            mesh.faces.push_back(f);
        }
    }

    mesh.computeIslands();

    mesh.logSummary();
}

}  // namespace op::bboxproj
