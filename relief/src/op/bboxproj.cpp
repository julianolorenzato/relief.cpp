/**
 * @file bboxproj.cpp
 */
#include "relief/op/bboxproj.h"

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include "relief/mesh.h"

namespace op::bboxproj {

namespace {

/// Areas / distances below this are treated as zero.
constexpr double EPS = 1e-12;

/// One corner of a clipped polygon, in a quad's local frame.
struct ClipVert {
    /// Position on the quad plane, relative to the box center.
    Eigen::Vector2d uv;
    /// Distance from the quad plane (smaller is nearer the viewer).
    double depth;
    /// Barycentric coordinates within the source mesh face.
    Eigen::Vector3d bary;
};

/// Convex polygon, counter-clockwise in (u, v).
using Poly = std::vector<ClipVert>;

/// A mesh face projected onto a quad, counter-clockwise in (u, v).
struct ProjTri {
    std::array<ClipVert, 3> v;
    /// UVs of the source face's wedges, in the same order as `v`.
    std::array<Eigen::Vector2d, 3> wedgeUV;
};

double cross(const Eigen::Vector2d& a, const Eigen::Vector2d& b) {
    return a.x() * b.y() - a.y() * b.x();
}

double signedArea(const Poly& p) {
    double area = 0.0;
    for (size_t i = 0; i < p.size(); i++) {
        area += cross(p[i].uv, p[(i + 1) % p.size()].uv);
    }
    return 0.5 * area;
}

ClipVert lerp(const ClipVert& a, const ClipVert& b, double t) {
    return {a.uv + t * (b.uv - a.uv), a.depth + t * (b.depth - a.depth),
            a.bary + t * (b.bary - a.bary)};
}

/**
 * @brief Splits a convex polygon by the line through a -> b.
 * @return {part on the left of a -> b (inside, for a CCW triangle), part on
 *         the right (outside)}. Either may be empty; vertices exactly on the
 *         line belong to both.
 */
std::pair<Poly, Poly> splitByHalfPlane(const Poly& poly, const Eigen::Vector2d& a,
                                       const Eigen::Vector2d& b) {
    Poly inside, outside;
    const Eigen::Vector2d dir = b - a;
    const size_t n = poly.size();

    for (size_t i = 0; i < n; i++) {
        const ClipVert& cur = poly[i];
        const ClipVert& nxt = poly[(i + 1) % n];
        const double dc = cross(dir, cur.uv - a);
        const double dn = cross(dir, nxt.uv - a);

        if (dc >= 0.0) inside.push_back(cur);
        if (dc <= 0.0) outside.push_back(cur);
        if ((dc > 0.0 && dn < 0.0) || (dc < 0.0 && dn > 0.0)) {
            const ClipVert cut = lerp(cur, nxt, dc / (dc - dn));
            inside.push_back(cut);
            outside.push_back(cut);
        }
    }
    return {std::move(inside), std::move(outside)};
}

/// Depth of the occluder's plane at a point of the quad.
double occluderDepthAt(const ProjTri& occ, const Eigen::Vector2d& p) {
    const Eigen::Vector2d e1 = occ.v[1].uv - occ.v[0].uv;
    const Eigen::Vector2d e2 = occ.v[2].uv - occ.v[0].uv;
    const double det = cross(e1, e2);
    const Eigen::Vector2d d = p - occ.v[0].uv;
    const double l1 = cross(d, e2) / det;
    const double l2 = cross(e1, d) / det;
    return occ.v[0].depth + l1 * (occ.v[1].depth - occ.v[0].depth) +
           l2 * (occ.v[2].depth - occ.v[0].depth);
}

/**
 * @brief Removes from `pieces` the parts hidden behind `occ`.
 *
 * A piece is only split when `occ` actually hides part of it: it must overlap
 * the piece in (u, v) with real area, and be nearer than the piece at every
 * corner of the overlap. Otherwise the piece is kept whole, so occluders that
 * merely share a grid cell never fragment it.
 */
void subtract(std::vector<Poly>& pieces, const ProjTri& occ) {
    std::vector<Poly> result;

    for (Poly& piece : pieces) {
        // Peel the piece along the occluder's three edges. `outside` collects
        // the parts beyond each edge; `rest` ends up as the overlap with `occ`.
        std::vector<Poly> outsides;
        Poly rest = piece;
        for (int i = 0; i < 3 && rest.size() >= 3; i++) {
            auto [inside, outside] = splitByHalfPlane(rest, occ.v[i].uv, occ.v[(i + 1) % 3].uv);
            if (outside.size() >= 3 && std::abs(signedArea(outside)) > EPS) {
                outsides.push_back(std::move(outside));
            }
            rest = std::move(inside);
        }

        // No overlap: the splits above were only an artifact of the infinite edge lines.
        bool hidden = rest.size() >= 3 && std::abs(signedArea(rest)) > EPS;
        if (hidden) {
            for (const ClipVert& cv : rest) {
                if (occluderDepthAt(occ, cv.uv) > cv.depth + EPS) hidden = false;
            }
        }

        if (!hidden) {
            result.push_back(std::move(piece));
            continue;
        }
        for (Poly& out : outsides) result.push_back(std::move(out));
    }
    pieces = std::move(result);
}

/// A non-vertical segment with `a.x < b.x`, used as the lower/upper bound of a free gap.
struct Seg {
    Eigen::Vector2d a, b;

