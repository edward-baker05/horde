#include "sim/WallColliders.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "logic/LevelGeometry.hpp"

namespace horde::sim {

namespace {

constexpr float kEpsilon = 1e-6f;

float cross(glm::vec2 a, glm::vec2 b) {
    return a.x * b.y - a.y * b.x;
}

glm::vec2 closestOnSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 ab = b - a;
    const float lengthSq = glm::dot(ab, ab);
    const float t = lengthSq > kEpsilon ? std::clamp(glm::dot(p - a, ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
    return a + ab * t;
}

float signOrOne(float v) {
    return v < 0.0f ? -1.0f : 1.0f;
}

void primitiveBounds(const WallPrimitive& p, glm::vec2& lo, glm::vec2& hi) {
    switch (p.kind) {
        case WallPrimitive::Box: {
            const glm::vec2 axisY{-p.p1.y, p.p1.x};
            const glm::vec2 extent = glm::abs(p.p1) * p.p2.x + glm::abs(axisY) * p.p2.y;
            lo = p.p0 - extent;
            hi = p.p0 + extent;
            return;
        }
        case WallPrimitive::Circle:
            lo = p.p0 - glm::vec2(p.radius);
            hi = p.p0 + glm::vec2(p.radius);
            return;
        case WallPrimitive::Capsule:
            lo = glm::min(p.p0, p.p1) - glm::vec2(p.radius);
            hi = glm::max(p.p0, p.p1) + glm::vec2(p.radius);
            return;
        default:
            lo = glm::min(glm::min(p.p0, p.p1), p.p2);
            hi = glm::max(glm::max(p.p0, p.p1), p.p2);
            return;
    }
}

void appendWall(const logic::Wall& wall, std::vector<WallPrimitive>& out) {
    const glm::vec2 axisX{std::cos(wall.rotation), std::sin(wall.rotation)};

    if (const auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        out.push_back({wall.center, axisX, rect->halfExtents, WallPrimitive::Box, 0.0f});
        return;
    }
    if (const auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        // Apex at (0, -hy), base from (-hx, +hy) to (+hx, +hy), as HitTest and the atlas draw it.
        const glm::vec2 h = tri->halfExtents;
        out.push_back({logic::localToWorld(wall, {0.0f, -h.y}), logic::localToWorld(wall, {-h.x, h.y}),
                       logic::localToWorld(wall, {h.x, h.y}), WallPrimitive::Triangle, 0.0f});
        return;
    }
    if (const auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        out.push_back({wall.center, {0.0f, 0.0f}, {0.0f, 0.0f}, WallPrimitive::Circle, circle->radius});
        return;
    }

    // Each polyline segment is a capsule. Rounded ends also close the gaps the
    // renderer leaves at bends, which is what a unit sliding along it wants.
    const auto& line = std::get<logic::PolylineShape>(wall.shape);
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        out.push_back({logic::localToWorld(wall, line.points[i]), logic::localToWorld(wall, line.points[i + 1]),
                       {0.0f, 0.0f}, WallPrimitive::Capsule, line.thickness * 0.5f});
    }
}

} // namespace

float primitiveDistance(const WallPrimitive& p, glm::vec2 point, glm::vec2& normal) {
    switch (p.kind) {
        case WallPrimitive::Box: {
            const glm::vec2 axisY{-p.p1.y, p.p1.x};
            const glm::vec2 d = point - p.p0;
            const glm::vec2 q{glm::dot(d, p.p1), glm::dot(d, axisY)};
            const glm::vec2 e = glm::abs(q) - p.p2;

            glm::vec2 local;
            float distance;
            if (e.x > 0.0f || e.y > 0.0f) {
                const glm::vec2 outside = glm::max(e, glm::vec2(0.0f));
                distance = glm::length(outside);
                local = outside * glm::vec2(signOrOne(q.x), signOrOne(q.y)) / std::max(distance, kEpsilon);
            } else {
                distance = std::max(e.x, e.y);
                local = e.x > e.y ? glm::vec2(signOrOne(q.x), 0.0f) : glm::vec2(0.0f, signOrOne(q.y));
            }
            normal = p.p1 * local.x + axisY * local.y;
            return distance;
        }
        case WallPrimitive::Circle: {
            const glm::vec2 d = point - p.p0;
            const float length = glm::length(d);
            normal = length > kEpsilon ? d / length : glm::vec2(0.0f, -1.0f);
            return length - p.radius;
        }
        case WallPrimitive::Capsule: {
            const glm::vec2 d = point - closestOnSegment(point, p.p0, p.p1);
            const float length = glm::length(d);
            if (length > kEpsilon) {
                normal = d / length;
            } else {
                const glm::vec2 along = p.p1 - p.p0;
                normal = glm::length(along) > kEpsilon ? glm::normalize(glm::vec2(-along.y, along.x))
                                                       : glm::vec2(0.0f, -1.0f);
            }
            return length - p.radius;
        }
        default: {
            const glm::vec2 corners[3] = {p.p0, p.p1, p.p2};
            float bestSq = std::numeric_limits<float>::max();
            glm::vec2 best = p.p0;
            int sideSigns = 0;
            for (int i = 0; i < 3; ++i) {
                const glm::vec2 a = corners[i];
                const glm::vec2 b = corners[(i + 1) % 3];
                const glm::vec2 c = closestOnSegment(point, a, b);
                const float distSq = glm::dot(point - c, point - c);
                if (distSq < bestSq) {
                    bestSq = distSq;
                    best = c;
                }
                sideSigns += cross(b - a, point - a) >= 0.0f ? 1 : -1;
            }
            const bool inside = sideSigns == 3 || sideSigns == -3;
            const float length = std::sqrt(bestSq);
            if (length > kEpsilon) {
                normal = (inside ? best - point : point - best) / length;
            } else {
                normal = glm::vec2(0.0f, -1.0f);
            }
            return inside ? -length : length;
        }
    }
}

void WallColliders::build(const logic::Level& level, float reach) {
    m_primitives.clear();
    for (const logic::Wall& wall : level.walls) {
        appendWall(wall, m_primitives);
    }

    // Bins a few unit-widths across: small enough that a unit tests only the
    // walls near it, big enough that a wall lands in few bins.
    m_binSize = std::max(32.0f, reach * 8.0f);
    m_binDim = glm::uvec2(static_cast<std::uint32_t>(std::max(1.0f, std::ceil(level.size.x / m_binSize))),
                          static_cast<std::uint32_t>(std::max(1.0f, std::ceil(level.size.y / m_binSize))));

    const auto binRange = [&](const WallPrimitive& p, glm::ivec2& lo, glm::ivec2& hi) {
        glm::vec2 a;
        glm::vec2 b;
        primitiveBounds(p, a, b);
        a -= glm::vec2(reach + 1.0f);
        b += glm::vec2(reach + 1.0f);
        const glm::ivec2 maxBin = glm::ivec2(m_binDim) - 1;
        lo = glm::clamp(glm::ivec2(glm::floor(a / m_binSize)), glm::ivec2(0), maxBin);
        hi = glm::clamp(glm::ivec2(glm::floor(b / m_binSize)), glm::ivec2(0), maxBin);
    };

    // Counting sort: count per bin, prefix-sum into offsets, then fill.
    m_bins.assign(static_cast<std::size_t>(m_binDim.x) * m_binDim.y, glm::uvec2(0));
    for (const WallPrimitive& p : m_primitives) {
        glm::ivec2 lo;
        glm::ivec2 hi;
        binRange(p, lo, hi);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                ++m_bins[x + y * m_binDim.x].y;
            }
        }
    }

    std::uint32_t offset = 0;
    for (glm::uvec2& bin : m_bins) {
        bin.x = offset;
        offset += bin.y;
        bin.y = 0;
    }

    m_binIndices.assign(offset, 0);
    for (std::uint32_t i = 0; i < m_primitives.size(); ++i) {
        glm::ivec2 lo;
        glm::ivec2 hi;
        binRange(m_primitives[i], lo, hi);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                glm::uvec2& bin = m_bins[x + y * m_binDim.x];
                m_binIndices[bin.x + bin.y++] = i;
            }
        }
    }
}

