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
    // const glm::vec2 r(unitSize);
    maxP = WorldBounds - glm::vec2(unitSize);
}

void UnitManager::BuildUniformGrid() {
    gridCols = static_cast<int>(std::ceil(worldBounds.x * invCellSize));
    gridRows = static_cast<int>(std::ceil(worldBounds.y * invCellSize));
    maxCol = gridCols - 1;
    maxRow = gridRows - 1;
    cellHeads.assign(gridCols * gridRows, -1);
}

void UnitManager::PopulateUniformGrid() {
    std::ranges::fill(cellHeads, -1);
    for (int i = 0; i < static_cast<int>(currentUnits); ++i) {
        glm::vec2& pos = positions[i];
        const int cellX = std::clamp(static_cast<int>(pos.x * invCellSize), 0, maxCol);
        const int cellY = std::clamp(static_cast<int>(pos.y * invCellSize), 0, maxRow);
        const int cellIndex = cellX + cellY * gridCols;
        if (cellX == 0 || cellX == maxCol || cellY == 0 || cellY == maxRow) {
            glm::vec2& vel = velocities[i];
            if (pos.x > maxP.x || pos.x < worldOrigin.x)
                vel.x *= restitution;
            if (pos.y > maxP.y || pos.y < worldOrigin.y)
                vel.y *= restitution;
            pos.x = std::clamp(pos.x, 0.0f, maxP.x);
            pos.y = std::clamp(pos.y, 0.0f, maxP.y);
        }
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
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < currentUnits; ++i) {
        positions[i] += velocities[i] * dt;
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
