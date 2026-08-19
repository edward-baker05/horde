//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

#include <execution>
#include <algorithm>

// Constructor
UnitManager::UnitManager(
    const size_t MaxUnits,
    const glm::vec2 WorldBounds,
    const int unit_size) : unit_size(unit_size), MaxUnits(MaxUnits), WorldBounds(WorldBounds) {
    // does this once at startup
    Reserve(MaxUnits);
    constexpr glm::vec2 r(5);
    MaxP = WorldBounds - r;
}

void UnitManager::Reserve(size_t newMaxUnits) {
    MaxUnits = newMaxUnits;
    positions.resize(newMaxUnits);
    velocities.resize(newMaxUnits);
    health.resize(newMaxUnits);
    invMass.resize(newMaxUnits);
}

void UnitManager::SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM) {
    if (CurrentUnits >= MaxUnits) {
        return;
    }
    positions[CurrentUnits] = pos;
    velocities[CurrentUnits] = vel;
    health[CurrentUnits] = hp;
    invMass[CurrentUnits] = invM;

    CurrentUnits++;
}

// Main Loop
void UnitManager::UpdatePhysics(float dt) {
    UpdatePositions(dt);
    ResolveCollisions();
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < CurrentUnits; ++i) {
        positions[i] += velocities[i] * dt;
    }
}

void UnitManager::ResolveCollisions() {
    for (size_t i = 0; i < CurrentUnits; ++i) {
        glm::vec2& pos = positions[i];
        glm::vec2& vel = velocities[i];
        if ((pos.x > MaxP.x && vel.x > 0.0f)||(pos.x < WorldOrigin.x && vel.x < 0.0f)) vel.x *= Restitution;
        if ((pos.y > MaxP.y && vel.y > 0.0f)||(pos.y < WorldOrigin.y && vel.y < 0.0f)) vel.y *= Restitution;
        pos.x = std::clamp(pos.x, 0.0f, MaxP.x);
        pos.y = std::clamp(pos.y, 0.0f, MaxP.y);
    }
}


// Getters and Setters
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