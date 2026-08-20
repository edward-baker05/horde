#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "Logic/UnitData.hpp"

UnitManager::UnitManager(const size_t MaxUnits, horde::core::ThreadPool* pool) : MaxUnits(MaxUnits), threadPool(pool) {
    Reserve(MaxUnits);
    SetWorldBounds(worldBounds);
}

void UnitManager::Reserve(size_t newMaxUnits) {
    MaxUnits = newMaxUnits;
    positions.resize(MaxUnits);
    velocities.resize(MaxUnits);
    sizes.resize(MaxUnits, 5.0f);
    health.resize(MaxUnits);
    invMass.resize(MaxUnits);
    cramCount.resize(MaxUnits, 0);
    NextUnit.resize(MaxUnits, -1);
    if (CurrentUnits > MaxUnits) {
        CurrentUnits = MaxUnits;
    }
}

void UnitManager::ClearUnits() {
    for (int cellIdx : activeCells) {
        CellHeads[static_cast<size_t>(cellIdx)] = -1;
    }
    activeCells.clear();
    for (auto& colorList : activeCellsByColor) {
        colorList.clear();
    }
    CurrentUnits = 0;
}

void UnitManager::SetWorldBounds(const BoundingBox& bounds) {
    worldBounds = bounds;
    const float width = std::max(1.0f, worldBounds.max.x - worldBounds.min.x);
    const float height = std::max(1.0f, worldBounds.max.y - worldBounds.min.y);

    gridCols = std::max(1, static_cast<int>(std::ceil(width / cellSize)));
    gridRows = std::max(1, static_cast<int>(std::ceil(height / cellSize)));
    CellHeads.assign(static_cast<size_t>(gridCols) * static_cast<size_t>(gridRows), -1);
    activeCells.clear();
    for (auto& colorList : activeCellsByColor) {
        colorList.clear();
    }

    vectorField.resize(worldBounds, vectorField.getCellSize());
}

void UnitManager::SetCellSize(float size) {
    cellSize = std::max(1.0f, size);
    SetWorldBounds(worldBounds);
}

const BoundingBox& UnitManager::GetWorldBounds() const {
    return worldBounds;
}

BoundingBox UnitManager::GetBoundingBox(size_t index) const {
    const float radius = sizes[index];
    return BoundingBox::fromCenterHalfExtents(positions[index], glm::vec2(radius, radius));
}

void UnitManager::BuildSpatialGrid() {
    // Sparse reset: only reset cells that were occupied in the previous sub-step
    for (int cellIdx : activeCells) {
        CellHeads[static_cast<size_t>(cellIdx)] = -1;
    }
    activeCells.clear();
    for (auto& colorList : activeCellsByColor) {
        colorList.clear();
    }

    const float invCellSize = 1.0f / cellSize;
    const float minX = worldBounds.min.x;
    const float minY = worldBounds.min.y;
    const int maxCol = gridCols - 1;
    const int maxRow = gridRows - 1;

    const glm::vec2* const posPtr = positions.data();
    int* const nextPtr = NextUnit.data();
    int* const headPtr = CellHeads.data();
    const size_t count = CurrentUnits;

    for (size_t i = 0; i < count; ++i) {
        int cellX = static_cast<int>((posPtr[i].x - minX) * invCellSize);
        int cellY = static_cast<int>((posPtr[i].y - minY) * invCellSize);

        cellX = std::clamp(cellX, 0, maxCol);
        cellY = std::clamp(cellY, 0, maxRow);

        const int cellIdx = cellY * gridCols + cellX;
        if (headPtr[cellIdx] == -1) {
            activeCells.push_back(cellIdx);
            const int color = (cellY % 2) * 3 + (cellX % 3);
            activeCellsByColor[static_cast<size_t>(color)].push_back(cellIdx);
        }
        nextPtr[i] = headPtr[cellIdx];
        headPtr[cellIdx] = static_cast<int>(i);
    }
}

