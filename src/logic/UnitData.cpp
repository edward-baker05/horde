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
    invCellSize = 1.f / cellSize;
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
    UpdatePositions(dt);
    PopulateUniformGrid();
    ResolveEntityCollisions(dt);
    // ResolveEntityCollisions(dt*0.5f);
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < currentUnits; ++i) {
        velocities[i] = clamp(velocities[i], 0.0f, 10.0f);
        positions[i] += velocities[i] * dt;
    }
}

inline void UnitManager::ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const {
    if (pos.x > maxP.x || pos.x < worldOrigin.x)
        // vel.x = 0;
        vel.x *= restitution;
    if (pos.y > maxP.y || pos.y < worldOrigin.y)
        // vel.y = 0;
        vel.y *= restitution;
    pos.x = std::clamp(pos.x, worldOrigin.x, maxP.x);
    pos.y = std::clamp(pos.y, worldOrigin.y, maxP.y);
}

void UnitManager::ResolveEntityCollisions(float dt) {
    struct NeighborList {
        std::vector<int> neighbours;
    };

    // auto getNeighbours = [&](const int cellIndex) {
    //     NeighborList activeCellHeads;
    //     int cellX = cellIndex % gridCols;
    //     int cellY = cellIndex / gridCols;
    //     std::array<int, 5> heads{};
    //     int k = 0;
    //     heads[k++] = cellHeads[cellIndex];
    //     if (cellX < maxCol) { // east cell
    //         heads[k++] = cellHeads[cellIndex + 1];
    //     }
    //     if (cellY < maxRow) {
    //         heads[k++] = cellHeads[cellIndex + gridCols]; // south cell
    //         if (cellX < maxCol) {
    //             heads[k++] = cellHeads[cellIndex + gridCols + 1]; // south-east cell
    //         }
    //         // if (cellX > 0) {
    //         //     heads[k++] = cellHeads[cellIndex + gridCols - 1]; // south-west cell //TODO: check if can remove
    //         // }
    //     }
    //     for (int head : heads) {
    //         activeCellHeads.neighbours.push_back(head);
    //         for (int u = head; u != -1; u = nextUnit[u]) {
    //             activeCellHeads.neighbours.push_back(u);
    //         }
    //     }
    //     return activeCellHeads;
    // };

    auto getNeighbours = [&](const int cellIndex) {
        NeighborList activeCellHeads;
        int cellX = cellIndex % gridCols;
        int cellY = cellIndex / gridCols;
        std::array<int, 9> heads{};
        int k = 0;
        heads[k++] = cellHeads[cellIndex];
        if (cellY < maxRow) {
            heads[k++] = cellHeads[cellIndex + gridCols]; // south cell
            if (cellX < maxCol) {
                heads[k++] = cellHeads[cellIndex + gridCols + 1]; // south-east cell
            }
            if (cellX > 0) {
                heads[k++] = cellHeads[cellIndex + gridCols - 1]; // south-west cell //TODO: check if can remove
            }
        }
        if (cellY > 0) {
            heads[k++] = cellHeads[cellIndex - gridCols]; // north cell
            if (cellX < maxCol) {
                heads[k++] = cellHeads[cellIndex - gridCols + 1]; // north-east cell
            }
            if (cellX > 0) {
                heads[k++] = cellHeads[cellIndex - gridCols - 1]; // north-west cell //TODO: check if can remove
            }
        }
        if (cellX < maxCol) { // east cell
            heads[k++] = cellHeads[cellIndex + 1];
        }
        if (cellX > 0) { // west cell
            heads[k++] = cellHeads[cellIndex - 1];
        }
        for (int head : heads) {
            activeCellHeads.neighbours.push_back(head);
            for (int u = head; u != -1; u = nextUnit[u]) {
                activeCellHeads.neighbours.push_back(u);
            }
        }
        return activeCellHeads;
    };

    // TODO: fix, doesnt quite work yet.
    float desiredDistSquared = unitSize * unitSize;
    for (const int cellIndex : activeCells) {
        for (int unit = cellHeads[cellIndex]; unit != -1; unit = nextUnit[unit]) {
            // int unit = cellHeads[cellIndex];
            glm::vec2& pos = positions[unit];
            float px = pos.x;
            float py = pos.y;
            auto [neighbours] = getNeighbours(cellIndex);
            for (const int i : neighbours) {
                if (i == unit) {
                    continue;
                }
                glm::vec2& ipos = positions[i];
                float ipx = ipos.x;
                float dx = ipx - px;
                if (dx <= unitSize && dx > 0) {
                    float ipy = ipos.y;
                    float dy = ipy - py;
                    if (dy <= unitSize) {
                        float distSquared = dx * dx + dy * dy;
                        if (distSquared <= desiredDistSquared) {
                            float nx = dx / distSquared;
                            float ny = dy / distSquared;
                            float dist = sqrt(distSquared);
                            float overlap = unitSize - dist;
                            glm::vec2& vel = velocities[unit];
                            glm::vec2& ivel = velocities[i];
                            float vx = vel.x;
                            float vy = vel.y;
                            float ivx = ivel.x;
                            float ivy = ivel.y;
                            float dvx = ivx - vx;
                            float dvy = ivy - vy;
                            pos.x -= nx * (overlap);
                            pos.y -= ny * (overlap);
                            pos.x = std::clamp(pos.x, worldOrigin.x, maxP.x);
                            pos.y = std::clamp(pos.y, worldOrigin.y, maxP.y);
                            ipos.x += nx * (overlap);
                            ipos.y += ny * (overlap);
                            ipos.x = std::clamp(ipos.x, worldOrigin.x, maxP.x);
                            ipos.y = std::clamp(ipos.y, worldOrigin.y, maxP.y);
                            vel.x -= nx * (overlap);
                            vel.y -= ny * (overlap);
                            // vel.x = std::clamp(vel.x, 0.0f, 10.0f);
                            // vel.y = std::clamp(vel.y, 0.0f, 10.0f);
                            ivel.x += nx * (overlap);
                            ivel.y += ny * (overlap);
                            // ivel.x = std::clamp(ivel.x, 0.0f, 10.0f);
                            // ivel.y = std::clamp(ivel.y, 0.0f, 10.0f);
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
