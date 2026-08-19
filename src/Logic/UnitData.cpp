//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

#include <algorithm>
#include <cmath>
#include <execution>
#include <set>

#include "SDL3/SDL_log.h"

UnitManager::UnitManager(const size_t MaxUnits, const glm::vec2 WorldBounds, const int unit_size)
    : unitSize(unit_size), cellSize(unit_size), maxUnits(MaxUnits), worldBounds(WorldBounds) {
    Reserve(MaxUnits);
    SetWorldBounds(WorldBounds);
    BuildUniformGrid();
}

void UnitManager::Reserve(size_t newMaxUnits) {
    maxUnits = newMaxUnits;
    positions.resize(newMaxUnits);
    velocities.resize(newMaxUnits);
    health.resize(newMaxUnits);
    invMass.resize(newMaxUnits);
    nextUnit.resize(newMaxUnits, -1);
}

// Consider removing input to this function and have entire object be recreated to change.
void UnitManager::SetWorldBounds(const glm::vec2 WorldBounds) {
    const glm::vec2 r(unitSize);
    maxP = WorldBounds - r;
}

void UnitManager::BuildUniformGrid() {
    gridCols = static_cast<int>(std::ceil(worldBounds.x * invCellSize));
    gridRows = static_cast<int>(std::ceil(worldBounds.y * invCellSize));
    maxCol = gridCols - 1;
    maxRow = gridRows - 1;
    cellHeads.assign(gridCols * gridRows, -1);
    activeCells.clear();
}

void UnitManager::PopulateUniformGrid() {
    std::fill(cellHeads.begin(), cellHeads.end(), -1);
    activeCells.clear();
    for (int i = 0; i < (int)currentUnits; ++i) {
        const glm::vec2 pos = positions[i];
        const int cellX = std::clamp((int)(pos.x * invCellSize), 0, maxCol);
        const int cellY = std::clamp((int)(pos.y * invCellSize), 0, maxRow);
        const int cellIndex = cellX + cellY * gridCols;

        nextUnit[i] = cellHeads[cellIndex];
        cellHeads[cellIndex] = i;
    }
}

void UnitManager::SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM) {
    if (currentUnits >= maxUnits) {
        return;
    }
    positions[currentUnits] = pos;
    velocities[currentUnits] = vel;
    health[currentUnits] = hp;
    invMass[currentUnits] = invM;

    currentUnits++;
}

// Main Loop
void UnitManager::UpdatePhysics(float dt) {
    UpdatePositions(dt);
    PopulateUniformGrid();
    ResolveCollisions();
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < currentUnits; ++i) {
        positions[i] += velocities[i] * dt;
    }
}

void UnitManager::ResolveCollisions() {
    ResolveEdgeCollisions();
}

void UnitManager::ResolveEdgeCollisions() {
    auto calculate = [&](int cellX, int cellY) {
        int id = cellY * gridCols + cellX;

        for (int unitId = cellHeads[id]; unitId != -1; unitId = nextUnit[unitId]) {
            glm::vec2& pos = positions[unitId];
            glm::vec2& vel = velocities[unitId];
            if ((pos.x > maxP.x && vel.x > 0.0f) || (pos.x < worldOrigin.x && vel.x < 0.0f))
                vel.x *= restitution;
            if ((pos.y > maxP.y && vel.y > 0.0f) || (pos.y < worldOrigin.y && vel.y < 0.0f))
                vel.y *= restitution;
            pos.x = std::clamp(pos.x, 0.0f, maxP.x);
            pos.y = std::clamp(pos.y, 0.0f, maxP.y);
        }
    };
    for (int cellX = 0; cellX < gridCols; ++cellX) {
        calculate(cellX, 0);
        calculate(cellX, maxRow);
    }
    for (int cellY = 1; cellY < gridRows; ++cellY) {
        calculate(0, cellY);
        calculate(maxCol, cellY);
    }
}

size_t UnitManager::GetCurrentUnits() const {
    return currentUnits;
}

size_t UnitManager::GetMaxUnits() const {
    return maxUnits;
}

glm::vec2 UnitManager::GetPosition(size_t index) const {
    return positions[index];
}

const glm::vec2* UnitManager::GetPositionsPtr() const {
    return positions.data();
}
