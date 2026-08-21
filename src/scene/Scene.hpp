#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>
#include <glm/mat4x4.hpp>

namespace horde::gfx {
class Camera2D;
class GpuContext;
class ShaderLoader;
class SpriteBatch;
class Texture;
} // namespace horde::gfx

namespace horde::scene {

class SceneStack;

// Everything a scene is allowed to reach for. Passed by reference so scenes
// stay testable and never reach back into App directly.
struct Services {
    gfx::GpuContext* gpu = nullptr;
    gfx::ShaderLoader* shaders = nullptr;
    gfx::Texture* atlas = nullptr; // shared sprite atlas
    SDL_Window* window = nullptr;
    SceneStack* scenes = nullptr;
};

// A distinct view of the game: the level, the tech tree, the main menu.
//
// Scenes are drawn through the shared SpriteBatch rather than owning their own
// renderer, which is what lets the tech tree reuse the world renderer's sprite
// path and camera wholesale.
class Scene {
public:
    virtual ~Scene() = default;

    // One-time GPU setup. Return false to abort startup.
    virtual bool onEnter(Services& services) {
        (void)services;
        return true;
    }

    virtual void onExit() {}

    // Return true if the event was consumed and should not propagate.
    virtual bool handleEvent(const SDL_Event& event) {
        (void)event;
        return false;
    }

    virtual void update(float deltaTime) {
        (void)deltaTime;
    }

    // Compute dispatches. Called on the frame's command buffer BEFORE the
    // render pass begins, since compute and render passes cannot be nested.
    virtual void compute(SDL_GPUCommandBuffer* commands) {
        (void)commands;
    }

    // Queue sprites for this frame. The batch has already been begun.
    virtual void render(gfx::SpriteBatch& batch) {
        (void)batch;
    }

    // Direct GPU rendering calls inside the active render pass.
    virtual void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection) {
        (void)commands;
        (void)pass;
        (void)viewProjection;
    }

    // Dear ImGui calls for developer tooling. Game-facing UI belongs in
    // render() as sprites.
    virtual void debugUi() {}

    // Called on window resize.
    virtual void onResize(float width, float height) {
        (void)width;
        (void)height;
    }

    // Camera used to draw this scene.
    virtual gfx::Camera2D& camera() = 0;

    virtual const char* name() const = 0;
};

} // namespace horde::scene
