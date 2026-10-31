#pragma once
#include "rdp_types.hpp"
#include "rdp_vertex.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// Shared RDP edge reconstruction. Attribute and sample coverage evaluation
// stays in fragment shaders, including on GL2 and Shader Model 3 devices.
inline std::vector<GLVertex> glrdp_triangle_geometry(const RDPTriangle &t) {
    const auto &e = t.edge;
    auto floor_div = [](std::int64_t n, std::int64_t d) { return n >= 0 ? n / d : -(((-n) + d - 1) / d); };
    auto edge = [&](std::int32_t x, std::int32_t dx, int y, int y0) {
        return std::int64_t(x) + floor_div(std::int64_t(dx) * (std::int64_t(y) - y0), 4);
    };
    auto f16 = [](std::int64_t x) { return float(double(x) / 65536.0); };
    auto fy = [](int y) { return float(double(y) / 4.0); };
    if (e.yh >= e.yl)
        return {};
    std::vector<GLVertex> v;
    v.reserve(12);
    auto span = [&](int y) {
        auto ma = edge(e.xh, e.dxhdy, y, e.yh & ~3);
        auto mi = y < e.ym ? edge(e.xm, e.dxmdy, y, e.yh & ~3) : edge(e.xl, e.dxldy, y, e.ym);
        return e.right_major ? std::pair{ma, mi} : std::pair{mi, ma};
    };
    auto make = [&](std::int64_t x, int y) {
        GLVertex vertex;
        vertex.x = f16(x);
        vertex.y = fy(y);
        return vertex;
    };
    // Bound both linear edge segments; attributes come from fragment uniforms.
    const int split = std::clamp(e.ym, e.yh, e.yl);
    const int boundaries[] = {e.yh, split, e.yl};
    for (int i = 0; i < 2; ++i) {
        const int y = boundaries[i], y1 = boundaries[i + 1];
        if (y1 <= y)
            continue;
        auto [l0, r0] = span(y);
        // Evaluate the upper minor edge at YM before switching to the lower one.
        const auto minor_end = i == 0 ? edge(e.xm, e.dxmdy, y1, e.yh & ~3) : edge(e.xl, e.dxldy, y1, e.ym);
        const auto major_end = edge(e.xh, e.dxhdy, y1, e.yh & ~3);
        const auto l1 = e.right_major ? major_end : minor_end;
        const auto r1 = e.right_major ? minor_end : major_end;
        if (r0 <= l0 && r1 <= l1)
            continue;
        auto a = make(l0, y), b = make(r0, y), c = make(r1, y1), d = make(l1, y1);
        v.insert(v.end(), {a, b, c, a, c, d});
    }
    return v;
}

// A conservative hull of GLRDP's reconstructed edge segments. Expanding by
// one console pixel contains every staggered RDP sample while avoiding the
// large empty areas of a full triangle bounding box. The fragment shader
// applies the original edges; the hull never substitutes hardware edge rules.
inline std::vector<std::array<float, 2>> glrdp_conservative_triangle(const std::vector<GLVertex> &vertices) {
    using Point = std::array<float, 2>;
    std::vector<Point> points;
    for (const auto &v : vertices)
        for (float dx : {-1.0f, 1.0f})
            for (float dy : {-1.0f, 1.0f})
                points.push_back({v.x + dx, v.y + dy});
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3)
        return {};
    auto cross = [](Point a, Point b, Point c) {
        return (double(b[0]) - a[0]) * (double(c[1]) - a[1]) - (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
    };
    std::vector<Point> hull;
    for (auto p : points) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), p) <= 0)
            hull.pop_back();
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (hull.size() > lower && cross(hull[hull.size() - 2], hull.back(), *it) <= 0)
            hull.pop_back();
        hull.push_back(*it);
    }
    hull.pop_back();
    std::vector<Point> triangles;
    for (std::size_t i = 1; i + 1 < hull.size(); ++i)
        triangles.insert(triangles.end(), {hull[0], hull[i], hull[i + 1]});
    return triangles;
}
