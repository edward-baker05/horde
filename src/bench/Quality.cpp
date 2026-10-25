#include "bench/Quality.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace horde::bench {

CollisionQuality measureCollisions(const std::vector<BaseUnitData>& units, float diameter, glm::vec2 worldSize) {
    CollisionQuality q;
    q.measured = true;

    const auto count = static_cast<std::uint32_t>(units.size());
    const double area = static_cast<double>(worldSize.x) * worldSize.y;
    q.density = area > 0.0 ? count * std::numbers::pi * 0.25 * diameter * diameter / area : 0.0;
    if (count < 2 || diameter <= 0.0f) {
        return q;
    }

    // Counting sort of unit indices into one-diameter cells.
    const int cols = std::max(1, static_cast<int>(std::ceil(worldSize.x / diameter)));
    const int rows = std::max(1, static_cast<int>(std::ceil(worldSize.y / diameter)));
    const auto cellOf = [&](glm::vec2 p) {
        const int x = std::clamp(static_cast<int>(p.x / diameter), 0, cols - 1);
        const int y = std::clamp(static_cast<int>(p.y / diameter), 0, rows - 1);
        return x + y * cols;
    };

    std::vector<std::uint32_t> start(static_cast<std::size_t>(cols) * rows + 1, 0);
    for (const BaseUnitData& u : units) {
        ++start[cellOf(u.pos) + 1];
    }
    for (std::size_t i = 1; i < start.size(); ++i) {
        start[i] += start[i - 1];
    }
    std::vector<std::uint32_t> order(count);
    std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
    for (std::uint32_t i = 0; i < count; ++i) {
        order[fill[cellOf(units[i].pos)]++] = i;
    }

    const float tolerance = 0.01f * diameter;
    const float limitSq = (diameter - tolerance) * (diameter - tolerance);
    std::vector<bool> overlapping(count, false);
    double penetrationSum = 0.0;

    for (std::uint32_t i = 0; i < count; ++i) {
        const glm::vec2 p = units[i].pos;
        const int cx = std::clamp(static_cast<int>(p.x / diameter), 0, cols - 1);
        const int cy = std::clamp(static_cast<int>(p.y / diameter), 0, rows - 1);

        for (int y = std::max(0, cy - 1); y <= std::min(rows - 1, cy + 1); ++y) {
            for (int x = std::max(0, cx - 1); x <= std::min(cols - 1, cx + 1); ++x) {
                const int cell = x + y * cols;
                for (std::uint32_t s = start[cell]; s < start[cell + 1]; ++s) {
                    const std::uint32_t j = order[s];
                    if (j <= i) {
                        continue; // each pair once
                    }
                    const glm::vec2 d = units[j].pos - p;
                    const float distSq = d.x * d.x + d.y * d.y;
                    if (distSq >= limitSq) {
                        continue;
                    }
                    const double penetration = (diameter - std::sqrt(distSq)) / diameter;
                    ++q.overlappingPairs;
                    penetrationSum += penetration;
                    q.maxPenetration = std::max(q.maxPenetration, penetration);
                    overlapping[i] = true;
                    overlapping[j] = true;
                }
            }
        }
    }

    q.overlappingUnits = static_cast<std::uint32_t>(std::count(overlapping.begin(), overlapping.end(), true));
    q.meanPenetration = q.overlappingPairs ? penetrationSum / static_cast<double>(q.overlappingPairs) : 0.0;
    return q;
}

} // namespace horde::bench