    double yAt(double x) const { return a.y() + (x - a.x()) * (b.y() - a.y()) / (b.x() - a.x()); }
    bool operator==(const Seg& o) const { return a == o.a && b == o.b; }
};

/// A triangle already in the quad, in local (u, v) coordinates.
struct QuadTri {
    std::array<Eigen::Vector2d, 3> p;
    double xmin, xmax;
};

/// The cross-section of the pieces at one x: [lo, hi], each bounded by a triangle edge.
struct Interval {
    double lo, hi;
    Seg loSeg, hiSeg;
};

/// A free region between two bounding segments, open since `x0`.
struct Gap {
    Seg bot, top;
    double x0;
};

/**
 * @brief Triangulates the part of a quad's rectangle not covered by its faces.
 *
 * Sweeps vertical slabs delimited by every vertex x. Inside a slab no vertex
 * lies strictly within it, so the faces' cross-section at its middle is a set
 * of intervals; the gaps between them (and the rectangle's bottom/top) are
 * trapezoids. Adjacent slabs whose gap has the same bounding edges are merged
 * into one trapezoid, so interior vertices of the covered area cost nothing.
 * Handles holes and disconnected islands.
 *
 * Overlapping faces whose edges cross inside a slab are treated as if they
 * didn't, which can only misplace a gap boundary within that overlap.
 *
 * @param quad Quad whose faces are read and to which fill triangles are appended.
 * @param hu   Half extent of the quad rectangle along local x.
 * @param hv   Half extent of the quad rectangle along local y.
 * @param uv   UV given to every fill vertex.
 */
void fillQuad(BBoxQuad& quad, double hu, double hv, const Eigen::Vector2d& uv) {
    const double epsX = 1e-9 * hu;
    const double epsY = 1e-9 * hv;

    std::vector<QuadTri> tris;
    std::vector<double> xs{-hu, hu};
    for (const mesh::Face& f : quad.faces) {
        QuadTri t;
        for (int i = 0; i < 3; i++) {
            const Eigen::Vector3d& pos = quad.vertices[quad.wedges[f.w[i]].vertex].pos;
            t.p[i] = Eigen::Vector2d(std::clamp(pos.x(), -hu, hu), std::clamp(pos.y(), -hv, hv));
            xs.push_back(t.p[i].x());
        }
        t.xmin = std::min({t.p[0].x(), t.p[1].x(), t.p[2].x()});
        t.xmax = std::max({t.p[0].x(), t.p[1].x(), t.p[2].x()});
        tris.push_back(t);
    }
    std::sort(tris.begin(), tris.end(),
              [](const QuadTri& a, const QuadTri& b) { return a.xmin < b.xmin; });
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end(), [&](double a, double b) { return b - a <= epsX; }),
             xs.end());

    const Seg bottom{{-hu, -hv}, {hu, -hv}};
    const Seg topSeg{{-hu, hv}, {hu, hv}};

