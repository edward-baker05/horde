#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace horde::gfx {
class GpuContext;
}

namespace horde::ui {

// Dear ImGui on the SDL3 + SDL_GPU backends.
//
// Scoped to developer tooling — inspectors, timings, shader reload. Game-facing
// UI (menus, the tech tree) is drawn with the sprite renderer instead, because
// it needs sprites, a world camera, and pan/zoom that ImGui does not provide.
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool init(gfx::GpuContext& gpu);
    void shutdown();

    // Returns true if ImGui consumed the event and the game should ignore it.
    bool handleEvent(const SDL_Event& event);

    void beginFrame();

    // Ends the ImGui frame and uploads its vertex data. MUST be called on the
    // frame's command buffer BEFORE the render pass that draws ImGui — the
    // SDL_GPU backend requires it and silently renders nothing otherwise.
    void prepare(SDL_GPUCommandBuffer* commands);

    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass);

    // Ends the frame without drawing it. Must be called if a frame is begun but
    // never reaches prepare() — for example when the swapchain is unavailable
    // because the window is minimised. Leaving a frame open corrupts ImGui's
    // internal state and makes widgets fire spuriously on the next frame.
    void discardFrame();

private:
    bool m_initialised = false;
    bool m_frameStarted = false;
    void* m_drawData = nullptr;
};

} // namespace horde::ui
