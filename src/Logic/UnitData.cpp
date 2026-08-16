//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

UnitManager::UnitManager(const size_t MaxUnits) : MaxUnits(MaxUnits) {
    // does this once at startup
    Reserve(MaxUnits);
}

void UnitManager::Reserve(size_t newMaxUnits) {
    MaxUnits = newMaxUnits;
    positions.resize(newMaxUnits);
    velocities.resize(newMaxUnits);
    health.resize(newMaxUnits);
    invMass.resize(newMaxUnits);
}

void UnitManager::UpdatePhysics(float dt) {
    UpdatePositions(dt);
}

void UnitManager::UpdatePositions(float dt) {
    for (size_t i = 0; i < CurrentUnits; ++i) {
        positions[i] += velocities[i] * dt;
    }
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