    auto addVertex = [&](double x, double y) {
        mesh::Vertex vertex;
        vertex.pos = Eigen::Vector3d(x, y, 0.0);
        quad.vertices.push_back(vertex);

        mesh::Wedge wedge;
        wedge.vertex = (int)quad.vertices.size() - 1;
        wedge.uv = uv;
        quad.wedges.push_back(wedge);
        return (int)quad.wedges.size() - 1;
    };
    // Faces are CCW in (u, v), whose normal is +axis; flip for -axis quads.
    auto addFace = [&](int w0, int w1, int w2) {
        mesh::Face f;
        f.w[0] = w0;
        f.w[1] = quad.info.sign < 0 ? w2 : w1;
        f.w[2] = quad.info.sign < 0 ? w1 : w2;
        quad.faces.push_back(f);
    };
    auto emit = [&](const Gap& g, double x1) {
        if (x1 - g.x0 <= epsX) return;
        const double b0 = g.bot.yAt(g.x0), b1 = g.bot.yAt(x1);
        const double t0 = g.top.yAt(g.x0), t1 = g.top.yAt(x1);
        const int bl = addVertex(g.x0, b0), br = addVertex(x1, b1);
        const int tr = addVertex(x1, t1), tl = addVertex(g.x0, t0);
        if (t1 - b1 > epsY || t0 - b0 > epsY) {
            addFace(bl, br, tr);
            addFace(bl, tr, tl);
        }
    };

    // The fill is appended to quad.faces while the sweep reads `tris`, a copy.
    std::vector<Gap> open;
    std::vector<const QuadTri*> active;
    size_t next = 0;

    for (size_t s = 0; s + 1 < xs.size(); s++) {
        const double x0 = xs[s], x1 = xs[s + 1], xm = 0.5 * (x0 + x1);

        while (next < tris.size() && tris[next].xmin < xm) active.push_back(&tris[next++]);
        active.erase(std::remove_if(active.begin(), active.end(),
                                    [xm](const QuadTri* t) { return t->xmax <= xm; }),
                     active.end());

        // Cross-section of each active triangle at xm.
        std::vector<Interval> intervals;
        for (const QuadTri* t : active) {
            Interval iv{std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(), {}, {}};
            for (int i = 0; i < 3; i++) {
                Eigen::Vector2d a = t->p[i], b = t->p[(i + 1) % 3];
                if (a.x() > b.x()) std::swap(a, b);
                if (!(a.x() < xm && xm < b.x())) continue;

                const Seg seg{a, b};
                const double y = seg.yAt(xm);
                if (y < iv.lo) iv.lo = y, iv.loSeg = seg;
                if (y > iv.hi) iv.hi = y, iv.hiSeg = seg;
            }
            if (iv.lo <= iv.hi) intervals.push_back(iv);
        }
        std::sort(intervals.begin(), intervals.end(),
                  [](const Interval& a, const Interval& b) { return a.lo < b.lo; });

        // Merge overlapping intervals, then collect the gaps between them.
        std::vector<Gap> gaps;
        Seg prevSeg = bottom;
        double prevY = -hv;
        for (size_t i = 0; i < intervals.size();) {
            Interval merged = intervals[i++];
            while (i < intervals.size() && intervals[i].lo <= merged.hi + epsY) {
                if (intervals[i].hi > merged.hi) merged.hi = intervals[i].hi, merged.hiSeg = intervals[i].hiSeg;
                i++;
            }
            if (merged.lo - prevY > epsY) gaps.push_back({prevSeg, merged.loSeg, x0});
            prevSeg = merged.hiSeg;
            prevY = merged.hi;
        }
        if (hv - prevY > epsY) gaps.push_back({prevSeg, topSeg, x0});

        // Extend gaps that continue from the previous slab; close the rest.
        std::vector<Gap> stillOpen;
        for (const Gap& g : open) {
            auto it = std::find_if(gaps.begin(), gaps.end(),
                                   [&](const Gap& c) { return c.bot == g.bot && c.top == g.top; });
            if (it == gaps.end()) {
                emit(g, x0);
            } else {
                stillOpen.push_back(g);
                gaps.erase(it);
            }
        }
        for (const Gap& g : gaps) stillOpen.push_back(g);
        open = std::move(stillOpen);
    }
    for (const Gap& g : open) emit(g, hu);
}

}  // namespace

