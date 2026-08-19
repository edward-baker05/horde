#pragma once

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace horde::logic {

// Fixed thickness of every marker, in world units. Markers stretch along their
// edge only, so this is the one dimension no one authors.
inline constexpr float kMarkerThickness = 12.0f;

// A colour with no alpha. Walls are opaque: see spec section 1.
struct Rgb {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
};

struct RectangleShape {
    glm::vec2 halfExtents{25.0f, 25.0f};
};

// Isoceles. The apex points toward -y in the wall's local space, which is
// upward on screen because world +y points down.
struct TriangleShape {
    glm::vec2 halfExtents{25.0f, 25.0f};
};

struct CircleShape {
    float radius = 25.0f;
};

// An ordered run of points joined by straight segments of uniform thickness.
// Points are in the wall's LOCAL space, relative to its centre, so that moving
// and rotating a polyline works exactly as it does for every other wall kind.
// The run need not be closed.
struct PolylineShape {
    std::vector<glm::vec2> points;
    float thickness = 6.0f;
};

using WallShape = std::variant<RectangleShape, TriangleShape, CircleShape, PolylineShape>;

// A shape units will collide with. Every placed shape is a wall.
//
// `rotation` is present but ignored for CircleShape. Keeping one transform for
// every kind is worth one unused float: see spec section 1.
struct Wall {
    glm::vec2 center{0.0f, 0.0f};
    float rotation = 0.0f; // radians; positive is clockwise on screen
    Rgb color{180, 180, 190};
    WallShape shape;
};

enum class Edge { North, South, East, West };
enum class MarkerKind { Spawn, Exit };

// A spawn or an exit: a thin band lying flat against one level edge.
//
// Stored edge-relative rather than positioned so that "cannot move toward the
// centre" is unrepresentable rather than merely validated. See
// docs/adr/0002-edge-relative-markers.md.
//
// `offset` is the distance along the edge from its start corner; `length` is
// how far it extends from there. Both are in world units.
struct Marker {
    MarkerKind kind = MarkerKind::Spawn;
    Edge edge = Edge::West;
    float offset = 0.0f;
    float length = 100.0f;
};

// A rectangular arena and everything placed in it.
//
// Bounds run from (0,0) to `size`. The background exactly fills those bounds
// and is the only element units do not collide with.
struct Level {
    glm::vec2 size{600.0f, 400.0f};
    Rgb backgroundColor{38, 42, 50};
    std::vector<Wall> walls;     // a later index draws on top of an earlier one
    std::vector<Marker> markers; // always at least one spawn and one exit
};

// Length of one edge of a level, in world units.
inline float edgeLength(const Level& level, Edge edge) {
    return (edge == Edge::North || edge == Edge::South) ? level.size.x : level.size.y;
}

// A blank level: default size, one spawn centred on the west edge, one exit
// centred on the east.
Level makeDefaultLevel();

// The axis-aligned world-space rectangle a marker occupies, as centre and size.
// Markers are drawn inward from their edge.
void markerRect(const Level& level, const Marker& marker, glm::vec2& outCenter, glm::vec2& outSize);

// How many markers of this kind the level has. The last spawn and the last exit
// cannot be deleted.
std::size_t countMarkers(const Level& level, MarkerKind kind);

} // namespace horde::logic
