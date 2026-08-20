//
// Created by Bailen on 13/08/2026.
//
#pragma once

#include <vector>
// TODO: test this
#define GLM_FORCE_INLINE
#include <glm/glm.hpp>

// TODO: test glm::vec2 vs custom 2vector implementation
// glm has obvious benefits of including many functions but it may be bloated for what we're using it for, testing
// needed

// TODO: compare data structure methods and their performance at scale. Metric: how many units can be simulated until i
// can no longer get 240fps. Structure of Arrays, class containing arrays where index is the unit "ID". Array of
// Structures, singular Unit structure contains all the info for an individual unit, then stored in a list.

// SoA based approach
class UnitManager {
private:
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;

    std::vector<int> health;
    std::vector<float> invMass;

    float unitSize;
    float cellSize;
    float invCellSize = 1.f / cellSize;

    size_t currentUnits = 0;
    size_t maxUnits;

    // World bounds
    const glm::vec2 worldOrigin = {0, 0};
    glm::vec2 worldBounds;
    glm::vec2 maxP{};
    float restitution = -1.f;

    // used in Uniform Grid for collisions.
    std::vector<int> cellHeads;
    std::vector<int> nextUnit;
    std::vector<int> activeCells;
    std::vector<int> edgeUnits;
    int gridCols = 0;
    int gridRows = 0;
    int maxCol = 0;
    int maxRow = 0;

public:
    // Constructor
    explicit UnitManager(size_t MaxUnits, glm::vec2 WorldBounds, float unit_size);

    void Reserve(size_t newMaxUnits);

    void SetWorldBounds(glm::vec2 WorldBounds);

    void BuildUniformGrid();

    void PopulateUniformGrid();

    void SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM = 0.01f);

    // Main Loop
    void UpdatePhysics(float dt);

    void UpdatePositions(float dt);

    inline void ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const;

    void ResolveEntityCollisions(float dt);

    // Getters and Setters
    [[nodiscard]] size_t GetCurrentUnits() const;

    [[nodiscard]] size_t GetMaxUnits() const;

    [[nodiscard]] glm::vec2 GetPosition(size_t index) const;

    [[nodiscard]] const glm::vec2* GetPositionsPtr() const;
};