void BBoxProjectionOp::apply(mesh::Mesh& mesh) const {
    BBox box(mesh, resolution_);
    const int res = GRID_RES;

    for (size_t quadIdx = 0; quadIdx < QUADS.size(); quadIdx++) {
        const QuadInfo& quad = QUADS[quadIdx];
        BBoxQuad& target = box.quads[quadIdx];

        const int axis = quad.axis;
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        const Eigen::Vector3d outward = quad.sign * Eigen::Vector3d::Unit(axis);
        const Eigen::Vector2d origin(-box.halfExtents[u], -box.halfExtents[v]);
        const Eigen::Vector2d cellSize(2 * box.halfExtents[u] / res, 2 * box.halfExtents[v] / res);

        // A flat box has no area to project onto on this quad.
        if ((cellSize.array() <= 0.0).any()) continue;

        // Distance from the quad's plane: smaller means closer to the quad (nearer the viewer).
        auto getDepth = [&](const Eigen::Vector3d& coord) -> double {
            return box.halfExtents[axis] - quad.sign * (coord[axis] - box.center[axis]);
        };

        // Projects a face onto the quad, or returns false if it is degenerate there.
        auto project = [&](int faceIdx, ProjTri& tri) -> bool {
            const mesh::Face& f = mesh.faces()[faceIdx];
            const auto verts = mesh.faceVertices(f);
            for (int i = 0; i < 3; i++) {
                const Eigen::Vector3d d = verts[i].pos - box.center;
                tri.v[i] = {Eigen::Vector2d(d[u], d[v]), getDepth(verts[i].pos),
                            Eigen::Vector3d::Unit(i)};
                tri.wedgeUV[i] = mesh.faceWedge(f, i).uv;
            }
            // Faces towards a -axis quad project clockwise; make them CCW.
            const double area = signedArea(Poly(tri.v.begin(), tri.v.end()));
            if (std::abs(area) <= EPS) return false;
            if (area < 0.0) {
                std::swap(tri.v[1], tri.v[2]);
                std::swap(tri.wedgeUV[1], tri.wedgeUV[2]);
            }
            return true;
        };

        auto toDiscreteBox = [&](const ProjTri& tri) -> Eigen::AlignedBox2i {
            Eigen::AlignedBox2d bbox2d;
            for (const ClipVert& cv : tri.v) bbox2d.extend(cv.uv);

            const Eigen::Vector2d lo = (bbox2d.min() - origin).cwiseQuotient(cellSize);
            const Eigen::Vector2d hi = (bbox2d.max() - origin).cwiseQuotient(cellSize);
            const Eigen::Vector2i last = Eigen::Vector2i::Constant(res - 1);
            return Eigen::AlignedBox2i(lo.array().floor().matrix().cast<int>().cwiseMax(0),
                                       hi.array().floor().matrix().cast<int>().cwiseMin(last));
        };

        // Project every face facing this quad, and bin it into the grid.
        std::vector<ProjTri> tris;
        std::vector<std::vector<int>> grid(res * res);  // cell -> indices into `tris`
        for (int faceIdx : mesh.getOutwardingFaces(outward)) {
            ProjTri tri;
            if (!project(faceIdx, tri)) continue;

            const int triIdx = (int)tris.size();
            const auto bbox2i = toDiscreteBox(tri);
            for (int y = bbox2i.min().y(); y <= bbox2i.max().y(); y++) {
                for (int x = bbox2i.min().x(); x <= bbox2i.max().x(); x++) {
                    grid[y * res + x].push_back(triIdx);
                }
            }
            tris.push_back(tri);
        }

        for (int triIdx = 0; triIdx < (int)tris.size(); triIdx++) {
            const ProjTri& tri = tris[triIdx];

            // Every face sharing a cell with this one is a candidate...
            std::set<int> candidates;
            const auto bbox2i = toDiscreteBox(tri);
            for (int y = bbox2i.min().y(); y <= bbox2i.max().y(); y++) {
                for (int x = bbox2i.min().x(); x <= bbox2i.max().x(); x++) {
                    const auto& cell = grid[y * res + x];
                    candidates.insert(cell.begin(), cell.end());
                }
            }

            // ...except itself, and faces that lie entirely behind it: an occluder
            // must reach closer to the quad than this face's farthest point.
            candidates.erase(triIdx);
            const double maxDepth = std::max({tri.v[0].depth, tri.v[1].depth, tri.v[2].depth});

            std::vector<Poly> pieces{Poly(tri.v.begin(), tri.v.end())};
            for (int occIdx : candidates) {
                const ProjTri& occ = tris[occIdx];
                const double minDepth = std::min({occ.v[0].depth, occ.v[1].depth, occ.v[2].depth});
                if (minDepth >= maxDepth) continue;

                subtract(pieces, occ);
                if (pieces.empty()) break;
            }

            // Fan-triangulate each surviving convex piece into the quad.
            for (const Poly& piece : pieces) {
                const int wedgeBase = (int)target.wedges.size();

                for (const ClipVert& cv : piece) {
                    mesh::Vertex vertex;
                    vertex.pos = Eigen::Vector3d(cv.uv.x(), cv.uv.y(), cv.depth);
                    target.vertices.push_back(vertex);

                    mesh::Wedge wedge;
                    wedge.vertex = (int)target.vertices.size() - 1;
                    for (int i = 0; i < 3; i++) wedge.uv += cv.bary[i] * tri.wedgeUV[i];
                    target.wedges.push_back(wedge);
                }

                for (int i = 1; i + 1 < (int)piece.size(); i++) {
                    mesh::Face f;
                    f.w[0] = wedgeBase;
                    f.w[1] = wedgeBase + i;
                    f.w[2] = wedgeBase + i + 1;
                    // Pieces are CCW in (u, v), whose normal is +axis; flip for -axis quads.
                    if (quad.sign < 0) std::swap(f.w[1], f.w[2]);
                    target.faces.push_back(f);
                }
            }
        }

        // Fill whatever the projected faces leave uncovered so the box is closed. The fill
        // has no source texel, so its vertices get UV (0, 0).
        fillQuad(target, box.halfExtents[u], box.halfExtents[v], Eigen::Vector2d::Zero());
    }

    box.exportTo(mesh);
}

