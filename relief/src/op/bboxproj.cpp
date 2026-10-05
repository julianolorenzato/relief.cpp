/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

#include "relief/mesh.h"

namespace op::bboxproj {

/// Axis index (0=X, 1=Y, 2=Z) of BBox::quads[i].
constexpr int QUAD_AXIS[6] = {0, 0, 1, 1, 2, 2};

/// Outward sign (-1 or +1 along QUAD_AXIS[i]) of BBox::quads[i].
constexpr double QUAD_SIGN[6] = {-1, +1, -1, +1, -1, +1};

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box(mesh);

    // Project every outward-facing mesh face onto each box quad it faces (a
    // face can face more than one box quad, e.g. towards a corner, in which
    // case it's projected onto each). No occlusion resolution: overlapping
    // projected triangles are all kept, in original mesh order.
    for (const auto& f : mesh.faces) {
        // Skip removed faces.
        if (f.removed) continue;
        Eigen::Vector3d normal = mesh.faceNormal(f);
        if (normal.isZero()) continue;

        for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
            // Skip backfaces for this axis.
            Eigen::Vector3d outward =
                QUAD_SIGN[quadIdx] * Eigen::Vector3d::Unit(QUAD_AXIS[quadIdx]);
            if (normal.dot(outward) <= 0.0) continue;

            int u = (QUAD_AXIS[quadIdx] + 1) % 3;
            int v = (QUAD_AXIS[quadIdx] + 2) % 3;
            BBoxQuad& quad = box.quads[quadIdx];
            mesh::Face triangle;
            for (int i = 0; i < 3; i++) {
                Eigen::Vector3d d = mesh.faceVertex(f, i).pos - box.center;

                mesh::Vertex vertex;
                vertex.pos = Eigen::Vector3d(d.dot(Eigen::Vector3d::Unit(u)),
                                             d.dot(Eigen::Vector3d::Unit(v)), 0.0);
                int vertexIdx = (int)quad.vertices.size();
                quad.vertices.push_back(vertex);

                mesh::Wedge wedge = mesh.faceWedge(f, i);
                wedge.vertex = vertexIdx;
                triangle.w[i] = (int)quad.wedges.size();
                quad.wedges.push_back(wedge);
            }
            quad.faces.push_back(triangle);
        }
    }

    // Quads with no mesh geometry ever facing them (e.g. a flat/open source
    // mesh) fall back to two triangles spanning the full quad rectangle, with
    // a synthetic unit-square UV, so the box stays closed.
    for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        BBoxQuad& quad = box.quads[quadIdx];
        if (!quad.faces.empty()) continue;

        int axis = QUAD_AXIS[quadIdx];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        double hu = box.halfExtents[u];
        double hv = box.halfExtents[v];

        std::array<Eigen::Vector2d, 4> corners = {Eigen::Vector2d(-hu, -hv),
                                                  Eigen::Vector2d(hu, -hv), Eigen::Vector2d(hu, hv),
                                                  Eigen::Vector2d(-hu, hv)};
        std::array<Eigen::Vector2d, 4> cornerUVs = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0),
                                                    Eigen::Vector2d(1, 1), Eigen::Vector2d(0, 1)};
        for (int i = 0; i < 4; i++) {
            mesh::Vertex vertex;
            vertex.pos = Eigen::Vector3d(corners[i].x(), corners[i].y(), 0.0);
            quad.vertices.push_back(vertex);

            mesh::Wedge wedge;
            wedge.vertex = i;
            wedge.uv = cornerUVs[i];
            quad.wedges.push_back(wedge);
        }
        if (QUAD_SIGN[quadIdx] > 0) {
            quad.faces.push_back({{0, 1, 2}});
            quad.faces.push_back({{0, 2, 3}});
        } else {
            quad.faces.push_back({{0, 2, 1}});
            quad.faces.push_back({{0, 3, 2}});
        }
    }

    mesh.vertices.clear();
    mesh.wedges.clear();
    mesh.faces.clear();

    // Each quad's local 2D vertices/UVs/triangles are flattened back into
    // 3D independently (no welding across quads), so the result is a
    // disconnected triangle soup at box edges/corners.
    for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        int axis = QUAD_AXIS[quadIdx];
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d quadOrigin =
            box.center + QUAD_SIGN[quadIdx] * box.halfExtents[axis] * Eigen::Vector3d::Unit(axis);
        Eigen::Vector3d axisU = Eigen::Vector3d::Unit(u);
        Eigen::Vector3d axisV = Eigen::Vector3d::Unit(v);

        const BBoxQuad& quad = box.quads[quadIdx];
        int vertexBase = (int)mesh.vertices.size();
        for (const mesh::Vertex& local : quad.vertices) {
            mesh::Vertex vertex;
            vertex.pos = quadOrigin + local.pos.x() * axisU + local.pos.y() * axisV;
            mesh.vertices.push_back(vertex);
        }

        int wedgeBase = (int)mesh.wedges.size();
        for (const mesh::Wedge& localWedge : quad.wedges) {
            mesh::Wedge wedge;
            wedge.vertex = vertexBase + localWedge.vertex;
            wedge.uv = localWedge.uv;
            mesh.wedges.push_back(wedge);
        }

        for (const mesh::Face& tri : quad.faces) {
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
