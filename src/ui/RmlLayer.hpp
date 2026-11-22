#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include <glm/glm.hpp>

#include <array>
#include <memory>

#include "ui/DeviceLayout.hpp"
#include "ui/RmlRenderer.hpp"
#include "ui/RmlSystem.hpp"

namespace Rml {
class Context;
}

namespace horde::gfx {
class GpuContext;
class ShaderLoader;
} // namespace horde::gfx

namespace horde::ui {

// RmlUi's lifecycle: installs the SDL_GPU render and system interfaces, owns
// one context per device (None is the full window) and routes input and
// drawing to them. Game-facing UI lives here; ImGui stays for dev tooling.
class RmlLayer {
public:
    RmlLayer() = default;
    ~RmlLayer();

    RmlLayer(const RmlLayer&) = delete;
    RmlLayer& operator=(const RmlLayer&) = delete;

    bool init(gfx::GpuContext& gpu, const gfx::ShaderLoader& shaders);
    void shutdown();

    Rml::Context* context(DeviceKind kind) const;

    // `pixels` is the context's size in render-target pixels.
    void setContextSize(DeviceKind kind, glm::ivec2 pixels, float dpRatio);

    void update();

    // Once per frame before any record().
    void beginFrame();

    // record() runs Context::Render(), uploading through copy passes, so it
    // goes before the render pass; draw() replays the result inside it.
    void record(DeviceKind kind, SDL_GPUCommandBuffer* commands);
    void draw(DeviceKind kind, SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands);

    // Returns true if RmlUi consumed the event. For None, mouse positions are
    // converted from window units to pixels first.
    bool handleEvent(DeviceKind kind, const SDL_Event& event);

private:
    static constexpr int kKinds = 3;

    SDL_Window* m_window = nullptr;
    RmlSystem m_system;
    RmlRenderer m_renderer;
    std::array<Rml::Context*, kKinds> m_contexts{};
    std::array<glm::ivec2, kKinds> m_sizes{};
    bool m_initialised = false;
};

} // namespace horde::ui
