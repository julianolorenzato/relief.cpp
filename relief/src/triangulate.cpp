/**
 * @file triangulate.cpp
 */
#include "relief/triangulate.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace triangulate {

namespace {

constexpr double kEps = 1e-12;

/// @return Twice the signed area of triangle (a, b, c); > 0 if counter-clockwise.
double cross(const Eigen::Vector2d& a, const Eigen::Vector2d& b, const Eigen::Vector2d& c) {
    return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
}

/// @return true if `p` lies inside or on the boundary of CCW triangle (a, b, c).
bool inTriangle(const Eigen::Vector2d& p, const Eigen::Vector2d& a, const Eigen::Vector2d& b,
                const Eigen::Vector2d& c) {
    return cross(a, b, p) >= -kEps && cross(b, c, p) >= -kEps && cross(c, a, p) >= -kEps;
}

}  // namespace

Patch triangulatePolygon(const std::vector<Eigen::Vector2d>& polygon) {
    Patch patch;
    const int n = (int)polygon.size();

    for (int i = 0; i < n; i++) {
        patch.vertices.push_back(mesh::Vertex{.pos = Eigen::Vector3d(polygon[i].x(), polygon[i].y(), 0.0)});
        patch.wedges.push_back(mesh::Wedge{.vertex = i});
    }
    if (n < 3) return patch;

    // Remaining polygon as indices into `polygon`, forced to CCW order.
    double area2 = 0.0;
    for (int i = 0; i < n; i++) {
        const Eigen::Vector2d& a = polygon[i];
        const Eigen::Vector2d& b = polygon[(i + 1) % n];
        area2 += a.x() * b.y() - b.x() * a.y();
    }
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    if (area2 < 0.0) std::reverse(idx.begin(), idx.end());

    auto emit = [&](int a, int b, int c) {
        mesh::Face f;
        f.w[0] = a;
        f.w[1] = b;
        f.w[2] = c;
        patch.faces.push_back(f);
    };

    while (idx.size() > 3) {
        const int m = (int)idx.size();
        int earPos = -1;

        for (int i = 0; i < m && earPos < 0; i++) {
            int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
            const Eigen::Vector2d &a = polygon[ia], &b = polygon[ib], &c = polygon[ic];

            if (cross(a, b, c) <= kEps) continue;  // reflex or collinear: not an ear

            bool empty = true;
            for (int j = 0; j < m && empty; j++) {
                int ip = idx[j];
                if (ip == ia || ip == ib || ip == ic) continue;
                const Eigen::Vector2d& p = polygon[ip];
                // Duplicates of a corner (e.g. a pinched outline) don't block the ear.
                if (p == a || p == b || p == c) continue;
                if (inTriangle(p, a, b, c)) empty = false;
            }
            if (empty) earPos = i;
        }

        if (earPos < 0) {
            // No valid ear (degenerate/self-touching input). Drop a collinear
            // vertex if there is one, otherwise clip the most convex corner so
            // the loop always terminates.
            double best = -1e300;
            for (int i = 0; i < m; i++) {
                double c = cross(polygon[idx[(i + m - 1) % m]], polygon[idx[i]],
                                 polygon[idx[(i + 1) % m]]);
                if (std::abs(c) <= kEps) {
                    earPos = -2 - i;  // collinear: remove without emitting a face
                    break;
                }
                if (c > best) {
                    best = c;
                    earPos = i;
                }
            }
            if (earPos <= -2) {
                idx.erase(idx.begin() + (-2 - earPos));
                continue;
            }
        }

        emit(idx[(earPos + m - 1) % m], idx[earPos], idx[(earPos + 1) % m]);
        idx.erase(idx.begin() + earPos);
    }

    if (cross(polygon[idx[0]], polygon[idx[1]], polygon[idx[2]]) > kEps) {
        emit(idx[0], idx[1], idx[2]);
    }
    return patch;
}

}  // namespace triangulate
