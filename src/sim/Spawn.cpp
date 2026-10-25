#include <algorithm>
#include <cmath>
#include <random>

#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

bool parseSpawnPattern(const std::string& text, SpawnPattern& out) {
    if (text == "column") {
        out = SpawnPattern::Column;
    } else if (text == "random") {
        out = SpawnPattern::Random;
    } else if (text == "grid") {
        out = SpawnPattern::Grid;
    } else {
        return false;
    }
    return true;
}

const char* spawnPatternName(SpawnPattern pattern) {
    switch (pattern) {
        case SpawnPattern::Column:
            return "column";
        case SpawnPattern::Random:
            return "random";
        case SpawnPattern::Grid:
            return "grid";
    }
    return "?";
}

std::vector<BaseUnitData> spawnUnits(SpawnPattern pattern, std::uint32_t count, glm::vec2 worldSize, float unitSize,
                                     std::uint32_t seed) {
    std::vector<BaseUnitData> units(count);
    const glm::vec2 maxP = glm::max(worldSize - glm::vec2(unitSize), glm::vec2(0.0f));

    switch (pattern) {
        case SpawnPattern::Column: {
            const int height = std::max(1, static_cast<int>(worldSize.y));
            for (std::uint32_t i = 0; i < count; ++i) {
                const int n = static_cast<int>(i);
                units[i].pos = glm::vec2(n * 25 / height + worldSize.x * 0.5f, n * 5 % height);
            }
            break;
        }
        case SpawnPattern::Random: {
            std::mt19937 rng(seed);
            std::uniform_real_distribution<float> x(0.0f, maxP.x);
            std::uniform_real_distribution<float> y(0.0f, maxP.y);
            for (BaseUnitData& unit : units) {
                unit.pos = glm::vec2(x(rng), y(rng));
            }
            break;
        }
        case SpawnPattern::Grid: {
            // Square-ish lattice with the level's aspect ratio.
            const float aspect = worldSize.x / std::max(worldSize.y, 1.0f);
            const auto cols = static_cast<std::uint32_t>(
                std::max(1.0, std::ceil(std::sqrt(static_cast<double>(count) * aspect))));
            const std::uint32_t rows = std::max(1u, (count + cols - 1) / cols);
            const glm::vec2 step = maxP / glm::vec2(static_cast<float>(cols), static_cast<float>(rows));
            for (std::uint32_t i = 0; i < count; ++i) {
                units[i].pos = glm::vec2(static_cast<float>(i % cols), static_cast<float>(i / cols)) * step;
            }
            break;
        }
    }

    return units;
}

} // namespace horde::sim
