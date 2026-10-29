#include "app/App.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <vector>

#include "core/Paths.hpp"
#include "gfx/Camera2D.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/LevelScene.hpp"
#include "scene/MainMenuScene.hpp"

namespace horde::app {

App::~App() {
    shutdown();
}

// creating App init
bool App::init(const AppConfig& config) {
    m_config = config;

    // checking sdl can start drivers?
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // log graphics driver being used: Vulkan/DirectX/Metal
    SDL_Log("Video driver: %s", SDL_GetCurrentVideoDriver());

    // creating the window with config from App.hpp header file, @see AppConfig
    m_window = SDL_CreateWindow(m_config.title, m_config.width, m_config.height,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);

    // if m_window fails to complete properly, error out
    if (m_window == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    // m_gpu is a gfx::GpuContext object created in App.hpp header file, @see GpuContext
    if (!m_gpu.init(m_window, m_config.gpuDebug, m_config.lowPowerGpu)) {
        return false;
    }

    // swap chain stuff, I presume methods to update displayed window from frame buffer
    // Without vsync, take whichever uncapped mode the backend has; VSYNC is the
    // only one SDL guarantees. IMMEDIATE first: on D3D12, MAILBOX still caps at
    // the display's refresh rate, which hides differences in a benchmark.
    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!m_config.vsync) {
        if (SDL_WindowSupportsGPUPresentMode(m_gpu.device(), m_window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
            presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            m_presentMode = "immediate";
        } else if (SDL_WindowSupportsGPUPresentMode(m_gpu.device(), m_window, SDL_GPU_PRESENTMODE_MAILBOX)) {
            presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
            m_presentMode = "mailbox";
        } else {
            SDL_Log("No uncapped present mode available; staying on vsync");
        }
    }
    SDL_SetGPUSwapchainParameters(m_gpu.device(), m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);

    // honestly no clue what this is doing, I think its assigning the gpu to @see ShaderLoader
    m_shaders = std::make_unique<gfx::ShaderLoader>(m_gpu.device());

    // m_batch is a gfx::SpriteBatch object, I think it's for batching sprite draw calls and atlas stuff
    if (!m_batch.init(m_gpu.device(), *m_shaders, m_gpu.swapchainFormat())) {
        return false;
    }
    // loads sprite atlas. I think we'll be using one large atlas at runtime, so I'd like to make
    // a tool for creating the atlas from a set of sprite textures.
    if (!m_atlas.loadFromFile(m_gpu.device(), paths::asset("textures/atlas.png"))) {
        return false;
    }
    // idk. TODO: figure this out
    if (!m_imgui.init(m_gpu)) {
        return false;
    }

    // allocated services to a scene
    scene::Services services;
    services.gpu = &m_gpu;
    services.shaders = m_shaders.get();
    services.atlas = &m_atlas;
    services.window = m_window;
    services.frameStats = &m_stats;
    services.measureSimGpu = &m_measureSimGpu;
    // Benchmarks decide for themselves whether to sync the GPU.
    if (m_config.bench) {
        m_measureSimGpu = m_config.bench->syncGpu;
    }

    m_scenes.emplace(services);
    if (m_config.bench) {
        m_recorder.emplace(*m_config.bench);
        m_scenes->replace(std::make_unique<scene::LevelScene>(m_config.bench->level));
    } else {
        m_scenes->replace(std::make_unique<scene::MainMenuScene>());
        // Over the menu, so "Back to menu" still has somewhere to go.
        if (m_config.startLevel) {
            m_scenes->push(std::make_unique<scene::LevelScene>(*m_config.startLevel));
        }
    }

    if (!m_scenes->applyPending()) {
        return false;
    }

    m_running = true;
    m_lastTicks = SDL_GetTicksNS();
    return true;
}

int App::run() {
    while (m_running) {
        const Uint64 now = SDL_GetTicksNS();
        const Uint64 frameNs = now - m_lastTicks;
        // Clamped so a hitch or a dragged window does not teleport the
        // simulation on the next frame.
        // TODO: This will need changing with more complex stuff, likely going to be moved to Logic/sim
        float deltaTime = std::min(static_cast<float>(frameNs) / 1.0e9f, 0.01666f);
        m_lastTicks = now;

        if (m_haveStats) {
            accumulateStats(frameNs);
        }
        m_haveStats = true;

        if (m_recorder) {
            // The previous iteration has now fully completed, so its sample is whole.
            if (m_haveFrame && !m_recorder->record({frameNs, m_lastUpdateNs, m_lastRenderNs, m_lastAcquireNs, m_lastGpuNs, m_lastPresentNs})) {
                return finishBenchmark();
            }
            m_haveFrame = true;
            if (m_recorder->config().fixedDt > 0.0f) {
                deltaTime = m_recorder->config().fixedDt;
            }
        }

        //? handles inputs maybe?
        pumpEvents();

        if (!m_running) {
            break;
        }

        // App general update func
        const Uint64 updateStart = SDL_GetTicksNS();
        update(deltaTime);
        const Uint64 renderStart = SDL_GetTicksNS();
        render();
        m_lastUpdateNs = renderStart - updateStart;
        m_lastRenderNs = SDL_GetTicksNS() - renderStart;

        if (!m_scenes->applyPending()) {
            return 1;
        }

        // Every scene popped means there is nothing left to show.
        if (m_scenes->empty()) {
            m_running = false;
        }
    }

    return 0;
}

int App::finishBenchmark() {
    bench::RunInfo info;
    info.videoDriver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
    info.gpuDriver = m_gpu.driverName() ? m_gpu.driverName() : "";
    info.gpuName = m_gpu.deviceName();
    info.shaderFormat = m_shaders->formatName();
    info.presentMode = m_presentMode;
    SDL_GetWindowSizeInPixels(m_window, &info.windowWidth, &info.windowHeight);

    // The benchmark always runs a LevelScene; measure where its units ended up.
    if (auto* level = dynamic_cast<scene::LevelScene*>(m_scenes->active())) {
        std::vector<BaseUnitData> units;
        if (level->physics().readUnits(units)) {
            info.quality = bench::measureCollisions(units, level->unitSize(), level->level().size, &level->walls());
            if (!m_recorder->config().unitsPath.empty()) {
                bench::writeUnitsCsv(units, level->unitSize(), m_recorder->config().unitsPath);
            }
        }
        std::vector<float> health;
        if (level->physics().readHealth(health) && !health.empty()) {
            double sum = 0.0;
            info.healthLowest = health.front();
            for (const float hp : health) {
                sum += hp;
                info.healthLowest = std::min<double>(info.healthLowest, hp);
            }
            info.healthMean = sum / static_cast<double>(health.size());
            info.healthMeasured = true;
        }
        if (const sim::CollisionSettings* collision = level->physics().collisionSettings()) {
            info.collision = *collision;
        }
        info.wallCount = static_cast<std::uint32_t>(level->level().walls.size());
        info.wallPrimitives = static_cast<std::uint32_t>(level->walls().primitives().size());
    }

    m_running = false;
    return m_recorder->finish(info) ? 0 : 3;
}

void App::accumulateStats(Uint64 frameNs) {
    // Render time includes the swapchain wait and the timed compute wait;
    // what remains is the CPU building and submitting the frame.
    const Uint64 waits = m_lastAcquireNs + m_lastPresentNs + (m_measureSimGpu ? m_lastGpuNs : 0);
    const Uint64 record = m_lastRenderNs > waits ? m_lastRenderNs - waits : 0;

    m_statsWindow.frames++;
    m_statsWindow.total += frameNs;
    m_statsWindow.frame += frameNs;
    m_statsWindow.frameMax = std::max(m_statsWindow.frameMax, frameNs);
    m_statsWindow.update += m_lastSceneUpdateNs;
    m_statsWindow.record += record;
    m_statsWindow.wait += m_lastAcquireNs + m_lastPresentNs;
    if (m_measureSimGpu) {
        m_statsWindow.gpu += m_lastGpuNs;
        m_statsWindow.gpuMax = std::max(m_statsWindow.gpuMax, m_lastGpuNs);
    }

    // Publish twice a second: steady enough to read.
    if (m_statsWindow.total < 500'000'000) {
        return;
    }

    const auto ms = [&](Uint64 sum) {
        return static_cast<float>(static_cast<double>(sum) / static_cast<double>(m_statsWindow.frames) / 1.0e6);
    };
    m_stats.frameMs = ms(m_statsWindow.frame);
    m_stats.frameMaxMs = static_cast<float>(m_statsWindow.frameMax / 1.0e6);
    m_stats.updateMs = ms(m_statsWindow.update);
    m_stats.recordMs = ms(m_statsWindow.record);
    m_stats.waitMs = ms(m_statsWindow.wait);
    m_stats.simGpuMeasured = m_measureSimGpu;
    m_stats.simGpuMs = m_measureSimGpu ? ms(m_statsWindow.gpu) : 0.0f;
    m_stats.simGpuMaxMs = m_measureSimGpu ? static_cast<float>(m_statsWindow.gpuMax / 1.0e6) : 0.0f;
    m_stats.otherMs = std::max(0.0f, m_stats.frameMs - m_stats.updateMs - m_stats.recordMs - m_stats.waitMs -
                                         m_stats.simGpuMs);
    m_statsWindow = {};
}

void App::pumpEvents() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        // ImGui gets first refusal so that tool panels capture input before the
        // game camera sees it.
        const bool consumed = m_imgui.handleEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                continue;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                const float width = static_cast<float>(event.window.data1);
                const float height = static_cast<float>(event.window.data2);

                if (scene::Scene* active = m_scenes->active()) {
                    active->onResize(width, height);
                }
                continue;
            }