void UnitManager::ResolveEntityCollisions() {
    const int cols = gridCols;
    const int rows = gridRows;

    glm::vec2* const posPtr = positions.data();
    glm::vec2* const velPtr = velocities.data();
    const float* const sizePtr = sizes.data();
    const float* const invMassPtr = invMass.data();
    int* const cramPtr = cramCount.data();
    const int* const nextPtr = NextUnit.data();
    const int* const headPtr = CellHeads.data();
    const bool cramEnabled = enableCramming;

    if (cramEnabled && CurrentUnits > 0) {
        std::memset(cramPtr, 0, CurrentUnits * sizeof(int));
    }

    const size_t numThreads = threadPool ? threadPool->threadCount() : 1;
    std::vector<size_t> threadPairs(numThreads, 0);

    // 6-color grid decomposition guarantees zero spatial overlap between cells of the same color
    for (size_t c = 0; c < 6; ++c) {
        const auto& colorCells = activeCellsByColor[c];
        const size_t numCellsInColor = colorCells.size();
        if (numCellsInColor == 0) {
            continue;
        }

        auto processCellRange = [&](size_t start, size_t end, size_t threadIdx) {
            size_t localPairs = 0;

            for (size_t a = start; a < end; ++a) {
                const int cellIdx = colorCells[a];
                const int headA = headPtr[cellIdx];
                if (headA == -1) {
                    continue;
                }

                const int cx = cellIdx % cols;
                const int cy = cellIdx / cols;

                // Collect valid forward neighbor cell heads:
                // 1. East: (cx+1, cy)
                // 2. South-West: (cx-1, cy+1)
                // 3. South: (cx, cy+1)
                // 4. South-East: (cx+1, cy+1)
                int neighborHeads[4];
                int neighborCount = 0;

                if (cx + 1 < cols) {
                    const int h = headPtr[cellIdx + 1];
                    if (h != -1) {
                        neighborHeads[neighborCount++] = h;
                    }
                }
                if (cx > 0 && cy + 1 < rows) {
                    const int h = headPtr[cellIdx + cols - 1];
                    if (h != -1) {
                        neighborHeads[neighborCount++] = h;
                    }
                }
                if (cy + 1 < rows) {
                    const int h = headPtr[cellIdx + cols];
                    if (h != -1) {
                        neighborHeads[neighborCount++] = h;
                    }
                }
                if (cx + 1 < cols && cy + 1 < rows) {
                    const int h = headPtr[cellIdx + cols + 1];
                    if (h != -1) {
                        neighborHeads[neighborCount++] = h;
                    }
                }

                // Single pass over headA: i's state is pinned in registers across all pair tests
                for (int i = headA; i != -1; i = nextPtr[i]) {
                    float posIx = posPtr[i].x;
                    float posIy = posPtr[i].y;
                    float velIx = velPtr[i].x;
                    float velIy = velPtr[i].y;
                    const float rI = sizePtr[i];
                    const float invMassI = invMassPtr[i];

                    auto testPairWithI = [&](int j) {
                        const float rJ = sizePtr[j];
                        const float minDist = rI + rJ;
                        const float dx = posPtr[j].x - posIx;
                        const float dy = posPtr[j].y - posIy;
                        const float distSq = dx * dx + dy * dy;

                        if (distSq < minDist * minDist) {
                            if (cramEnabled) {
                                cramPtr[i]++;
                                cramPtr[j]++;
                            }

                            const float dist = std::sqrt(std::max(distSq, 1e-6f));
                            const float invDist = 1.0f / dist;
                            const float nx = (distSq > 1e-6f) ? (dx * invDist) : 1.0f;
                            const float ny = (distSq > 1e-6f) ? (dy * invDist) : 0.0f;
                            const float depth = minDist - dist;

                            const float invMassJ = invMassPtr[j];
                            const float totalInvMass = invMassI + invMassJ;
                            if (totalInvMass > 0.0f) {
                                const float invTotalInvMass = 1.0f / totalInvMass;
                                const float moveFactor = depth * 0.5f * invTotalInvMass;
                                const float moveA = invMassI * moveFactor;
                                const float moveB = invMassJ * moveFactor;

                                posIx -= nx * moveA;
                                posIy -= ny * moveA;
                                posPtr[j].x += nx * moveB;
                                posPtr[j].y += ny * moveB;

                                const float rvx = velPtr[j].x - velIx;
                                const float rvy = velPtr[j].y - velIy;
                                const float velAlongNormal = rvx * nx + rvy * ny;

                                if (velAlongNormal < 0.0f) {
                                    constexpr float restitution = 0.8f;
                                    const float impulseMag = -(1.0f + restitution) * velAlongNormal * invTotalInvMass;

                                    velIx -= nx * (impulseMag * invMassI);
                                    velIy -= ny * (impulseMag * invMassI);
                                    velPtr[j].x += nx * (impulseMag * invMassJ);
                                    velPtr[j].y += ny * (impulseMag * invMassJ);
                                }
                            }
                        }
                    };

                    // 1. Intra-cell pairs (test with succeeding units in this cell)
                    for (int j = nextPtr[i]; j != -1; j = nextPtr[j]) {
                        testPairWithI(j);
                        ++localPairs;
                    }

                    // 2. Inter-cell pairs (test with units in neighbor cells)
                    for (int n = 0; n < neighborCount; ++n) {
                        for (int j = neighborHeads[n]; j != -1; j = nextPtr[j]) {
                            testPairWithI(j);
                            ++localPairs;
                        }
                    }

                    // Write back updated position and velocity once
                    posPtr[i].x = posIx;
                    posPtr[i].y = posIy;
                    velPtr[i].x = velIx;
                    velPtr[i].y = velIy;
                }
            }

            if (threadIdx < threadPairs.size()) {
                threadPairs[threadIdx] += localPairs;
            }
        };

        if (threadPool && numCellsInColor > 16) {
            threadPool->parallelFor(0, numCellsInColor, processCellRange, 16);
        } else {
            processCellRange(0, numCellsInColor, 0);
        }
    }

    size_t totalPairsChecked = 0;
    for (size_t p : threadPairs) {
        totalPairsChecked += p;
    }
    profile.collisionPairsChecked = totalPairsChecked;
    profile.activeCellCount = activeCells.size();
}