bool WallColliders::nearest(glm::vec2 point, float& distance, glm::vec2& normal) const {
    if (m_primitives.empty()) {
        return false;
    }

    const glm::ivec2 cell = glm::clamp(glm::ivec2(glm::floor(point / m_binSize)), glm::ivec2(0),
                                       glm::ivec2(m_binDim) - 1);
    const glm::uvec2 bin = m_bins[cell.x + cell.y * m_binDim.x];

    bool found = false;
    for (std::uint32_t i = 0; i < bin.y; ++i) {
        glm::vec2 n;
        const float d = primitiveDistance(m_primitives[m_binIndices[bin.x + i]], point, n);
        if (!found || d < distance) {
            distance = d;
            normal = n;
            found = true;
        }
    }
    return found;
}

glm::vec2 WallColliders::resolve(glm::vec2 centre, float radius) const {
    if (m_primitives.empty()) {
        return centre;
    }

    const glm::ivec2 cell = glm::clamp(glm::ivec2(glm::floor(centre / m_binSize)), glm::ivec2(0),
                                       glm::ivec2(m_binDim) - 1);
    const glm::uvec2 bin = m_bins[cell.x + cell.y * m_binDim.x];

    for (std::uint32_t i = 0; i < bin.y; ++i) {
        glm::vec2 n;
        const float d = primitiveDistance(m_primitives[m_binIndices[bin.x + i]], centre, n);
        if (d < radius) {
            centre += n * (radius - d);
        }
    }
    return centre;
}

} // namespace horde::sim
