#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace horde::gfx {
class Camera2D;
class GpuContext;
class ShaderLoader;
class SpriteBatch;
class Texture;
} // namespace horde::gfx

namespace horde::scene {

class SceneStack;

// Where the time in a frame goes, averaged over the last half second (with the
// worst frame alongside), so scenes can show what a frame actually costs even
// when a frame cap hides it from the frame rate. Filled in by App.
struct FrameStats {
    float frameMs = 0.0f;      // frame start to next frame start: 1000 / fps
    float frameMaxMs = 0.0f;
    float updateMs = 0.0f;     // CPU: the scene's update(), i.e. CPU-side physics
    float recordMs = 0.0f;     // CPU: building and submitting the frame's GPU work, waits excluded
    float simGpuMs = 0.0f;     // GPU: the scene's compute() work alone, i.e. GPU-side physics
    float simGpuMaxMs = 0.0f;
    float waitMs = 0.0f;       // blocked on the display, frame cap or a GPU still busy with earlier frames
    float otherMs = 0.0f;      // the rest: events, ImGui, scene transitions
    bool simGpuMeasured = false;
};

// Everything a scene is allowed to reach for. Passed by reference so scenes
// stay testable and never reach back into App directly.
struct Services {
    gfx::GpuContext* gpu = nullptr;
    gfx::ShaderLoader* shaders = nullptr;
    gfx::Texture* atlas = nullptr; // shared sprite atlas
    SDL_Window* window = nullptr;
    SceneStack* scenes = nullptr;
    const FrameStats* frameStats = nullptr;
    // When set, App submits the scene's compute work on its own and waits for
    // it each frame, which is what makes FrameStats::simGpuMs exact. Costs a
    // little CPU/GPU overlap.
    bool* measureSimGpu = nullptr;
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

    // Direct drawing into the render pass (e.g. custom pipelines / GPU instancing).
    // Called after SpriteBatch has flushed its draws.
    virtual void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
        (void)commands;
        (void)pass;
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
