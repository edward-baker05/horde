//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

#include <algorithm>
#include <cmath>
#include <execution>
#include <set>

#include "SDL3/SDL_log.h"

// Constructor
UnitManager::UnitManager(const size_t MaxUnits, const glm::vec2 WorldBounds, const int unit_size)
    : unitSize(unit_size), cellSize(unit_size), maxUnits(MaxUnits), worldBounds(WorldBounds) {
    // does this once at startup
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
    nextUnit.resize(newMaxUnits);
    edgeUnits.resize(newMaxUnits);
    activeCells.resize(newMaxUnits);
}

// Consider removing input to this function and have entire object be recreated to change.
void UnitManager::SetWorldBounds(const glm::vec2 WorldBounds) {
    const glm::vec2 r(unitSize);
    maxP = WorldBounds - r;
}

void UnitManager::BuildUniformGrid() {
    gridCols = std::max(1, static_cast<int>(std::ceil(worldBounds.x * invCellSize)));
    gridRows = std::max(1, static_cast<int>(std::ceil(worldBounds.y * invCellSize)));
    maxCol = gridCols - 1;
    maxRow = gridRows - 1;
    cellHeads.assign(gridCols*gridRows, -1);
    activeCells.clear();
}

// void UnitManager::PopulateUniformGrid() {
//     for (int i : activeCells) {
//         cellHeads[i] = -1;
//     }
//     activeCells.clear();
//     edgeUnits.clear();
//     for (int i = 0; i < currentUnits; ++i) {
//         const glm::vec2& pos = positions[i];
//         const int cellX = std::clamp(static_cast<int>(pos.x * invCellSize), 0, maxCol);
//         const int cellY = std::clamp(static_cast<int>(pos.y * invCellSize), 0, maxRow);
//         const int cellIndex = cellX + cellY * gridCols;
//         if (cellX == 0 || cellX == maxCol || cellY == 0 || cellY == maxRow)
//             edgeUnits.push_back(i);
//         int& cellHead = cellHeads[cellIndex];
//         if (cellHead == -1) {
//             activeCells.push_back(cellIndex);
//         }
//         nextUnit[i] = cellHead;
//         cellHead = i;
//     }
// }

void UnitManager::PopulateUniformGrid() {
    for (int i : activeCells) {
        cellHeads[i] = -1;
    }
    activeCells.clear();
    int j = 0;
    for (int i = 0; i < currentUnits; ++i) {
        const glm::vec2& pos = positions[i];
        const int cellX = std::clamp(static_cast<int>(pos.x * invCellSize), 0, maxCol);
        const int cellY = std::clamp(static_cast<int>(pos.y * invCellSize), 0, maxRow);
        const int cellIndex = cellX + cellY * gridCols;
        if (cellX == 0 || cellX == maxCol || cellY == 0 || cellY == maxRow) {
            edgeUnits[j] = i;
            j++;
        }
        int& cellHead = cellHeads[cellIndex];
        if (cellHead == -1) {
            activeCells.push_back(cellIndex);
        }
        nextUnit[i] = cellHead;
        cellHead = i;
    }
    edgeUnits[j] = -1;
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
    for (int i : edgeUnits) {
        if (i == -1) {
            break;
        }
        glm::vec2& pos = positions[i];
        glm::vec2& vel = velocities[i];
        if ((pos.x > maxP.x && vel.x > 0.0f) || (pos.x < worldOrigin.x && vel.x < 0.0f))
            vel.x *= restitution;
        if ((pos.y > maxP.y && vel.y > 0.0f) || (pos.y < worldOrigin.y && vel.y < 0.0f))
            vel.y *= restitution;
        pos.x = std::clamp(pos.x, 0.0f, maxP.x);
        pos.y = std::clamp(pos.y, 0.0f, maxP.y);
    }
}

// void UnitManager::ResolveEdgeCollisions() {
//         for (int i : edgeUnits) {
//             glm::vec2& pos = positions[i];
//             glm::vec2& vel = velocities[i];
//             if ((pos.x > maxP.x && vel.x > 0.0f) || (pos.x < worldOrigin.x && vel.x < 0.0f))
//                 vel.x *= restitution;
//             if ((pos.y > maxP.y && vel.y > 0.0f) || (pos.y < worldOrigin.y && vel.y < 0.0f))
//                 vel.y *= restitution;
//             pos.x = std::clamp(pos.x, 0.0f, maxP.x);
//             pos.y = std::clamp(pos.y, 0.0f, maxP.y);
//         }
// }

// Getters and Setters
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