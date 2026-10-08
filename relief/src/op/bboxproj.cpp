/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "relief/mesh.h"

namespace op::bboxproj {

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box(mesh, resolution_);

    for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        BBoxQuad& quad = box.quads[quadIdx];
        const int axis = quad.info.axis;
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        const double hu = box.halfExtents[u];
        const double hv = box.halfExtents[v];
        const Eigen::Vector3d outward = quad.info.sign * Eigen::Vector3d::Unit(axis);

        // Project every mesh face facing this quad onto it, in original mesh
        // order (a face can face more than one quad, e.g. towards a corner,
        // in which case it's projected onto each).
        for (const auto& f : mesh.faces) {
            if (f.removed) continue;

            // Skip backfaces for this quad (this also skips degenerate faces,
            // whose normal is zero).
            Eigen::Vector3d normal = mesh.faceNormal(f);
            if (normal.dot(outward) <= 0.0) continue;

            mesh::Face triangle;
            for (int i = 0; i < 3; i++) {
                Eigen::Vector3d d = mesh.faceVertex(f, i).pos - box.center;
                double outwardCoord = quad.info.sign * d[axis];
                double depth = box.halfExtents[axis] - outwardCoord;

                // z is the depth: distance from this quad's plane.
                auto pos = Eigen::Vector3d(d[u], d[v], depth);

                int vertexIdx = (int)quad.vertices.size();
                quad.vertices.push_back(mesh::Vertex{.pos = pos});

                mesh::Wedge wedge = mesh.faceWedge(f, i);
                wedge.vertex = vertexIdx;

                triangle.w[i] = (int)quad.wedges.size();
                quad.wedges.push_back(wedge);
            }
            quad.faces.push_back(triangle);
        }

        // Resolve occlusion: rasterize the triangles into the depth buffer
        // (nearest to the quad plane wins; ties go to the earlier triangle),
        // then drop every triangle that doesn't win a pixel. Surviving
        // triangles are kept whole, not clipped, so partly occluded ones
        // stay. A triangle too small or thin to cover a pixel center is
        // dropped as well.
        const int res = quad.resolution;

        // Maps local quad coordinates to continuous pixel coordinates. A
        // zero-extent axis collapses to 0, which makes the triangle degenerate.
        auto toPixel = [&](const Eigen::Vector3d& pos) {
            return Eigen::Vector2d(hu > 0.0 ? (pos.x() + hu) / (2.0 * hu) * res : 0.0,
                                   hv > 0.0 ? (pos.y() + hv) / (2.0 * hv) * res : 0.0);
        };
        auto cross = [](const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
            return a.x() * b.y() - a.y() * b.x();
        };

