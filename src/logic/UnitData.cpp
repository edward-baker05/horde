//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <execution>
#include <immintrin.h>

#include "SDL3/SDL_log.h"

// Constructor
UnitManager::UnitManager(const size_t MaxUnits, const glm::vec2 WorldBounds, const float unit_size) {
    Init(MaxUnits, WorldBounds, unit_size);
}

void UnitManager::Init(const size_t MaxUnits, const glm::vec2 WorldBounds, const float unit_size) {
    unitSize = unit_size;
    cellSize = unit_size;
    maxUnits = MaxUnits;
    worldBounds = WorldBounds;
    currentUnits = 0;
    Reserve(MaxUnits);
    maxP = worldBounds - glm::vec2(unitSize);
    invCellSize = 1.0f / cellSize;
    rowHeight = cellSize * 0.86602540378f; // sqrt(3)/2
    invRowHeight = 1.0f / rowHeight;
    BuildUniformGrid();
}

void UnitManager::Reserve(const size_t newMaxUnits) {
    maxUnits = newMaxUnits;
    positions.resize(newMaxUnits);
    velocities.resize(newMaxUnits);
    health.resize(newMaxUnits);
    invMass.resize(newMaxUnits);

    sortedPositions.resize(newMaxUnits);
    sortedVelocities.resize(newMaxUnits);
    sortedHealth.resize(newMaxUnits);
    sortedInvMass.resize(newMaxUnits);

    unitCellIndices.resize(newMaxUnits);

    unitGroupId.resize(newMaxUnits);
    unitStabilityTimer.resize(newMaxUnits);
    unitContactCount.resize(newMaxUnits);

    for (auto& snap : renderSnapshots) {
        snap.positions.resize(newMaxUnits);
        snap.health.resize(newMaxUnits);
        snap.unitCount = 0;
    }
}

void UnitManager::BuildUniformGrid() {
    gridCols = static_cast<int>(std::ceil(worldBounds.x * invCellSize)) + 1;
    gridRows = static_cast<int>(std::ceil(worldBounds.y * invRowHeight)) + 1;
    SDL_Log("HexGrid: cellSize=%f rowHeight=%f cols=%d rows=%d", cellSize, rowHeight, gridCols, gridRows);
    maxCol = gridCols - 1;
    maxRow = gridRows - 1;

    const int numCells = gridCols * gridRows;
    cellStarts.assign(numCells + 1, 0);
    cellCounts.assign(numCells, 0);
    cellCurrent.assign(numCells, 0);
    cellFlags.assign(numCells, CellFlag_None);
    cellGroupId.assign(numCells, -1);
    cellMeanVelocities.assign(numCells, {0.0f, 0.0f});
    cellIsStable.assign(numCells, 0);
    cellStabilityTimer.assign(numCells, 0);
    bfsQueue.reserve(numCells);
    activeCells.reserve(numCells);
}

inline void UnitManager::PruneGroupCell(int cellIndex) {
    if (cellIndex >= 0 && cellIndex < gridCols * gridRows) {
        cellFlags[cellIndex] = CellFlag_None;
        cellGroupId[cellIndex] = -1;
        cellStabilityTimer[cellIndex] = 0;
    }
}

