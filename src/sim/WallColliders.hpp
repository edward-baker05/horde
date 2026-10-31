#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "logic/Level.hpp"

namespace horde::sim {

// One convex piece of a wall, in world space. Matches WallPrimitive in
// shaders/src/walls.hlsli: 32 bytes, read straight from a structured buffer.
//
//   Box       p0 = centre, p1 = unit x axis (cos, sin of rotation), p2 = half extents
//   Circle    p0 = centre, radius
//   Triangle  p0, p1, p2 = corners
struct WallPrimitive {
    enum Kind : std::uint32_t { Box = 0, Circle = 1, Triangle = 3 };

    glm::vec2 p0{0.0f};
    glm::vec2 p1{0.0f};
    glm::vec2 p2{0.0f};
    std::uint32_t kind = Box;
    float radius = 0.0f;
};
static_assert(sizeof(WallPrimitive) == 32);

// A level's walls, flattened into primitives and binned into a coarse uniform
// grid, so a unit only tests the few walls near it whatever the level holds.
// Walls don't move, so this is built once when a level starts, on the CPU.
//
// The same data drives the GPU (GpuWalls uploads it) and CPU-side checks such
// as the benchmark's "units inside walls" count, so both agree on the shapes.
class WallColliders {
public:
    // `reach` is the furthest from a wall anything will ask about: a unit's
    // radius. Primitives are binned into every cell within `reach` of them.
    void build(const logic::Level& level, float reach);

    bool empty() const {
        return m_primitives.empty();
    }

    // Signed distance from `point` to the nearest wall surface (negative
    // inside), and the outward direction there. Returns false, leaving the
    // outputs alone, when no wall is within `reach`.
    bool nearest(glm::vec2 point, float& distance, glm::vec2& normal) const;

    // Pushes a disc of `radius` centred at `centre` out of every wall it
    // overlaps, as the GPU does.
    glm::vec2 resolve(glm::vec2 centre, float radius) const;

    const std::vector<WallPrimitive>& primitives() const {
        return m_primitives;
    }

    // Per bin: (first index into binIndices(), count).
    const std::vector<glm::uvec2>& bins() const {
        return m_bins;
    }

    const std::vector<std::uint32_t>& binIndices() const {
        return m_binIndices;
    }

    glm::uvec2 binDim() const {
        return m_binDim;
    }

    float binSize() const {
        return m_binSize;
    }

private:
    std::vector<WallPrimitive> m_primitives;
    std::vector<glm::uvec2> m_bins;
    std::vector<std::uint32_t> m_binIndices;
    glm::uvec2 m_binDim{1, 1};
    float m_binSize = 32.0f;
};

// Signed distance from `point` to one primitive, with the outward normal.
// Mirrors wallDistance in walls.hlsli.
float primitiveDistance(const WallPrimitive& primitive, glm::vec2 point, glm::vec2& normal);

} // namespace horde::sim