            default:
                break;
        }

        if (consumed) {
            continue;
        }

        if (scene::Scene* active = m_scenes->active()) {
            active->handleEvent(event);
        }
    }
}

void App::update(float deltaTime) {
    m_imgui.beginFrame();

    if (scene::Scene* active = m_scenes->active()) {
        const Uint64 start = SDL_GetTicksNS();
        active->update(deltaTime);
        m_lastSceneUpdateNs = SDL_GetTicksNS() - start;
        active->debugUi();
    }
}

void App::render() {
    m_lastPresentNs = 0;
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_gpu.device());

    if (commands == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        m_imgui.discardFrame();
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 swapchainWidth = 0;
    Uint32 swapchainHeight = 0;

    const Uint64 acquireStart = SDL_GetTicksNS();
    const bool acquired =
        SDL_WaitAndAcquireGPUSwapchainTexture(commands, m_window, &swapchain, &swapchainWidth, &swapchainHeight);
    m_lastAcquireNs = SDL_GetTicksNS() - acquireStart;

    if (!acquired) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commands);
        m_imgui.discardFrame();
        return;
    }

    // Minimised, or the swapchain is being rebuilt: nothing to draw, but the
    // command buffer must still be submitted and the ImGui frame closed.
    if (swapchain == nullptr) {
        SDL_SubmitGPUCommandBuffer(commands);
        m_imgui.discardFrame();
        return;
    }

    scene::Scene* active = m_scenes->active();

    // Compute runs first and outside the render pass — SDL_GPU passes cannot
    // nest, and the graphics pass samples what compute writes.
    if (active != nullptr) {
        if (m_measureSimGpu) {
            // Benchmarks put the scene's compute work on a command buffer of
            // its own and wait for it, which times the simulation's GPU cost
            // alone: no rendering, no present, no frame-rate cap. The same
            // queue runs it before the frame's render pass.
            const Uint64 submitted = SDL_GetTicksNS();
            SDL_GPUCommandBuffer* computeCommands = SDL_AcquireGPUCommandBuffer(m_gpu.device());
            if (computeCommands != nullptr) {
                active->compute(computeCommands);
                if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(computeCommands)) {
                    SDL_WaitForGPUFences(m_gpu.device(), true, &fence, 1);
                    SDL_ReleaseGPUFence(m_gpu.device(), fence);
                }
            }
            m_lastGpuNs = SDL_GetTicksNS() - submitted;
        } else {
            m_lastGpuNs = 0;
            active->compute(commands);
        }
    }

    m_batch.begin();

    if (active != nullptr) {
        active->render(m_batch);
    }

    // Both of these upload vertex data through copy passes and so must happen
    // before the render pass begins. ImGui's SDL_GPU backend is explicit about
    // this: skipping PrepareDrawData renders nothing, silently.
    m_batch.upload(commands);
    m_imgui.prepare(commands);

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = SDL_FColor{0.06f, 0.07f, 0.09f, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);

    if (pass != nullptr) {
        if (active != nullptr) {
            active->camera().setViewport(static_cast<float>(swapchainWidth), static_cast<float>(swapchainHeight));
            m_batch.flush(commands, pass, active->camera().viewProjection());
            active->renderPass(commands, pass);
        }

        m_imgui.draw(commands, pass);
        SDL_EndGPURenderPass(pass);
    }

    // Submitting the swapchain's command buffer presents the frame, and that is
    // where a driver frame limiter (NVIDIA's cap, Reflex) or a full present
    // queue sleeps, so it is timed as waiting rather than as work.
    const Uint64 presentStart = SDL_GetTicksNS();
    SDL_SubmitGPUCommandBuffer(commands);
    m_lastPresentNs = SDL_GetTicksNS() - presentStart;
}

void App::shutdown() {
    // Order matters: everything holding GPU resources must go before the
    // device, and the device before the window.
    if (m_gpu.device() != nullptr) {
        SDL_WaitForGPUIdle(m_gpu.device());
    }

    m_scenes.reset();
    m_imgui.shutdown();
    m_atlas.release();
    m_batch.shutdown();
    m_shaders.reset();
    m_gpu.shutdown();

    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    SDL_Quit();
}

} // namespace horde::app