void BBox::exportTo(mesh::Mesh& mesh) const {
    std::vector<mesh::Vertex> vertices;
    std::vector<mesh::Wedge> wedges;
    std::vector<mesh::Face> faces;
    std::array<int, 8> cornerVertex;
    cornerVertex.fill(-1);

    for (int quadIdx = 0; quadIdx < 6; quadIdx++) {
        const BBoxQuad& quad = quads[quadIdx];
        int axis = quad.info.axis;
        int u = (axis + 1) % 3;
        int v = (axis + 2) % 3;
        Eigen::Vector3d quadOrigin =
            center + quad.info.sign * halfExtents[axis] * Eigen::Vector3d::Unit(axis);
        Eigen::Vector3d axisU = Eigen::Vector3d::Unit(u);
        Eigen::Vector3d axisV = Eigen::Vector3d::Unit(v);

        // Vertices on a box corner are shared by the three quads meeting there.
        const double hu = halfExtents[u], hv = halfExtents[v];
        const double tol = 1e-9 * (hu + hv);
        std::vector<int> remap;
        for (const mesh::Vertex& local : quad.vertices) {
            const double x = local.pos.x(), y = local.pos.y();
            const bool atCorner = hu > 0.0 && hv > 0.0 && std::abs(std::abs(x) - hu) <= tol &&
                                  std::abs(std::abs(y) - hv) <= tol;
            if (atCorner) {
                const int corner = ((quad.info.sign > 0) << axis) | ((x > 0) << u) | ((y > 0) << v);
                if (cornerVertex[corner] < 0) {
                    cornerVertex[corner] = (int)vertices.size();
                    mesh::Vertex vertex;
                    vertex.pos = quadOrigin + (x > 0 ? hu : -hu) * axisU + (y > 0 ? hv : -hv) * axisV;
                    vertices.push_back(vertex);
                }
                remap.push_back(cornerVertex[corner]);
                continue;
            }
            mesh::Vertex vertex;
            vertex.pos = quadOrigin + x * axisU + y * axisV;
            remap.push_back((int)vertices.size());
            vertices.push_back(vertex);
        }

        int wedgeBase = (int)wedges.size();
        for (const mesh::Wedge& localWedge : quad.wedges) {
            mesh::Wedge wedge;
            wedge.vertex = remap[localWedge.vertex];
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