void UnitManager::ResolveBoundaryCollisions() {
    const float minBoundX = worldBounds.min.x;
    const float minBoundY = worldBounds.min.y;
    const float maxBoundX = worldBounds.max.x;
    const float maxBoundY = worldBounds.max.y;

    glm::vec2* const posPtr = positions.data();
    glm::vec2* const velPtr = velocities.data();
    const float* const sizePtr = sizes.data();
    const size_t count = CurrentUnits;

    auto resolveRange = [=](size_t start, size_t end, size_t /*threadIdx*/) {
        for (size_t i = start; i < end; ++i) {
            const float r = sizePtr[i];
            const float minX = minBoundX + r;
            const float maxX = maxBoundX - r;
            const float minY = minBoundY + r;
            const float maxY = maxBoundY - r;

            float px = posPtr[i].x;
            float py = posPtr[i].y;
            float vx = velPtr[i].x;
            float vy = velPtr[i].y;

            if (px < minX) {
                px = minX;
                if (vx < 0.0f) {
                    vx = -vx * 0.8f;
                }
            } else if (px > maxX) {
                px = maxX;
                if (vx > 0.0f) {
                    vx = -vx * 0.8f;
                }
            }

            if (py < minY) {
                py = minY;
                if (vy < 0.0f) {
                    vy = -vy * 0.8f;
                }
            } else if (py > maxY) {
                py = maxY;
                if (vy > 0.0f) {
                    vy = -vy * 0.8f;
                }
            }

            posPtr[i].x = px;
            posPtr[i].y = py;
            velPtr[i].x = vx;
            velPtr[i].y = vy;
        }
    };

    if (threadPool && count > 512) {
        threadPool->parallelFor(0, count, resolveRange, 512);
    } else {
        resolveRange(0, count, 0);
    }
}

