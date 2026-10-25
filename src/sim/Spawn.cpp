#include <algorithm>
#include <cmath>
#include <random>

#include "sim/PhysicsSystem.hpp"
#include "sim/WallColliders.hpp"

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

bool parseBroadphase(const std::string& text, Broadphase& out) {
    if (text == "bitonic") {
        out = Broadphase::BitonicSort;
    } else if (text == "counting") {
        out = Broadphase::CountingSort;
    } else if (text == "buckets") {
        out = Broadphase::Buckets;
    } else {
        return false;
    }
    return true;
}

const char* broadphaseName(Broadphase broadphase) {
    switch (broadphase) {
        case Broadphase::BitonicSort:
            return "bitonic";
        case Broadphase::CountingSort:
            return "counting";
        case Broadphase::Buckets:
            return "buckets";
    }
    return "?";
}

bool parseSolver(const std::string& text, CollisionSolver& out) {
    if (text == "relaxed") {
        out = CollisionSolver::Relaxed;
    } else if (text == "average") {
        out = CollisionSolver::Average;
    } else {
        return false;
    }
    return true;
}

const char* solverName(CollisionSolver solver) {
    switch (solver) {
        case CollisionSolver::Relaxed:
            return "relaxed";
        case CollisionSolver::Average:
            return "average";
    }
    return "?";
}

bool collisionPreset(const std::string& name, CollisionSettings& out) {
    // Passes are the main cost knob: each is a full read of every unit's
    // neighbourhood. The broadphase is cheap with counting sort at any scale.
    CollisionSettings preset;
    if (name == "low") {
        preset.iterations = 1;
        preset.relaxation = 0.8f;
        preset.broadphase = Broadphase::Buckets;
        preset.bucketCapacity = 6;
    } else if (name == "medium") {
        preset.iterations = 2;
        preset.relaxation = 0.7f;
        preset.broadphase = Broadphase::Buckets;
    } else if (name == "high") {
        preset.iterations = 4;
        preset.relaxation = 0.5f;
        preset.broadphase = Broadphase::CountingSort;
    } else if (name == "ultra") {
        preset.iterations = 8;
        preset.relaxation = 0.5f;
        preset.broadphase = Broadphase::CountingSort;
    } else {
        return false;
    }
    out = preset;
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
                                     std::uint32_t seed, const WallColliders* walls) {
    std::vector<BaseUnitData> units(count);
    const glm::vec2 maxP = glm::max(worldSize - glm::vec2(unitSize), glm::vec2(0.0f));
    const float radius = unitSize * 0.5f;
    const bool hasWalls = walls != nullptr && !walls->empty();

    // Whether a unit at top-left `pos` would overlap a wall.
    const auto blocked = [&](glm::vec2 pos) {
        float distance = 0.0f;
        glm::vec2 normal;
        return hasWalls && walls->nearest(pos + radius, distance, normal) && distance < radius;
    };

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
                // A few retries is plenty unless walls cover most of the level;
                // whatever is still blocked is pushed out below.
                unit.pos = glm::vec2(x(rng), y(rng));
                for (int attempt = 0; attempt < 16 && blocked(unit.pos); ++attempt) {
                    unit.pos = glm::vec2(x(rng), y(rng));
                }
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

    if (hasWalls) {
        for (BaseUnitData& unit : units) {
            if (blocked(unit.pos)) {
                // Twice: a push out of one wall can land in a neighbour.
                for (int pass = 0; pass < 2; ++pass) {
                    unit.pos = glm::clamp(walls->resolve(unit.pos + radius, radius) - radius, glm::vec2(0.0f), maxP);
                }
            }
        }
    }

    return units;
}

} // namespace horde::sim