void UnitManager::FormCellGroupsAndClassify() {
    // Sparse reset: only clear previously active cells (instant, zero 185k-element scan)
    for (int cellIndex : activeCells) {
        cellFlags[cellIndex] = CellFlag_None;
        cellGroupId[cellIndex] = -1;
        cellIsStable[cellIndex] = 0;
    }

    struct HexNeighbor { int dx, dy; };
    static constexpr HexNeighbor kEvenNeighbors[6] = {
        { 1,  0}, {-1,  0}, {-1, -1}, { 0, -1}, {-1,  1}, { 0,  1}
    };
    static constexpr HexNeighbor kOddNeighbors[6] = {
        { 1,  0}, {-1,  0}, { 0, -1}, { 1, -1}, { 0,  1}, { 1,  1}
    };

    const float minPackDist = unitSize * minPackingQuality;
    const float minPackingDistSq = minPackDist * minPackDist;
    const float maxPackingDistSq = (unitSize * 1.35f) * (unitSize * 1.35f);
    const float minOverlapDistSq = (unitSize * 0.85f) * (unitSize * 0.85f);

    // Step 1: Compute mean velocity and verify rotation-invariant hexagonal packing & no overlaps
    for (int cellIndex : activeCells) {
        const int cellStart = cellStarts[cellIndex];
        const int cellEnd = cellStarts[cellIndex + 1];
        const int count = cellEnd - cellStart;

        // Overcrowded cell check: if >2 units occupy the exact same cell, it is jammed/overlapping
        if (count == 0 || count > 2) {
            cellStabilityTimer[cellIndex] = 0;
            continue;
        }

        glm::vec2 sumVel = {0.0f, 0.0f};
        for (int u = cellStart; u < cellEnd; ++u) {
            sumVel += velocities[u];
        }
        glm::vec2 avgVel = sumVel / static_cast<float>(count);
        cellMeanVelocities[cellIndex] = avgVel;

        const int curX = cellIndex % gridCols;
        const int curY = cellIndex / gridCols;
        const HexNeighbor* neighbors = (curY % 2 == 0) ? kEvenNeighbors : kOddNeighbors;

        // Check contacts and overlap violations across all 7 neighbor cells in 360 degrees
        bool hasOverlap = false;
        int contactCount = 0;

        for (int u = cellStart; u < cellEnd; ++u) {
            const glm::vec2& uPos = positions[u];

            // 1. Check other units in same cell
            for (int v = cellStart; v < cellEnd; ++v) {
                if (u == v) continue;
                glm::vec2 delta = positions[v] - uPos;
                float dSq = delta.x * delta.x + delta.y * delta.y;
                if (dSq < minOverlapDistSq) {
                    hasOverlap = true;
                    break;
                }
                if (dSq >= minPackingDistSq && dSq <= maxPackingDistSq) {
                    contactCount++;
                }
            }
            if (hasOverlap) break;

            // 2. Check units in all 6 surrounding hex neighbor cells
            for (int k = 0; k < 6; ++k) {
                int nx = curX + neighbors[k].dx;
                int ny = curY + neighbors[k].dy;
                if (nx < 0 || nx > maxCol || ny < 0 || ny > maxRow) continue;

                int nIdx = nx + ny * gridCols;
                int nStart = cellStarts[nIdx];
                int nEnd = cellStarts[nIdx + 1];
                for (int v = nStart; v < nEnd; ++v) {
                    glm::vec2 delta = positions[v] - uPos;
                    float dSq = delta.x * delta.x + delta.y * delta.y;
                    if (dSq < minOverlapDistSq) {
                        hasOverlap = true;
                        break;
                    }
                    if (dSq >= minPackingDistSq && dSq <= maxPackingDistSq) {
                        glm::vec2 diffV = velocities[v] - avgVel;
                        if (diffV.x * diffV.x + diffV.y * diffV.y < groupNeighborDiffSq) {
                            contactCount++;
                        }
                    }
                }
                if (hasOverlap) break;
            }
        }

        // Wall & floor collision check: units hitting walls or floor with moving velocity stay AWAKE
        bool touchingWallWithVelocity = false;
        for (int u = cellStart; u < cellEnd; ++u) {
            const glm::vec2& p = positions[u];
            const glm::vec2& v = velocities[u];
            if (p.y >= maxP.y - unitSize * 0.75f ||
                p.x <= worldOrigin.x + unitSize * 0.75f ||
                p.x >= maxP.x - unitSize * 0.75f ||
                p.y <= worldOrigin.y + unitSize * 0.75f) {
                if (std::abs(v.x) > 0.25f || std::abs(v.y) > 0.25f) {
                    touchingWallWithVelocity = true;
                    break;
                }
            }
        }
        if (touchingWallWithVelocity) {
            cellStabilityTimer[cellIndex] = 0;
            cellIsStable[cellIndex] = 0;
            continue;
        }

        // Cell is stable if it has valid close-packed contacts and ZERO overlap violations
        if (!hasOverlap && contactCount >= 1) {
            cellStabilityTimer[cellIndex] = std::min<uint8_t>(cellStabilityTimer[cellIndex] + 1, 16);
        } else {
            cellStabilityTimer[cellIndex] = (cellStabilityTimer[cellIndex] >= 2) ? (cellStabilityTimer[cellIndex] - 2) : 0;
        }

        if (cellStabilityTimer[cellIndex] >= static_cast<uint8_t>(stabilityFramesToSleep)) {
            cellIsStable[cellIndex] = 1;
        }
    }

    // Step 2: Flood-fill connected groups of stable cells with low relative velocity
    int nextGroupId = 0;
    bfsQueue.clear();

    for (int cellIndex : activeCells) {
        if (!cellIsStable[cellIndex] || cellGroupId[cellIndex] != -1) continue;

        const int currentGroupId = nextGroupId++;
        cellGroupId[cellIndex] = currentGroupId;
        cellFlags[cellIndex] |= CellFlag_InGroup;

        bfsQueue.clear();
        bfsQueue.push_back(cellIndex);

        size_t head = 0;
        while (head < bfsQueue.size()) {
            int curIdx = bfsQueue[head++];
            const int curX = curIdx % gridCols;
            const int curY = curIdx / gridCols;
            const glm::vec2& curVel = cellMeanVelocities[curIdx];

            const HexNeighbor* neighbors = (curY % 2 == 0) ? kEvenNeighbors : kOddNeighbors;
            for (int k = 0; k < 6; ++k) {
                int nx = curX + neighbors[k].dx;
                int ny = curY + neighbors[k].dy;
                if (nx < 0 || nx > maxCol || ny < 0 || ny > maxRow) continue;

                int nIdx = nx + ny * gridCols;
                if (cellCounts[nIdx] > 0 && cellIsStable[nIdx] && cellGroupId[nIdx] == -1) {
                    glm::vec2 diffV = cellMeanVelocities[nIdx] - curVel;
                    if (diffV.x * diffV.x + diffV.y * diffV.y < groupNeighborDiffSq) {
                        cellGroupId[nIdx] = currentGroupId;
                        cellFlags[nIdx] |= CellFlag_InGroup;
                        bfsQueue.push_back(nIdx);
                    }
                }
            }
        }
    }

    // Step 2.5: Synchronize group velocities (treat entire macro-block as a single rigid body)
    if (nextGroupId > 0) {
        groupVelocities.assign(nextGroupId, {0.0f, 0.0f});
        groupUnitCounts.assign(nextGroupId, 0);

        for (int cellIndex : activeCells) {
            const int g = cellGroupId[cellIndex];
            if (g >= 0) {
                const int cellStart = cellStarts[cellIndex];
                const int cellEnd = cellStarts[cellIndex + 1];
                for (int u = cellStart; u < cellEnd; ++u) {
                    groupVelocities[g] += velocities[u];
                    groupUnitCounts[g]++;
                }
            }
        }

        for (int g = 0; g < nextGroupId; ++g) {
            if (groupUnitCounts[g] > 0) {
                groupVelocities[g] /= static_cast<float>(groupUnitCounts[g]);
            }
        }

        for (int cellIndex : activeCells) {
            const int g = cellGroupId[cellIndex];
            if (g >= 0) {
                const glm::vec2& gVel = groupVelocities[g];
                const int cellStart = cellStarts[cellIndex];
                const int cellEnd = cellStarts[cellIndex + 1];
                for (int u = cellStart; u < cellEnd; ++u) {
                    velocities[u] = gVel;
                }
            }
        }
    }

    // Step 3: Classify Group Interior vs Group Edge & Apply visual color codes
    size_t interiorCount = 0;
    size_t edgeCount = 0;

    for (int cellIndex : activeCells) {
        const int cellStart = cellStarts[cellIndex];
        const int cellEnd = cellStarts[cellIndex + 1];

        if (!(cellFlags[cellIndex] & CellFlag_InGroup)) {
            for (int u = cellStart; u < cellEnd; ++u) {
                if (health[u] < 0) {
                    health[u] = 10;
                }
            }
            continue;
        }

        const int curGroupId = cellGroupId[cellIndex];
        const int cellX = cellIndex % gridCols;
        const int cellY = cellIndex / gridCols;

        bool isInterior = (cellX > 0 && cellX < maxCol && cellY > 0 && cellY < maxRow);
        if (isInterior) {
            const HexNeighbor* neighbors = (cellY % 2 == 0) ? kEvenNeighbors : kOddNeighbors;
            for (int k = 0; k < 6; ++k) {
                int nx = cellX + neighbors[k].dx;
                int ny = cellY + neighbors[k].dy;
                int nIdx = nx + ny * gridCols;
                if (cellGroupId[nIdx] != curGroupId) {
                    isInterior = false;
                    break;
                }
            }
        }

        if (isInterior) {
            cellFlags[cellIndex] |= CellFlag_GroupInterior;
        }

        // Deep Royal Blue (-2) for Group Interior, Sky Blue (-1) for Group Edge
        const int colorCode = isInterior ? -2 : -1;
        const size_t cCount = cellEnd - cellStart;
        if (isInterior) {
            interiorCount += cCount;
        } else {
            edgeCount += cCount;
        }

        for (int u = cellStart; u < cellEnd; ++u) {
            health[u] = colorCode;
        }
    }

    sleepingInteriorUnits = interiorCount;
    sleepingEdgeUnits = edgeCount;
    activeUnits = (currentUnits > (interiorCount + edgeCount)) ? (currentUnits - interiorCount - edgeCount) : 0;
}

