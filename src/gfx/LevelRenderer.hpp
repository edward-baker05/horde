#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/vec4.hpp>

#include "logic/Level.hpp"

namespace horde::gfx {

class SpriteBatch;

// Atlas cells, in the 4x4 grid laid out by tools/make_placeholder_atlas.py.
glm::vec4 cellSquare();   // (0,0) bordered square
glm::vec4 cellDisc();     // (1,0) filled disc
glm::vec4 cellTriangle(); // (2,0) isoceles triangle, apex toward -y
glm::vec4 cellSolid();    // (1,1) solid fill

// Converts a stored colour to the linear float vector the GPU wants, opaque.
glm::vec4 toFloatColor(logic::Rgb color);

// Queues a whole level: background, then walls in list order, then markers.
//
// Shared by the editor and by LevelScene so the two views of a level cannot
// drift apart. Draw order is submission order — there is no depth test.
void renderLevel(const logic::Level& level, SpriteBatch& batch, SDL_GPUTexture* atlas);

// Queues one wall. Exposed separately so the editor can draw previews and
// highlights with a modified colour without duplicating the per-kind logic.
void renderWall(const logic::Wall& wall, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color);

} // namespace horde::gfx
