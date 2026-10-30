//
// Created by Bailen on 13/08/2026.
//
#pragma once

#include <vector>
#define GLM_FORCE_INLINE
#include <glm/glm.hpp>
#include <SDL3/SDL_stdinc.h>

// Matches HLSL UnitData in unit.vert.hlsl and the compute shaders
struct BaseUnitData {
    glm::vec2 pos{0.0f, 0.0f};
    glm::vec2 vel{0.0f, 0.0f};
};

// SoA based approach
class UnitManager {
private:
    std::vector<BaseUnitData> baseUnits;
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;

    std::vector<int> health;
    std::vector<float> invMass;

    float unitSize;
    float cellSize;
    float invCellSize;

    size_t currentUnits = 0;
    size_t maxUnits{};

    // World bounds
    static constexpr glm::vec2 worldOrigin = {0.0f, 0.0f};
    glm::vec2 worldBounds{};
    glm::vec2 maxP{};
    float restitution = -0.1f;

    // used in Uniform Grid for collisions.
    std::vector<int> cellHeads;
    std::vector<int> nextUnit;
    std::vector<int> activeCells;
    std::vector<int> edgeUnits;
    int gridCols = 0;
    int gridRows = 0;
    int maxCol = 0;
    int maxRow = 0;

    void BuildUniformGrid();

    void PopulateUniformGrid();
    void UpdatePositions(float dt);

    inline void ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const;

    void ResolveEntityCollisions(float dt);

    void getNeighbours(const int cellIndex, std::vector<int>& neighbours) const;

public:
    // Constructor
    UnitManager() = default;
    UnitManager(size_t MaxUnits, glm::vec2 WorldBounds, float unit_size);

    void Reserve(size_t newMaxUnits);

    void SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM = 0.01f);

    // Main Loop
    void UpdatePhysics(float dt);

    // Getters and Setters
    [[nodiscard]] size_t GetCurrentUnits() const;

    [[nodiscard]] size_t GetMaxUnits() const;

    [[nodiscard]] glm::vec2 GetPosition(size_t index) const;

    [[nodiscard]] const glm::vec2* GetPositionsPtr() const;

    [[nodiscard]] const int* GetHealthsPtr() const;

    [[nodiscard]] const std::vector<BaseUnitData>& GetBaseUnits() const;

    [[nodiscard]] const BaseUnitData* GetBaseUnitsPtr() const;
};