void UnitManager::PopulateUniformGrid() {
    const int numCells = gridCols * gridRows;
    std::ranges::fill(cellCounts, 0);

    // Pass 1: Compute hex cell index for each unit and count occupancy
    for (size_t i = 0; i < currentUnits; ++i) {
        const glm::vec2& pos = positions[i];
        const int row = std::clamp(static_cast<int>(pos.y * invRowHeight), 0, maxRow);
        const float xOffset = (row % 2 == 1) ? (0.5f * cellSize) : 0.0f;
        const int col = std::clamp(static_cast<int>((pos.x - xOffset) * invCellSize), 0, maxCol);
        const int cellIndex = col + row * gridCols;

        unitCellIndices[i] = cellIndex;
        cellCounts[cellIndex]++;
    }

    // Prefix sum: compute cellStarts offsets and collect non-empty active cells
    activeCells.clear();
    int runningSum = 0;
    for (int c = 0; c < numCells; ++c) {
        cellStarts[c] = runningSum;
        cellCurrent[c] = runningSum;
        if (cellCounts[c] > 0) {
            activeCells.push_back(c);
            runningSum += cellCounts[c];
        }
    }
    cellStarts[numCells] = runningSum;

    // Pass 2: Scatter units into contiguous sorted buffers
    for (size_t i = 0; i < currentUnits; ++i) {
        const int cellIndex = unitCellIndices[i];
        const int dest = cellCurrent[cellIndex]++;

        sortedPositions[dest] = positions[i];
        sortedVelocities[dest] = velocities[i];
        sortedHealth[dest] = health[i];
        sortedInvMass[dest] = invMass[i];
    }

    // O(1) buffer swaps
    std::swap(positions, sortedPositions);
    std::swap(velocities, sortedVelocities);
    std::swap(health, sortedHealth);
    std::swap(invMass, sortedInvMass);
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

void UnitManager::ClearUnits() {
    currentUnits = 0;
    sleepingInteriorUnits = 0;
    sleepingEdgeUnits = 0;
    activeUnits = 0;
    if (!cellStabilityTimer.empty()) {
        std::ranges::fill(cellStabilityTimer, 0);
        std::ranges::fill(cellIsStable, 0);
        std::ranges::fill(cellFlags, 0);
        std::ranges::fill(cellGroupId, -1);
    }
}

void UnitManager::UpdatePhysics(float dt) {
    const int steps = std::clamp(physicsSubSteps, 1, 8);
    const float subDt = dt / static_cast<float>(steps);

    // 1. Map units to spatial grid and compute stability / sleeping groups ONCE per frame
    PopulateUniformGrid();
    FormCellGroupsAndClassify();

    // 2. High-speed physics sub-steps (sleeping groups skip collisions with O(0) cost)
    for (int step = 0; step < steps; ++step) {
        UpdatePositions(subDt);
        if (step > 0) {
            PopulateUniformGrid();
        }
        ResolveEntityCollisions(subDt);
    }
}

void UnitManager::UpdatePositions(float dt) {
    const size_t count = currentUnits;
    size_t i = 0;

    const __m256 v_dt = _mm256_set1_ps(dt);
    const __m256 v_gravity = _mm256_setr_ps(0.0f, 10.0f * dt, 0.0f, 10.0f * dt, 0.0f, 10.0f * dt, 0.0f, 10.0f * dt);
    const __m256 v_minVel = _mm256_set1_ps(-25.0f);
    const __m256 v_maxVel = _mm256_set1_ps(25.0f);

    const __m256 v_minPos = _mm256_set1_ps(0.0f);
    const __m256 v_maxPos = _mm256_setr_ps(maxP.x, maxP.y, maxP.x, maxP.y, maxP.x, maxP.y, maxP.x, maxP.y);
    const __m256 v_restitution = _mm256_set1_ps(restitution);

    // Process 4 units (8 floats) per AVX2 register iteration
    for (; i + 4 <= count; i += 4) {
        __m256 v_vel = _mm256_loadu_ps(reinterpret_cast<const float*>(&velocities[i]));
        __m256 v_pos = _mm256_loadu_ps(reinterpret_cast<const float*>(&positions[i]));

        // Apply gravity & clamp velocity to [-25.0f, 25.0f]
        v_vel = _mm256_add_ps(v_vel, v_gravity);
        v_vel = _mm256_max_ps(v_minVel, _mm256_min_ps(v_vel, v_maxVel));

        // Update position: pos += vel * dt
        v_pos = _mm256_fmadd_ps(v_vel, v_dt, v_pos);

        // Edge collisions: check if pos < minPos or pos > maxPos
        __m256 v_lt_min = _mm256_cmp_ps(v_pos, v_minPos, _CMP_LT_OQ);
        __m256 v_gt_max = _mm256_cmp_ps(v_pos, v_maxPos, _CMP_GT_OQ);
        __m256 v_out_of_bounds = _mm256_or_ps(v_lt_min, v_gt_max);

        // Bounce velocity on walls/ceiling
        __m256 v_bounced_vel = _mm256_mul_ps(v_vel, v_restitution);
        v_vel = _mm256_blendv_ps(v_vel, v_bounced_vel, v_out_of_bounds);

        // Clamp positions to world boundaries
        v_pos = _mm256_max_ps(v_minPos, _mm256_min_ps(v_pos, v_maxPos));

        _mm256_storeu_ps(reinterpret_cast<float*>(&velocities[i]), v_vel);
        _mm256_storeu_ps(reinterpret_cast<float*>(&positions[i]), v_pos);
    }

    // Process remainder units (< 4)
    for (; i < count; ++i) {
        velocities[i].y += 10.0f * dt;
        velocities[i] = glm::clamp(velocities[i], -25.0f, 25.0f);
        positions[i] += velocities[i] * dt;
        ResolveEdgeCollisions(positions[i], velocities[i]);
    }
}

inline void UnitManager::ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const {
    if (pos.x >= maxP.x || pos.x <= worldOrigin.x) {
        vel.x *= restitution;
    }
    if (pos.y <= worldOrigin.y) {
        vel.y *= restitution;
    }
    if (pos.y >= maxP.y) {
        vel.y = 0.0f;
        vel.x *= 0.85f;
    }
    pos.x = std::clamp(pos.x, worldOrigin.x, maxP.x);
    pos.y = std::clamp(pos.y, worldOrigin.y, maxP.y);
}

inline bool UnitManager::ResolveCollisionPair(int u, int v, float desiredDistSquared) {
    glm::vec2& pos = positions[u];
    glm::vec2& ipos = positions[v];
    float dx = ipos.x - pos.x;
    if (std::abs(dx) <= unitSize) {
        float dy = ipos.y - pos.y;
        if (std::abs(dy) <= unitSize) {
            float distSquared = dx * dx + dy * dy;
            if (distSquared < desiredDistSquared && distSquared > 0.0001f) {
                if (health[u] > 0) health[u] = 2;
                if (health[v] > 0) health[v] = 2;
                float dist = std::sqrt(distSquared);
                float invDist = 1.0f / dist;
                glm::vec2 normal = {dx * invDist, dy * invDist};

                // Symmetric equal overlap separation
                float overlap = 0.5f * (unitSize - dist);
                pos -= normal * overlap;
                ipos += normal * overlap;
                pos.x = std::clamp(pos.x, worldOrigin.x, maxP.x);
                pos.y = std::clamp(pos.y, worldOrigin.y, maxP.y);
                ipos.x = std::clamp(ipos.x, worldOrigin.x, maxP.x);
                ipos.y = std::clamp(ipos.y, worldOrigin.y, maxP.y);

                // Inelastic collision damping to absorb impact kinetic energy
                glm::vec2 relativeVelocity = velocities[v] - velocities[u];
                float normalVelocity = glm::dot(relativeVelocity, normal);
                if (normalVelocity < 0.0f) {
                    float impulse = -normalVelocity * 0.5f;
                    velocities[u] -= normal * impulse;
                    velocities[v] += normal * impulse;
                }
                return true;
            }
        }
    }
    return false;
}

bool UnitManager::ResolveRangeCollisions(int u, int start, int end, float desiredDistSquared) {
    int v = start;
    const glm::vec2& pos = positions[u];
    bool anyHit = false;

    const __m256 v_pos_u = _mm256_setr_ps(pos.x, pos.y, pos.x, pos.y, pos.x, pos.y, pos.x, pos.y);
    const __m256 v_maxDistSq = _mm256_set1_ps(desiredDistSquared);
    const __m256 v_minDistSq = _mm256_set1_ps(0.0001f);

    // Contiguous SIMD batch check: 4 units (8 floats) per AVX2 register load
    for (; v + 4 <= end; v += 4) {
        __m256 v_pos_v = _mm256_loadu_ps(reinterpret_cast<const float*>(&positions[v]));
        __m256 v_d = _mm256_sub_ps(v_pos_v, v_pos_u);
        __m256 v_d2 = _mm256_mul_ps(v_d, v_d);
        __m256 v_hadd = _mm256_hadd_ps(v_d2, v_d2);

        __m256 v_cmp_lt = _mm256_cmp_ps(v_hadd, v_maxDistSq, _CMP_LT_OQ);
        __m256 v_cmp_gt = _mm256_cmp_ps(v_hadd, v_minDistSq, _CMP_GT_OQ);
        __m256 v_hit = _mm256_and_ps(v_cmp_lt, v_cmp_gt);

        int mask = _mm256_movemask_ps(v_hit);
        if ((mask & 0x33) != 0) {
            if (mask & 0x01) anyHit |= ResolveCollisionPair(u, v + 0, desiredDistSquared);
            if (mask & 0x02) anyHit |= ResolveCollisionPair(u, v + 1, desiredDistSquared);
            if (mask & 0x10) anyHit |= ResolveCollisionPair(u, v + 2, desiredDistSquared);
            if (mask & 0x20) anyHit |= ResolveCollisionPair(u, v + 3, desiredDistSquared);
        }
    }

    // Remainder units (< 4)
    for (; v < end; ++v) {
        anyHit |= ResolveCollisionPair(u, v, desiredDistSquared);
    }
    return anyHit;
}

void UnitManager::ResolveEntityCollisions(float /*dt*/) {
    const float desiredDistSquared = unitSize * unitSize;
    const size_t numActive = activeCells.size();
    if (numActive == 0) return;

    const int iterations = std::clamp(solverIterations, 1, 10);
    for (int iter = 0; iter < iterations; ++iter) {
        const bool reverse = (iter % 2 == 1);
        for (size_t a = 0; a < numActive; ++a) {
            const size_t idx = reverse ? (numActive - 1 - a) : a;
            const int cellIndex = activeCells[idx];
            const uint8_t flagsA = cellFlags[cellIndex];
            const bool isInteriorA = (flagsA & CellFlag_GroupInterior) != 0;
            const int groupA = cellGroupId[cellIndex];

            const int cellX = cellIndex % gridCols;
            const int cellY = cellIndex / gridCols;
            const int cellStart = cellStarts[cellIndex];
            const int cellEnd = cellStarts[cellIndex + 1];

            // 1. Intra-cell collisions:
            // Only deep interior cells skip intra-cell collisions. Edge cells always resolve intra-cell collisions.
            if (!isInteriorA) {
                for (int u = cellStart; u < cellEnd; ++u) {
                    ResolveRangeCollisions(u, u + 1, cellEnd, desiredDistSquared);
                }
            }

            // 2. Inter-cell collisions: test 3 forward neighbor cells
            auto processNeighbor = [&](int nx, int ny) {
                int nIdx = nx + ny * gridCols;
                int nStart = cellStarts[nIdx];
                int nEnd = cellStarts[nIdx + 1];
                if (nStart >= nEnd) return;

                const uint8_t flagsB = cellFlags[nIdx];
                const int groupB = cellGroupId[nIdx];

                // If both cells belong to the SAME sleeping group:
                // They are part of the same rigid macro-block -> 100% SKIPPED (O(0))!
                if (groupA != -1 && groupA == groupB) {
                    return;
                }

                // Foreign collision: cells belong to different groups or at least one is awake
                bool hit = false;
                for (int u = cellStart; u < cellEnd; ++u) {
                    hit |= ResolveRangeCollisions(u, nStart, nEnd, desiredDistSquared);
                }

                // If collision occurred with awake units or foreign group, wake up colliding cells
                if (hit) {
                    if (flagsA & CellFlag_InGroup) PruneGroupCell(cellIndex);
                    if (flagsB & CellFlag_InGroup) PruneGroupCell(nIdx);
                }
            };

            // 3 Canonical Forward Hexagonal Neighbors:
            // 1. East: (cellX + 1, cellY)
            if (cellX + 1 <= maxCol) {
                processNeighbor(cellX + 1, cellY);
            }

            // 2. South-West and 3. South-East
            if (cellY + 1 <= maxRow) {
                int swX = (cellY % 2 == 0) ? (cellX - 1) : cellX;
                if (swX >= 0 && swX <= maxCol) {
                    processNeighbor(swX, cellY + 1);
                }

                int seX = (cellY % 2 == 0) ? cellX : (cellX + 1);
                if (seX >= 0 && seX <= maxCol) {
                    processNeighbor(seX, cellY + 1);
                }
            }
        }
    }

    // Re-synchronize group velocities across all units in each macro-block (100% rigid cohesion, zero overlap)
    for (int cellIndex : activeCells) {
        const int g = cellGroupId[cellIndex];
        if (g >= 0 && g < static_cast<int>(groupVelocities.size())) {
            const glm::vec2& gVel = groupVelocities[g];
            const int cellStart = cellStarts[cellIndex];
            const int cellEnd = cellStarts[cellIndex + 1];
            for (int u = cellStart; u < cellEnd; ++u) {
                velocities[u] = gVel;
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

void UnitManager::PublishRenderSnapshot() {
    RenderSnapshot& snap = renderSnapshots[writeSnapshotIndex];
    snap.unitCount = currentUnits;
    if (currentUnits > 0) {
        std::memcpy(snap.positions.data(), positions.data(), currentUnits * sizeof(glm::vec2));
        std::memcpy(snap.health.data(), health.data(), currentUnits * sizeof(int));
    }

    const int prevReady = readySnapshotIndex.exchange(writeSnapshotIndex, std::memory_order_release);
    // Find an unused buffer slot among the 3 buffers (not the one we just wrote, not the one being read, and not the previous ready)
    for (int i = 0; i < 3; ++i) {
        if (i != writeSnapshotIndex && i != readSnapshotIndex && i != prevReady) {
            writeSnapshotIndex = i;
            break;
        }
    }
}

const RenderSnapshot* UnitManager::AcquireRenderSnapshot() {
    const int latest = readySnapshotIndex.exchange(-1, std::memory_order_acq_rel);
    if (latest >= 0 && latest < 3) {
        readSnapshotIndex = latest;
    }
    if (readSnapshotIndex >= 0 && readSnapshotIndex < 3) {
        return &renderSnapshots[readSnapshotIndex];
    }
    return nullptr;
}