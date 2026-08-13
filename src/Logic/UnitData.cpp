//
// Created by Bailen on 13/08/2026.
//

#include "UnitData.hpp"

UnitManager::UnitManager(const size_t MaxUnits) : MaxUnits(MaxUnits) {
    // does this once at startup
    positions.resize(MaxUnits);
    velocities.resize(MaxUnits);
    health.resize(MaxUnits);
    invMass.resize(MaxUnits);
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

size_t UnitManager::GetCurrentUnits() const{
    return CurrentUnits;
}
glm::vec2 UnitManager::GetPosition(size_t index) const{
    return positions[index];
}
const glm::vec2* UnitManager::GetPositionsPtr() const{
    return positions.data();
}