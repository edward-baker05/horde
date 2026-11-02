//
// Created by Bailen on 13/08/2026.
//
#pragma once

#include <vector>
#define GLM_FORCE_INLINE
#include <glm/glm.hpp>
#include <SDL3/SDL_stdinc.h>

// Matches HLSL UnitData in unit.vert.hlsl and the compute shaders
struct BaseUnitData {
    glm::vec2 pos{0.0f, 0.0f};
    glm::vec2 vel{0.0f, 0.0f};
};

