/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

#include <Eigen/src/Core/Matrix.h>
#include <Eigen/src/Geometry/AlignedBox.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <vector>

#include "relief/mesh.h"
#include "relief/triangulate.h"

namespace op::bboxproj {

namespace {
    void clipBySide() {}
void clipTriangle(mesh::Mesh& mesh, mesh::Face& f, std::set<mesh::Face> candidates) {
    std::vector<mesh::Face> parts;
    parts.push_back(f);

    for (auto& e : mesh.faceEdges(f)) {
        clipBySide();
    }
}
}  // namespace

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box(mesh, resolution_);

    for (const auto& quad : QUADS) {
        const int res = 64;
        std::vector<std::vector<int>> grid(res * res);

        // BBoxProjectionOp::handleQuad(quad, box, mesh);
        const auto axis = quad.axis;
        const int u = (quad.axis + 1) % 3;
        const int v = (quad.axis + 2) % 3;
        const Eigen::Vector3d outward = quad.sign * Eigen::Vector3d::Unit(quad.axis);
        const Eigen::Vector2d origin(-box.halfExtents[u], -box.halfExtents[v]);

        auto getUV = [u, v](const Eigen::Vector3d& coord) -> Eigen::Vector2d {
            return Eigen::Vector2d(coord[u], coord[v]);
        };
        // Distance from the quad's plane: smaller means closer to the quad (nearer the viewer).
        auto getDepth = [&](const Eigen::Vector3d& coord) -> double {
            return box.halfExtents[axis] - quad.sign * (coord[axis] - box.center[axis]);
        };
        auto depthRange = [&](int faceIdx) -> Eigen::AlignedBox1d {
            Eigen::AlignedBox1d range;
            for (const auto& vert : mesh.faceVertices(mesh.faces()[faceIdx])) {
                range.extend(Eigen::Matrix<double, 1, 1>(getDepth(vert.pos)));
            }
            return range;
        };

        const Eigen::Vector2d cellSize(2 * box.halfExtents[u] / res, 2 * box.halfExtents[v] / res);
        auto toDiscreteBox = [origin, cellSize](Eigen::AlignedBox2d bbox2d) -> Eigen::AlignedBox2i {
            const Eigen::Vector2d lo = (bbox2d.min() - origin).cwiseQuotient(cellSize);
            const Eigen::Vector2d hi = (bbox2d.max() - origin).cwiseQuotient(cellSize);

            const Eigen::Vector2i last = Eigen::Vector2i::Constant(res - 1);
            Eigen::AlignedBox2i bbox2i(lo.array().floor().matrix().cast<int>().cwiseMax(0),
                                       hi.array().floor().matrix().cast<int>().cwiseMin(last));

            return bbox2i;
        };

        // A flat box has no area to project onto on this quad.
        if ((cellSize.array() <= 0.0).any()) continue;

        // Build the GRID
        for (int faceIdx : mesh.getOutwardingFaces(outward)) {
            Eigen::AlignedBox2d bbox2d;

            // Create the bounding box
            for (auto& v : mesh.faceVertices(mesh.faces()[faceIdx])) {
                bbox2d.extend(getUV(v.pos - box.center));
            }

            auto bbox2i = toDiscreteBox(bbox2d);

            for (int y = bbox2i.min().y(); y <= bbox2i.max().y(); y++) {
                for (int x = bbox2i.min().x(); x <= bbox2i.max().x(); x++) {
                    grid[y * res + x].push_back(faceIdx);
                }
            }
        }

        // Query Grid
        for (int faceIdx : mesh.getOutwardingFaces(outward)) {
            std::set<int> faceOccluderCandidates;
            Eigen::AlignedBox2d bbox2d;

            for (auto& v : mesh.faceVertices(mesh.faces()[faceIdx])) {
                bbox2d.extend(getUV(v.pos - box.center));
            }

            auto bbox2i = toDiscreteBox(bbox2d);

            // Every face sharing a cell with this one is a candidate...
            for (int y = bbox2i.min().y(); y <= bbox2i.max().y(); y++) {
                for (int x = bbox2i.min().x(); x <= bbox2i.max().x(); x++) {
                    const auto& cell = grid[y * res + x];
                    faceOccluderCandidates.insert(cell.begin(), cell.end());
                }
            }

            // ...except itself, and faces that lie entirely behind it: an occluder
            // must reach closer to the quad than this face's farthest point.
            faceOccluderCandidates.erase(faceIdx);

            const double faceMaxDepth = depthRange(faceIdx).max()[0];
            for (auto it = faceOccluderCandidates.begin(); it != faceOccluderCandidates.end();) {
                if (depthRange(*it).min()[0] >= faceMaxDepth) {
                    it = faceOccluderCandidates.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        //     BBoxQuad& quad = box.quads[quadIdx];
        //     const int axis = quad.info.axis;
        //     const int u = (axis + 1) % 3;
        //     const int v = (axis + 2) % 3;
        //     const Eigen::Vector3d outward = quad.info.sign * Eigen::Vector3d::Unit(axis);

        //     // Project every mesh face facing this quad onto it, in original mesh
        //     // order (a face can face more than one quad, e.g. towards a corner,
        //     // in which case it's projected onto each).
        //     for (const auto& f : mesh.faces()) {
        //         if (f.removed) continue;

        //         // Skip backfaces for this quad (this also skips degenerate faces,
        //         // whose normal is zero).
        //         Eigen::Vector3d normal = mesh.faceNormal(f);
        //         if (normal.dot(outward) <= 0.0) continue;

        //         mesh::Face triangle;
        //         for (int i = 0; i < 3; i++) {
        //             Eigen::Vector3d d = mesh.faceVertex(f, i).pos - box.center;
        //             double outwardCoord = quad.info.sign * d[axis];
        //             double depth = box.halfExtents[axis] - outwardCoord;

        //             // z is the depth: distance from this quad's plane.
        //             auto pos = Eigen::Vector3d(d[u], d[v], depth);

        //             int vertexIdx = (int)quad.vertices.size();
        //             quad.vertices.push_back(mesh::Vertex{.pos = pos});

        //             mesh::Wedge wedge = mesh.faceWedge(f, i);
        //             wedge.vertex = vertexIdx;

        //             triangle.w[i] = (int)quad.wedges.size();
        //             quad.wedges.push_back(wedge);
        //         }
        //         quad.faces.push_back(triangle);
        //     }

        //     // Resolve occlusion: rasterize the triangles into the depth buffer
        //     // (nearest to the quad plane wins; ties go to the earlier triangle),
        //     // then drop every triangle that doesn't win a pixel. Surviving
        //     // triangles are kept whole, not clipped, so partly occluded ones
        //     // stay. A triangle too small or thin to cover a pixel center is
        //     // dropped as well.
        //     const int res = quad.resolution;

        //     // Maps local quad coordinates to continuous pixel coordinates. A
        //     // zero-extent axis collapses to 0, which makes the triangle degenerate.
        //     auto toPixel = [&](const Eigen::Vector3d& pos) {
        //         return Eigen::Vector2d(hu > 0.0 ? (pos.x() + hu) / (2.0 * hu) * res : 0.0,
        //                                hv > 0.0 ? (pos.y() + hv) / (2.0 * hv) * res : 0.0);
        //     };
        //     auto cross = [](const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
        //         return a.x() * b.y() - a.y() * b.x();
        //     };

        //     for (int faceIdx = 0; faceIdx < (int)quad.faces.size(); faceIdx++) {
        //         const mesh::Face& face = quad.faces[faceIdx];
        //         Eigen::Vector2d s[3];
        //         double z[3];
        //         for (int i = 0; i < 3; i++) {
        //             const mesh::Vertex& vertex = quad.vertices[quad.wedges[face.w[i]].vertex];
        //             s[i] = toPixel(vertex.pos);
        //             z[i] = vertex.pos.z();
        //         }

        //         double area = cross(s[1] - s[0], s[2] - s[0]);
        //         if (std::abs(area) < 1e-12) continue;

        //         Eigen::Vector2d lo = s[0].cwiseMin(s[1]).cwiseMin(s[2]);
        //         Eigen::Vector2d hi = s[0].cwiseMax(s[1]).cwiseMax(s[2]);
        //         int x0 = std::max(0, (int)std::floor(lo.x()));
        //         int x1 = std::min(res - 1, (int)std::ceil(hi.x()));
        //         int y0 = std::max(0, (int)std::floor(lo.y()));
        //         int y1 = std::min(res - 1, (int)std::ceil(hi.y()));

        //         for (int y = y0; y <= y1; y++) {
        //             for (int x = x0; x <= x1; x++) {
        //                 Eigen::Vector2d c(x + 0.5, y + 0.5);
        //                 double l0 = cross(s[1] - c, s[2] - c) / area;
        //                 double l1 = cross(s[2] - c, s[0] - c) / area;
        //                 double l2 = cross(s[0] - c, s[1] - c) / area;
        //                 if (l0 < 0.0 || l1 < 0.0 || l2 < 0.0) continue;

        //                 double depth = l0 * z[0] + l1 * z[1] + l2 * z[2];
        //                 int pixel = y * res + x;
        //                 if (depth < quad.depth[pixel]) {
        //                     quad.depth[pixel] = depth;
        //                     quad.owner[pixel] = faceIdx;
        //                 }
        //             }
        //         }
        //     }

        //     // Keep only the triangles that own at least one pixel.
        //     std::vector<bool> visible(quad.faces.size(), false);
        //     for (int id : quad.owner) {
        //         if (id >= 0) visible[id] = true;
        //     }

        //     std::vector<mesh::Vertex> vertices;
        //     std::vector<mesh::Wedge> wedges;
        //     std::vector<mesh::Face> faces;
        //     for (int faceIdx = 0; faceIdx < (int)quad.faces.size(); faceIdx++) {
        //         if (!visible[faceIdx]) continue;

        //         mesh::Face kept;
        //         for (int i = 0; i < 3; i++) {
        //             mesh::Wedge wedge = quad.wedges[quad.faces[faceIdx].w[i]];
        //             vertices.push_back(quad.vertices[wedge.vertex]);
        //             wedge.vertex = (int)vertices.size() - 1;
        //             kept.w[i] = (int)wedges.size();
        //             wedges.push_back(wedge);
        //         }
        //         faces.push_back(kept);
        //     }
        //     quad.vertices = std::move(vertices);
        //     quad.wedges = std::move(wedges);
        //     quad.faces = std::move(faces);

        //     // // Quads with no mesh geometry ever facing them (e.g. a flat/open
        //     // // source mesh) fall back to two triangles spanning the full quad
        //     // // rectangle, with a synthetic unit-square UV, so the box stays closed.
        //     // if (!quad.faces.empty()) continue;

        //     // std::array<Eigen::Vector2d, 4> corners = {Eigen::Vector2d(-hu, -hv),
        //     //                                           Eigen::Vector2d(hu, -hv),
        //     Eigen::Vector2d(hu,
        //     //                                           hv), Eigen::Vector2d(-hu, hv)};
        //     // std::array<Eigen::Vector2d, 4> cornerUVs = {Eigen::Vector2d(0, 0),
        //     Eigen::Vector2d(1, 0),
        //     //                                             Eigen::Vector2d(1, 1),
        //     Eigen::Vector2d(0,
        //     //                                             1)};
        //     // for (int i = 0; i < 4; i++) {
        //     //     mesh::Vertex vertex;
        //     //     vertex.pos = Eigen::Vector3d(corners[i].x(), corners[i].y(), 0.0);
        //     //     quad.vertices.push_back(vertex);

        //     //     mesh::Wedge wedge;
        //     //     wedge.vertex = i;
        //     //     wedge.uv = cornerUVs[i];
        //     //     quad.wedges.push_back(wedge);
        //     // }
        //     // if (quad.info.sign > 0) {
        //     //     quad.faces.push_back({{0, 1, 2}});
        //     //     quad.faces.push_back({{0, 2, 3}});
        //     // } else {
        //     //     quad.faces.push_back({{0, 2, 1}});
        //     //     quad.faces.push_back({{0, 3, 2}});
        //     // }
        // }
    }

    box.exportTo(mesh);
}

void BBoxProjectionOp::handleQuad(const QuadInfo& quad, BBox& box, mesh::Mesh& mesh) const {
    const int u = (quad.axis + 1) % 3;
    const int v = (quad.axis + 2) % 3;
    const Eigen::Vector3d outward = quad.sign * Eigen::Vector3d::Unit(quad.axis);
    auto outwardingFaces = mesh.getOutwardingFaces(outward);

    // const auto edgeToFaces = mesh.buildEdgeToFaces();
    // const auto& faces = mesh.faces();
    // auto isOutward = [&faces, &mesh, &outward](int fi) {
    //     return !faces[fi].removed && mesh.faceNormal(faces[fi]).dot(outward) > 0.0;
    // };

    // Outline edges of the outward faces, clustered per UV island.
    // std::map<int, std::vector<std::pair<int, int>>> edgesByIsland;
    // for (int fi = 0; fi < (int)faces.size(); fi++) {
    //     if (!isOutward(fi)) continue;
    //     const int island = faces[fi].island;

    //     for (const auto& e : mesh.faceEdges(faces[fi])) {
    //         // Only outward faces of the same island count: an edge whose other
    //         // neighbors face away or belong to another island is a border of
    //         // this island's outline (a seam is a border of both islands).
    //         std::vector<int> sameIsland;
    //         for (int nb : edgeToFaces.at(e)) {
    //             if (isOutward(nb) && faces[nb].island == island) sameIsland.push_back(nb);
    //         }

    //         // Each edge is visited from every such face; record it once.
    //         if (sameIsland.front() != fi) continue;
    //         if (sameIsland.size() == 1) edgesByIsland[island].emplace_back(e.first,
    //         e.second);
    //     }
    // }

    // auto edgeKey = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b));
    // };

    // // Chains unordered edges into closed loops of vertex ids.
    // auto chainLoops = [&edgeKey](const std::vector<std::pair<int, int>>& edges) {
    //     std::map<int, std::vector<int>> adjacency;
    //     for (const auto& [a, b] : edges) {
    //         adjacency[a].push_back(b);
    //         adjacency[b].push_back(a);
    //     }

    //     std::set<std::pair<int, int>> usedEdges;
    //     std::vector<std::vector<int>> loops;
    //     for (const auto& [start, neighbors] : adjacency) {
    //         for (int first : neighbors) {
    //             if (usedEdges.count(edgeKey(start, first))) continue;

    //             std::vector<int> loop{start};
    //             int prev = start;
    //             int cur = first;
    //             usedEdges.insert(edgeKey(start, first));
    //             while (cur != start) {
    //                 loop.push_back(cur);
    //                 int next = -1;
    //                 for (int nb : adjacency[cur]) {
    //                     if (nb != prev && !usedEdges.count(edgeKey(cur, nb))) {
    //                         next = nb;
    //                         break;
    //                     }
    //                 }
    //                 if (next < 0) break;  // open chain: not a closed loop
    //                 usedEdges.insert(edgeKey(cur, next));
    //                 prev = cur;
    //                 cur = next;
    //             }
    //             if (cur == start && loop.size() >= 3) loops.push_back(std::move(loop));
    //         }
    //     }
    //     return loops;
    // };

    // // Closed loops, tagged with the island they outline.
    // std::vector<std::pair<int, std::vector<int>>> loops;
    // for (const auto& [island, edges] : edgesByIsland) {
    //     for (auto& loop : chainLoops(edges)) loops.emplace_back(island, std::move(loop));
    // }

    // // Project each loop onto the quad's plane as local (u, v) coordinates
    // // relative to the box center, matching BBox::exportTo.
    // const auto& vertices = mesh.vertices();
    // auto project = [&](const std::vector<int>& loop) {
    //     std::vector<Eigen::Vector2d> pts;
    //     pts.reserve(loop.size());
    //     for (int vi : loop) {
    //         const Eigen::Vector3d d = vertices[vi].pos - box.center;
    //         pts.emplace_back(d[u], d[v]);
    //     }
    //     return pts;
    // };

    // // Triangulate every loop (one silhouette each) and merge the patches into
    // // the quad. Holes are unsupported by triangulatePolygon, so a loop nested
    // // inside another is filled like any other.
    // BBoxQuad& target = box.quads[quad.axis * 2 + (quad.sign > 0 ? 1 : 0)];
    // target.vertices.clear();
    // target.wedges.clear();
    // target.faces.clear();

    // // TEMPORARY: one fixed pseudo-random UV per island, so each island samples a
    // // single texel of the color texture and renders as a flat color.
    // auto islandUV = [](int island) {
    //     std::mt19937 rng(static_cast<uint32_t>(island));
    //     std::uniform_real_distribution<double> dist(0.0, 1.0);
    //     return Eigen::Vector2d(dist(rng), dist(rng));
    // };

    // for (const auto& [island, loop] : loops) {
    //     const Eigen::Vector2d uv = islandUV(island);
    //     triangulate::Patch patch = triangulate::triangulatePolygon(project(loop));
    //     const int vertexBase = (int)target.vertices.size();
    //     const int wedgeBase = (int)target.wedges.size();

    //     for (auto& vtx : patch.vertices) target.vertices.push_back(vtx);
    //     for (size_t i = 0; i < patch.wedges.size(); i++) {
    //         mesh::Wedge wedge;
    //         wedge.vertex = vertexBase + patch.wedges[i].vertex;
    //         wedge.uv = uv;
    //         target.wedges.push_back(wedge);
    //     }
    //     for (auto f : patch.faces) {
    //         // Patch faces are CCW in (u, v), whose normal is +axis; flip for -axis quads.
    //         if (quad.sign < 0) std::swap(f.w[1], f.w[2]);
    //         for (int& w : f.w) w += wedgeBase;
    //         target.faces.push_back(f);
    //     }
    // }
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