        for (int faceIdx = 0; faceIdx < (int)quad.faces.size(); faceIdx++) {
            const mesh::Face& face = quad.faces[faceIdx];
            Eigen::Vector2d s[3];
            double z[3];
            for (int i = 0; i < 3; i++) {
                const mesh::Vertex& vertex = quad.vertices[quad.wedges[face.w[i]].vertex];
                s[i] = toPixel(vertex.pos);
                z[i] = vertex.pos.z();
            }

            double area = cross(s[1] - s[0], s[2] - s[0]);
            if (std::abs(area) < 1e-12) continue;

            Eigen::Vector2d lo = s[0].cwiseMin(s[1]).cwiseMin(s[2]);
            Eigen::Vector2d hi = s[0].cwiseMax(s[1]).cwiseMax(s[2]);
            int x0 = std::max(0, (int)std::floor(lo.x()));
            int x1 = std::min(res - 1, (int)std::ceil(hi.x()));
            int y0 = std::max(0, (int)std::floor(lo.y()));
            int y1 = std::min(res - 1, (int)std::ceil(hi.y()));

            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    Eigen::Vector2d c(x + 0.5, y + 0.5);
                    double l0 = cross(s[1] - c, s[2] - c) / area;
                    double l1 = cross(s[2] - c, s[0] - c) / area;
                    double l2 = cross(s[0] - c, s[1] - c) / area;
                    if (l0 < 0.0 || l1 < 0.0 || l2 < 0.0) continue;

                    double depth = l0 * z[0] + l1 * z[1] + l2 * z[2];
                    int pixel = y * res + x;
                    if (depth < quad.depth[pixel]) {
                        quad.depth[pixel] = depth;
                        quad.owner[pixel] = faceIdx;
                    }
                }
            }
        }

        // Keep only the triangles that own at least one pixel.
        std::vector<bool> visible(quad.faces.size(), false);
        for (int id : quad.owner) {
            if (id >= 0) visible[id] = true;
        }

        std::vector<mesh::Vertex> vertices;
        std::vector<mesh::Wedge> wedges;
        std::vector<mesh::Face> faces;
        for (int faceIdx = 0; faceIdx < (int)quad.faces.size(); faceIdx++) {
            if (!visible[faceIdx]) continue;

            mesh::Face kept;
            for (int i = 0; i < 3; i++) {
                mesh::Wedge wedge = quad.wedges[quad.faces[faceIdx].w[i]];
                vertices.push_back(quad.vertices[wedge.vertex]);
                wedge.vertex = (int)vertices.size() - 1;
                kept.w[i] = (int)wedges.size();
                wedges.push_back(wedge);
            }
            faces.push_back(kept);
        }
        quad.vertices = std::move(vertices);
        quad.wedges = std::move(wedges);
        quad.faces = std::move(faces);

        // // Quads with no mesh geometry ever facing them (e.g. a flat/open
        // // source mesh) fall back to two triangles spanning the full quad
        // // rectangle, with a synthetic unit-square UV, so the box stays closed.
        // if (!quad.faces.empty()) continue;

        // std::array<Eigen::Vector2d, 4> corners = {Eigen::Vector2d(-hu, -hv),
        //                                           Eigen::Vector2d(hu, -hv), Eigen::Vector2d(hu,
        //                                           hv), Eigen::Vector2d(-hu, hv)};
        // std::array<Eigen::Vector2d, 4> cornerUVs = {Eigen::Vector2d(0, 0), Eigen::Vector2d(1, 0),
        //                                             Eigen::Vector2d(1, 1), Eigen::Vector2d(0,
        //                                             1)};
        // for (int i = 0; i < 4; i++) {
        //     mesh::Vertex vertex;
        //     vertex.pos = Eigen::Vector3d(corners[i].x(), corners[i].y(), 0.0);
        //     quad.vertices.push_back(vertex);

        //     mesh::Wedge wedge;
        //     wedge.vertex = i;
        //     wedge.uv = cornerUVs[i];
        //     quad.wedges.push_back(wedge);
        // }
        // if (quad.info.sign > 0) {
        //     quad.faces.push_back({{0, 1, 2}});
        //     quad.faces.push_back({{0, 2, 3}});
        // } else {
        //     quad.faces.push_back({{0, 2, 1}});
        //     quad.faces.push_back({{0, 3, 2}});
        // }
    }

    box.exportTo(mesh);

    mesh.logSummary();
}

void BBoxProjectionOp::handleQuad(const QuadInfo& quad, BBox& box, mesh::Mesh& mesh) {
    auto edgeToFaces = mesh.buildEdgeToFaces();

    const int u = (quad.axis + 1) % 3;
    const int v = (quad.axis + 2) % 3;
    const double hu = box.halfExtents[u];
    const double hv = box.halfExtents[v];
    const Eigen::Vector3d outward = quad.sign * Eigen::Vector3d::Unit(quad.axis);

    std::vector<std::pair<int, int>> edges;

    // for by island, for by islandFace?

    for (const auto& f : mesh.faces) {
        if (f.removed) continue;
        if (mesh.faceNormal(f).dot(outward) <= 0.0) continue;

        const auto fEdges = mesh.faceEdges(f);
        // Check which one in fEdges are boundary/seam

        auto a = edgeToFaces[mesh::Edge(1, 2)];
    }
}

void BBox::exportTo(mesh::Mesh& mesh) const {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;

    for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        const BBoxQuad& quad = quads[quadIdx];
        int axis = quad.info.axis;
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d quadOrigin =
            center + quad.info.sign * halfExtents[axis] * Eigen::Vector3d::Unit(axis);
        Eigen::Vector3d axisU = Eigen::Vector3d::Unit(u);
        Eigen::Vector3d axisV = Eigen::Vector3d::Unit(v);

        int vertexBase = (int)vertices.size();
        for (const mesh::Vertex& local : quad.vertices) {
            mesh::Vertex vertex;
            vertex.pos = quadOrigin + local.pos.x() * axisU + local.pos.y() * axisV;
            vertices.push_back(vertex);
        }

        int wedgeBase = (int)wedges.size();
        for (const mesh::Wedge& localWedge : quad.wedges) {
            mesh::Wedge wedge;
            wedge.vertex = vertexBase + localWedge.vertex;
            wedge.uv = localWedge.uv;
            wedges.push_back(wedge);
        }

        for (const mesh::Face& tri : quad.faces) {
            mesh::Face f;
            f.w[0] = wedgeBase + tri.w[0];
            f.w[1] = wedgeBase + tri.w[1];
            f.w[2] = wedgeBase + tri.w[2];
            faces.push_back(f);
        }
    }
    mesh.replaceGeometry(std::move(vertices), std::move(wedges), std::move(faces));
}

}  // namespace op::bboxproj
