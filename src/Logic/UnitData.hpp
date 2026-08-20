#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "Logic/BoundingBox.hpp"
#include "Logic/VectorField.hpp"
#include "core/ThreadPool.hpp"

struct UnitManagerProfile {
    float updatePosTimeMs = 0.0f;
    float gridBuildTimeMs = 0.0f;
    float collisionTimeMs = 0.0f;
    float boundaryTimeMs = 0.0f;
    float totalPhysicsTimeMs = 0.0f;
    size_t activeCellCount = 0;
    size_t collisionPairsChecked = 0;
    float totalKineticEnergy = 0.0f;
    size_t threadCount = 1;
    size_t crammedUnitsKilled = 0;
};

// SoA based approach
class UnitManager {
private:
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;
    std::vector<float> sizes; // Circular radius for each unit

    std::vector<int> health;
    std::vector<float> invMass; // a*b more efficient than a/b
    std::vector<int> cramCount; // Overlap/cramming counter per unit

    size_t CurrentUnits = 0;
    size_t MaxUnits;

    // Entity cramming settings
    bool enableCramming = true;
    int maxCrammingLimit = 12; // Maximum simultaneous overlaps before taking cramming damage
    int cramDamage = 10;       // Damage applied when cramming threshold is exceeded

    // Vector field settings
    VectorField vectorField;
    bool enableVectorField = true;
    float vectorFieldSpeed = 50.0f;
    float vectorFieldSteeringStrength = 3.0f;
    float maxSpeed = 75.0f;

    // World boundaries for collision & containment
    BoundingBox worldBounds{glm::vec2(0.0f, 0.0f), glm::vec2(6000.0f, 4000.0f)};

    // Spatial partitioning uniform grid for collision broad-phase
    float cellSize = 32.0f;
    int gridCols = 0;
    int gridRows = 0;
    std::vector<int> CellHeads;
    std::vector<int> NextUnit;
    std::vector<int> activeCells;
    std::array<std::vector<int>, 6> activeCellsByColor;

    horde::core::ThreadPool* threadPool = nullptr;
    UnitManagerProfile profile;

public:
    // Constructor
    explicit UnitManager(size_t MaxUnits, horde::core::ThreadPool* pool = &horde::core::ThreadPool::defaultPool());

    // Main loop
    void UpdatePhysics(float dt);

    void UpdatePositions(float dt);

    void BuildSpatialGrid();

    void ResolveEntityCollisions();

    void ResolveBoundaryCollisions();

    void ResolveCrammingAndDeaths();

    void SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM = 0.01f, float size = 5.0f);

    void ClearUnits();

    void Reserve(size_t newMaxUnits);

    void SetWorldBounds(const BoundingBox& bounds);

    void SetCellSize(float size);

    void SetThreadPool(horde::core::ThreadPool* pool) {
        threadPool = pool;
    }

    [[nodiscard]] horde::core::ThreadPool* GetThreadPool() const {
        return threadPool;
    }

    [[nodiscard]] float GetCellSize() const {
        return cellSize;
    }

    [[nodiscard]] const BoundingBox& GetWorldBounds() const;

    [[nodiscard]] BoundingBox GetBoundingBox(size_t index) const;

    [[nodiscard]] size_t GetCurrentUnits() const;

    [[nodiscard]] size_t GetMaxUnits() const;

    [[nodiscard]] glm::vec2 GetPosition(size_t index) const;

    [[nodiscard]] const glm::vec2* GetPositionsPtr() const;

    [[nodiscard]] float GetSize(size_t index) const;

    [[nodiscard]] const float* GetSizesPtr() const;

    [[nodiscard]] float CalculateTotalKineticEnergy() const;

    [[nodiscard]] const UnitManagerProfile& GetProfile() const;

    // Entity Cramming settings
    void SetCrammingEnabled(bool enabled) {
        enableCramming = enabled;
    }

    [[nodiscard]] bool IsCrammingEnabled() const {
        return enableCramming;
    }

    void SetMaxCrammingLimit(int limit) {
        maxCrammingLimit = std::max(1, limit);
    }

    [[nodiscard]] int GetMaxCrammingLimit() const {
        return maxCrammingLimit;
    }

    void SetCramDamage(int damage) {
        cramDamage = std::max(1, damage);
    }

    [[nodiscard]] int GetCramDamage() const {
        return cramDamage;
    }

    // Vector Field settings & access
    [[nodiscard]] VectorField& GetVectorField() {
        return vectorField;
    }

    [[nodiscard]] const VectorField& GetVectorField() const {
        return vectorField;
    }

    void SetVectorFieldEnabled(bool enabled) {
        enableVectorField = enabled;
    }

    [[nodiscard]] bool IsVectorFieldEnabled() const {
        return enableVectorField;
    }

    void SetVectorFieldSpeed(float speed) {
        vectorFieldSpeed = std::max(0.0f, speed);
    }

    [[nodiscard]] float GetVectorFieldSpeed() const {
        return vectorFieldSpeed;
    }

    void SetVectorFieldSteeringStrength(float strength) {
        vectorFieldSteeringStrength = std::max(0.0f, strength);
    }

    [[nodiscard]] float GetVectorFieldSteeringStrength() const {
        return vectorFieldSteeringStrength;
    }

    void SetMaxSpeed(float maxSpd) {
        maxSpeed = std::max(1.0f, maxSpd);
    }

    [[nodiscard]] float GetMaxSpeed() const {
        return maxSpeed;
    }
};