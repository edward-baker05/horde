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
    std::vector<float> invMass; // a*b more efficient than a/b

    size_t CurrentUnits = 0;
    size_t MaxUnits;

    // used in Uniform Grid for collisions.
    std::vector<int> CellHeads;
    std::vector<int> NextUnit;

public:
    // Constructor
    explicit UnitManager(size_t MaxUnits);

    void Reserve(size_t newMaxUnits);

    // Main loop
    void UpdatePhysics(float dt);

    void UpdatePositions(float dt);

    void SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM = 0.01f);

    [[nodiscard]] size_t GetCurrentUnits() const;

    [[nodiscard]] size_t GetMaxUnits() const;

    [[nodiscard]] glm::vec2 GetPosition(size_t index) const;

    [[nodiscard]] const glm::vec2* GetPositionsPtr() const;
};