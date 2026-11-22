#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <memory>
#include <optional>
#include <vector>

#include "audio/MusicPlayer.hpp"
#include "bench/Benchmark.hpp"
#include "gfx/GpuContext.hpp"
#include "gfx/Camera2D.hpp"
#include "gfx/GpuTimer.hpp"
#include "gfx/ShaderLoader.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/SceneStack.hpp"
#include "ui/DeviceShell.hpp"
#include "ui/ImGuiLayer.hpp"
#include "ui/RmlLayer.hpp"

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
    // Fits the devices to the window and tells RmlUi and the scenes the new sizes.
    void relayout();
    // Pushes the devices' screen sizes to RmlUi and the scenes if they changed.
    void syncDeviceSizes();
    // Points the devices at what the scene stack wants and advances their slides.
    void updateShell(float deltaTime);

    AppConfig m_config;
    SDL_Window* m_window = nullptr;
    gfx::GpuContext m_gpu;
    std::unique_ptr<gfx::ShaderLoader> m_shaders;
    gfx::SpriteBatch m_batch;
    gfx::Texture m_atlas;
    gfx::Texture m_weapons; // optional: empty if textures/weapons.png didn't load
    ui::ImGuiLayer m_imgui;
    ui::RmlLayer m_rml;
    audio::MusicPlayer m_music;
    // Phone and tablet screens; scenes on them draw offscreen and are composited
    // through m_compositeBatch, which is in window pixels.
    ui::DeviceShell m_shell;
    gfx::SpriteBatch m_compositeBatch;
    gfx::Camera2D m_compositeCamera;
    std::vector<ui::ScenePresentation> m_presentations;
    glm::ivec2 m_phoneSize{0};
    glm::ivec2 m_tabletSize{0};
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
    // F3 overlay: the default follows the top scene (on for full-window ones)
    // until the player presses F3, after which their choice stands.
    bool frameStatsVisible() const;
    bool m_showFrameStats = false;
    bool m_frameStatsChosen = false;
    bool m_measureSimGpu = true;
    bool m_haveStats = false;
    // GPU physics timing while playing: fences polled off the main thread, so
    // measuring costs the GPU nothing. Benchmarks wait for the work instead
    // (see render()), which isolates it exactly.
    gfx::GpuTimer m_gpuTimer;
    std::vector<Uint64> m_gpuSamples;
    struct {
        Uint64 frames = 0, total = 0, frame = 0, frameMax = 0, update = 0, record = 0, gpu = 0, gpuMax = 0,
               gpuSamples = 0, wait = 0;
    } m_statsWindow;
};

} // namespace horde::app
