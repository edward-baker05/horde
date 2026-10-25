#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <optional>

#include "bench/Benchmark.hpp"
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
    bool lowPowerGpu = false;
    // Boot straight into this level instead of stopping at the menu.
    std::optional<scene::LevelOptions> startLevel;
    // Run one benchmark and quit. See bench/Benchmark.hpp.
    std::optional<bench::BenchConfig> bench;
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
    int finishBenchmark();

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

    // Benchmark mode only.
    std::optional<bench::Recorder> m_recorder;
    const char* m_presentMode = "vsync";
    Uint64 m_lastUpdateNs = 0;
    Uint64 m_lastRenderNs = 0;
    Uint64 m_lastAcquireNs = 0;
    Uint64 m_lastGpuNs = 0;
    Uint64 m_lastPresentNs = 0;
    Uint64 m_lastSceneUpdateNs = 0;
    bool m_haveFrame = false;

    // Rolling frame cost, published to scenes through Services::frameStats.
    void accumulateStats(Uint64 frameNs);
    scene::FrameStats m_stats;
    bool m_measureSimGpu = true;
    bool m_haveStats = false;
    struct {
        Uint64 frames = 0, total = 0, frame = 0, frameMax = 0, update = 0, record = 0, gpu = 0, gpuMax = 0,
               wait = 0;
    } m_statsWindow;
};

} // namespace horde::app
