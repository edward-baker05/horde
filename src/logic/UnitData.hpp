//
// Created by Bailen on 13/08/2026.
//
#pragma once

#include <vector>
#define GLM_FORCE_INLINE
#include <glm/glm.hpp>
#include <SDL3/SDL_stdinc.h>

// Matches HLSL UnitData in update_positions.comp.hlsl and unit.vert.hlsl
struct BaseUnitData {
    glm::vec2 pos{0.0f, 0.0f};
    glm::vec2 vel{0.0f, 0.0f};
};

// Matches HLSL SimParams cbuffer in update_positions.comp.hlsl
struct alignas(16) GPUSimUniforms {
    glm::vec2 worldOrigin{0.0f, 0.0f};
    glm::vec2 worldBounds{0.0f, 0.0f};
    float dt = 0.0f;
    Uint32 unitCount = 0;
    float restitution = 0.8f;
    float gravity = 98.0f;
    float unitSize = 5.0f;
    float cellSize = 5.0f;
    glm::uvec2 gridDim{0, 0};
    glm::vec2 invCellSize{0.0f, 0.0f};
    glm::vec2 padding{0.0f, 0.0f};
};

// Matches HLSL ClearParams in grid_clear.comp.hlsl
struct alignas(16) GPUGridClearUniforms {
    Uint32 totalCells = 0;
    Uint32 padding[3] = {0, 0, 0};
};

// Matches HLSL GridBuildParams in grid_build.comp.hlsl
struct alignas(16) GPUGridBuildUniforms {
    glm::vec2 worldOrigin{0.0f, 0.0f};
    glm::vec2 invCellSize{0.0f, 0.0f};
    glm::uvec2 gridDim{0, 0};
    Uint32 unitCount = 0;
    Uint32 allocatedUnits = 0;
};

// Matches HLSL SortParams in bitonic_sort_step.comp.hlsl
struct alignas(16) GPUSortUniforms {
    Uint32 k = 0;
    Uint32 j = 0;
    Uint32 totalEntries = 0;
    Uint32 padding = 0;
};

// Matches HLSL RangeParams in grid_find_ranges.comp.hlsl
struct alignas(16) GPURangeUniforms {
    Uint32 unitCount = 0;
    Uint32 padding[3] = {0, 0, 0};
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