void UnitManager::ResolveCrammingAndDeaths() {
    if (CurrentUnits == 0) {
        return;
    }

    // Apply cramming damage to over-crowded units
    if (enableCramming) {
        const int maxLimit = maxCrammingLimit;
        const int damage = cramDamage;
        int* const healthPtr = health.data();
        const int* const cramPtr = cramCount.data();
        const size_t count = CurrentUnits;

        for (size_t i = 0; i < count; ++i) {
            if (cramPtr[i] >= maxLimit) {
                healthPtr[i] -= damage;
            }
        }
    }

    // Compact in-place by removing dead units (health <= 0)
    size_t writeIdx = 0;
    size_t killed = 0;

    glm::vec2* const posPtr = positions.data();
    glm::vec2* const velPtr = velocities.data();
    float* const sizePtr = sizes.data();
    int* const healthPtr = health.data();
    float* const invMassPtr = invMass.data();
    int* const cramPtr = cramCount.data();
    const size_t count = CurrentUnits;

    for (size_t readIdx = 0; readIdx < count; ++readIdx) {
        if (healthPtr[readIdx] > 0) {
            if (writeIdx != readIdx) {
                posPtr[writeIdx] = posPtr[readIdx];
                velPtr[writeIdx] = velPtr[readIdx];
                sizePtr[writeIdx] = sizePtr[readIdx];
                healthPtr[writeIdx] = healthPtr[readIdx];
                invMassPtr[writeIdx] = invMassPtr[readIdx];
                cramPtr[writeIdx] = cramPtr[readIdx];
            }
            ++writeIdx;
        } else {
            ++killed;
        }
    }

    CurrentUnits = writeIdx;
    profile.crammedUnitsKilled += killed;
}

void UnitManager::UpdatePhysics(float dt) {
    const Uint64 t0 = SDL_GetPerformanceCounter();

    const float clampedDt = std::min(dt, 0.05f);
    constexpr int subSteps = 2;
    const float subDt = clampedDt / static_cast<float>(subSteps);

    Uint64 posTicks = 0;
    Uint64 gridTicks = 0;
    Uint64 colTicks = 0;
    Uint64 boundTicks = 0;

    for (int step = 0; step < subSteps; ++step) {
        const Uint64 tStart = SDL_GetPerformanceCounter();
        UpdatePositions(subDt);
        const Uint64 tPos = SDL_GetPerformanceCounter();
        posTicks += (tPos - tStart);

        BuildSpatialGrid();
        const Uint64 tGrid = SDL_GetPerformanceCounter();
        gridTicks += (tGrid - tPos);

        ResolveEntityCollisions();
        const Uint64 tCol = SDL_GetPerformanceCounter();
        colTicks += (tCol - tGrid);

        ResolveBoundaryCollisions();
        const Uint64 tBound = SDL_GetPerformanceCounter();
        boundTicks += (tBound - tCol);

        ResolveCrammingAndDeaths();
    }

    const Uint64 t1 = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency()) / 1000.0;
    profile.totalPhysicsTimeMs = static_cast<float>(static_cast<double>(t1 - t0) / freq);
    profile.updatePosTimeMs = static_cast<float>(static_cast<double>(posTicks) / freq);
    profile.gridBuildTimeMs = static_cast<float>(static_cast<double>(gridTicks) / freq);
    profile.collisionTimeMs = static_cast<float>(static_cast<double>(colTicks) / freq);
    profile.boundaryTimeMs = static_cast<float>(static_cast<double>(boundTicks) / freq);
    profile.totalKineticEnergy = CalculateTotalKineticEnergy();
    profile.threadCount = threadPool ? threadPool->threadCount() : 1;
}

