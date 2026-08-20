//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>

#include "SDL3/SDL_log.h"

// Constructor
UnitManager::UnitManager(const size_t MaxUnits, const glm::vec2 WorldBounds, const float unit_size)
    : unitSize(unit_size), cellSize(unit_size), maxUnits(MaxUnits), worldBounds(WorldBounds) {
    // does this once at startup
    Reserve(MaxUnits);
    maxP = worldBounds - glm::vec2(unitSize);
    invCellSize = 1.0f / cellSize;
    BuildUniformGrid();
}

void UnitManager::Reserve(const size_t newMaxUnits) {
    maxUnits = newMaxUnits;
    positions.resize(newMaxUnits);
    velocities.resize(newMaxUnits);
    health.resize(newMaxUnits);
    invMass.resize(newMaxUnits);
    nextUnit.resize(newMaxUnits, -1);
}

void UnitManager::BuildUniformGrid() {
    gridCols = static_cast<int>(std::ceil(worldBounds.x * invCellSize));
    gridRows = static_cast<int>(std::ceil(worldBounds.y * invCellSize));
    SDL_Log("%f %d %d", invCellSize, gridCols, gridRows);
    maxCol = gridCols - 1;
    maxRow = gridRows - 1;
    cellHeads.assign(gridCols * gridRows, -1);
    activeCells.resize(gridCols * gridRows);
}

void UnitManager::PopulateUniformGrid() {
    std::ranges::fill(cellHeads, -1);
    activeCells.clear();
    for (int i = 0; i < static_cast<int>(currentUnits); ++i) {
        glm::vec2& pos = positions[i];
        const int cellX = std::clamp(static_cast<int>(pos.x * invCellSize), 0, maxCol);
        const int cellY = std::clamp(static_cast<int>(pos.y * invCellSize), 0, maxRow);
        const int cellIndex = cellX + cellY * gridCols;
        if (cellX == 0 || cellX == maxCol || cellY == 0 || cellY == maxRow) {
            ResolveEdgeCollisions(pos, velocities[i]);
        }
        if (cellHeads[cellIndex] == -1) {
            activeCells.push_back(cellIndex);
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
    int steps = 4;
    for (int step = 0; step < steps; ++step) {
        UpdatePositions(dt / steps);
        PopulateUniformGrid();
        ResolveEntityCollisions(dt);
    }
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < currentUnits; ++i) {

        velocities[i].y += 10 * dt;
        velocities[i] = clamp(velocities[i], 0.0f, 25.0f);
        positions[i] += velocities[i] * dt;
    }
}

inline void UnitManager::ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const {
    if (pos.x > maxP.x || pos.x < worldOrigin.x) {
        // vel.x = 0;
        vel.x *= restitution;
        // vel.y *= 0.5f;
    }
    if (pos.y > maxP.y || pos.y < worldOrigin.y) {
        // vel.y = 0;
        vel.y *= restitution;
        // vel.x *= 0.5f;
    }
    // pos = glm::clamp(pos, worldOrigin, maxP);
    pos.x = std::clamp(pos.x, worldOrigin.x, maxP.x);
    pos.y = std::clamp(pos.y, worldOrigin.y, maxP.y);
}

void UnitManager::getNeighbours(const int cellIndex, std::vector<int>& neighbours) const {
    neighbours.clear();
    int cellX = cellIndex % gridCols;
    int cellY = cellIndex / gridCols;

    const int minX = std::max(0, cellX - 1);
    const int maxX = std::min(maxCol, cellX + 1);
    const int minY = std::max(0, cellY - 1);
    const int maxY = std::min(maxRow, cellY + 1);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            for (int u = cellHeads[x + y * gridCols]; u != -1; u = nextUnit[u]) {
                neighbours.push_back(u);
            }
        }
    }
};

void UnitManager::ResolveEntityCollisions(float dt) {
    // TODO: fix, doesnt quite work yet.
    const float desiredDistSquared = unitSize * unitSize;
    std::vector<int> neighbours;
    neighbours.reserve(64);

    int solverIterations = 6;
    for (int iter = 0; iter < solverIterations; ++iter) {
        for (int cellIndex : activeCells) {
            getNeighbours(cellIndex, neighbours);

            for (int unit = cellHeads[cellIndex]; unit != -1; unit = nextUnit[unit]) {
                glm::vec2& pos = positions[unit];
                for (const int i : neighbours) {
                    if (i <= unit) {
                        continue;
                    }
                    health[i] = 10;
                    glm::vec2& ipos = positions[i];
                    float dx = ipos.x - pos.x;
                    if (std::abs(dx) <= unitSize) {
                        float dy = ipos.y - pos.y;
                        if (std::abs(dy) <= unitSize) {
                            float distSquared = dx * dx + dy * dy;
                            if (distSquared < desiredDistSquared && distSquared > 0.0001f) {
                                health[unit] = 2;
                                health[i] = 2;
                                float dist = std::sqrt(distSquared);
                                glm::vec2 normal = {dx / dist, dy / dist};
                                float overlap = 0.5f * (unitSize - dist);
                                pos -= normal * overlap;
                                ipos += normal * overlap;
                                pos = glm::clamp(pos, worldOrigin, maxP);
                                ipos = glm::clamp(ipos, worldOrigin, maxP);

                                glm::vec2 relativeVelocity = velocities[i] - velocities[unit];
                                float normalVelocity = glm::dot(relativeVelocity, normal);
                                if (normalVelocity < 0.0f) {
                                    float impulse = -(1.0f - restitution) * normalVelocity * 0.5f;
                                    velocities[unit] -= normal * impulse;
                                    velocities[i] += normal * impulse;
                                }
                            }
                        }
                    }
                }

                // glm::vec2 rpos = ipos - pos;
                // glm::vec2 rvel = ivel-vel;
                // glm::vec2 normal = normalize(rpos);
                // float distance = length(rpos);
                // float normalVel = dot(normal, rvel);
                // if (distance <= unitSize) {
                //     pos -= normal * (unitSize - distance);
                //     ipos += normal * (unitSize - distance);
                //     // vel -= normalVel*invMass[unit];
                //     // ivel += normalVel*invMass[unit];
                // }
            }

        }
    }
}

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

const int* UnitManager::GetHealthsPtr() const {
    return health.data();
}