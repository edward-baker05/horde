#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <optional>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/SceneStack.hpp"
#include "ui/ImGuiLayer.hpp"

namespace horde::app {

struct AppConfig {
    const char* title = "horde";
    int width = 1600;
    int height = 900;
    bool vsync = true;
    bool gpuDebug = true;
};

// Owns the window, the GPU device and the frame loop, and drives the scene
// stack. Everything below it is a plain object with no global state.
class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(const AppConfig& config);
    int run();
    void shutdown();

private:
    void pumpEvents();
    void update(float deltaTime);
    void render();

    AppConfig m_config;
    SDL_Window* m_window = nullptr;
    gfx::GpuContext m_gpu;
    std::unique_ptr<gfx::ShaderLoader> m_shaders;
    gfx::SpriteBatch m_batch;
    gfx::Texture m_atlas;
    ui::ImGuiLayer m_imgui;
    std::optional<scene::SceneStack> m_scenes;

    bool m_running = false;
    Uint64 m_lastTicks = 0;
};

} // namespace horde::app