void UnitManager::UpdatePositions(float dt) {
    const float maxSpd = maxSpeed;
    const float maxSpdSq = maxSpd * maxSpd;
    const bool vfEnabled = enableVectorField;
    const float vfSpeed = vectorFieldSpeed;
    const float vfSteer = vectorFieldSteeringStrength;
    const VectorField* const vfPtr = &vectorField;

    glm::vec2* const posPtr = positions.data();
    glm::vec2* const velPtr = velocities.data();
    const size_t count = CurrentUnits;

    auto updateRange = [=](size_t start, size_t end, size_t /*threadIdx*/) {
        for (size_t i = start; i < end; ++i) {
            float vx = velPtr[i].x;
            float vy = velPtr[i].y;
            const float px = posPtr[i].x;
            const float py = posPtr[i].y;

            if (vfEnabled) {
                const glm::vec2 flowDir = vfPtr->sample(glm::vec2(px, py));
                if (flowDir.x != 0.0f || flowDir.y != 0.0f) {
                    const float targetVx = flowDir.x * vfSpeed;
                    const float targetVy = flowDir.y * vfSpeed;
                    const float steerFactor = std::min(1.0f, vfSteer * dt);
                    vx += (targetVx - vx) * steerFactor;
                    vy += (targetVy - vy) * steerFactor;
                }
            }

            const float speedSq = vx * vx + vy * vy;
            if (speedSq > maxSpdSq) {
                const float scale = maxSpd / std::sqrt(speedSq);
                vx *= scale;
                vy *= scale;
            }
            velPtr[i].x = vx;
            velPtr[i].y = vy;
            posPtr[i].x = px + vx * dt;
            posPtr[i].y = py + vy * dt;
        }
    };

    if (threadPool && count > 512) {
        threadPool->parallelFor(0, count, updateRange, 512);
    } else {
        updateRange(0, count, 0);
    }
}

void UnitManager::SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM, float size) {
    if (CurrentUnits >= MaxUnits) {
        return;
    }
    positions[CurrentUnits] = pos;
    velocities[CurrentUnits] = vel;
    health[CurrentUnits] = hp;
    invMass[CurrentUnits] = invM;
    sizes[CurrentUnits] = size;
    cramCount[CurrentUnits] = 0;

    CurrentUnits++;
}

size_t UnitManager::GetCurrentUnits() const {
    return CurrentUnits;
}

size_t UnitManager::GetMaxUnits() const {
    return MaxUnits;
}

glm::vec2 UnitManager::GetPosition(size_t index) const {
    return positions[index];
}

const glm::vec2* UnitManager::GetPositionsPtr() const {
    return positions.data();
}

float UnitManager::GetSize(size_t index) const {
    return sizes[index];
}

const float* UnitManager::GetSizesPtr() const {
    return sizes.data();
}

float UnitManager::CalculateTotalKineticEnergy() const {
    const glm::vec2* const velPtr = velocities.data();
    const float* const invMassPtr = invMass.data();
    const size_t count = CurrentUnits;
    if (count == 0) {
        return 0.0f;
    }

    const size_t numThreads = threadPool ? threadPool->threadCount() : 1;
    std::vector<float> threadEnergy(numThreads, 0.0f);

    auto calcRange = [&](size_t start, size_t end, size_t threadIdx) {
        float localKe = 0.0f;
        for (size_t i = start; i < end; ++i) {
            const float invM = invMassPtr[i];
            if (invM > 0.0f) {
                const float vx = velPtr[i].x;
                const float vy = velPtr[i].y;
                const float vSq = vx * vx + vy * vy;
                localKe += 0.5f * (1.0f / invM) * vSq;
            }
        }
        if (threadIdx < threadEnergy.size()) {
            threadEnergy[threadIdx] += localKe;
        }
    };

    if (threadPool && count > 1024) {
        threadPool->parallelFor(0, count, calcRange, 1024);
    } else {
        calcRange(0, count, 0);
    }

    float totalKe = 0.0f;
    for (float ke : threadEnergy) {
        totalKe += ke;
    }
    return totalKe;
}

const UnitManagerProfile& UnitManager::GetProfile() const {
    return profile;
}
