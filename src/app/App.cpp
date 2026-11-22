#include "app/App.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <vector>

#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "gfx/Camera2D.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/LevelScene.hpp"
#include "scene/MainMenuScene.hpp"
#include "ui/FrameStatsOverlay.hpp"
#include "ui/ScreenInput.hpp"
#include "ui/ShellState.hpp"

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

    // Game-facing UI (menus); ImGui above stays for developer tooling.
    if (!m_rml.init(m_gpu, *m_shaders)) {
        return false;
    }

    if (!m_shell.init(m_gpu, *m_shaders) || !m_compositeBatch.init(m_gpu.device(), *m_shaders, m_gpu.swapchainFormat())) {
        return false;
    }
    relayout();

    // allocated services to a scene
    scene::Services services;
    services.gpu = &m_gpu;
    services.shaders = m_shaders.get();
    services.atlas = &m_atlas;
    services.ui = &m_rml;
    services.shell = &m_shell;
    // No music or cow in benchmarks; without a sound device the game runs silent.
    if (!m_config.bench && m_music.init()) {
        services.music = &m_music;
    }
    services.dancingCow = !m_config.bench;
    // Benchmarks decide for themselves whether to sync the GPU.
    if (m_config.bench) {
        m_measureSimGpu = m_config.bench->syncGpu;
    } else {
        m_gpuTimer.start(m_gpu.device());
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
    // Start with the first scene's device already in place rather than sliding in.
    updateShell(10.0f);
    m_scenes->notifyResize();

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

void App::relayout() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    m_rml.setContextSize(ui::DeviceKind::None, {width, height}, SDL_GetWindowDisplayScale(m_window));
    m_shell.layout({static_cast<float>(width), static_cast<float>(height)});
    m_phoneSize = glm::ivec2(-1);
    m_tabletSize = glm::ivec2(-1);
    syncDeviceSizes();
    if (m_scenes) {
        m_scenes->notifyResize();
    }
}

void App::syncDeviceSizes() {
    const glm::ivec2 phone = m_shell.screenSize(ui::DeviceKind::Phone);
    const glm::ivec2 tablet = m_shell.screenSize(ui::DeviceKind::Tablet);
    if (phone == m_phoneSize && tablet == m_tabletSize) {
        return;
    }
    const bool first = m_phoneSize.x < 0;
    m_phoneSize = phone;
    m_tabletSize = tablet;
    // The UI is authored in logical px; the scale makes it fill the screen texture.
    m_rml.setContextSize(ui::DeviceKind::Phone, phone, m_shell.rect(ui::DeviceKind::Phone).scale);
    m_rml.setContextSize(ui::DeviceKind::Tablet, tablet, m_shell.rect(ui::DeviceKind::Tablet).scale);
    // relayout() notifies the scenes itself; this covers a phone orientation change.
    if (!first && m_scenes) {
        m_scenes->notifyResize();
    }
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
    // Render time includes the swapchain wait and, in a benchmark, the timed
    // compute wait (m_lastGpuNs is 0 otherwise); what remains is the CPU
    // building and submitting the frame.
    const Uint64 waits = m_lastAcquireNs + m_lastPresentNs + m_lastGpuNs;
    const Uint64 record = m_lastRenderNs > waits ? m_lastRenderNs - waits : 0;

    m_statsWindow.frames++;
    m_statsWindow.total += frameNs;
    m_statsWindow.frame += frameNs;
    m_statsWindow.frameMax = std::max(m_statsWindow.frameMax, frameNs);
    m_statsWindow.update += m_lastSceneUpdateNs;
    m_statsWindow.record += record;
    m_statsWindow.wait += m_lastAcquireNs + m_lastPresentNs;
    // GPU physics times: the frame's own when a benchmark waited for it,
    // otherwise whatever GpuTimer has finished since the last frame. Those
    // trail by a frame or two, so they are averaged by sample, not by frame.
    m_gpuSamples.clear();
    if (m_config.bench) {
        if (m_measureSimGpu) {
            m_gpuSamples.push_back(m_lastGpuNs);
        }
    } else {
        m_gpuTimer.collect(m_gpuSamples);
    }
    if (m_measureSimGpu) {
        for (const Uint64 sample : m_gpuSamples) {
            m_statsWindow.gpu += sample;
            m_statsWindow.gpuMax = std::max(m_statsWindow.gpuMax, sample);
            m_statsWindow.gpuSamples++;
        }
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
    m_stats.simGpuMs = m_measureSimGpu && m_statsWindow.gpuSamples > 0
                           ? static_cast<float>(static_cast<double>(m_statsWindow.gpu) /
                                                static_cast<double>(m_statsWindow.gpuSamples) / 1.0e6)
                           : 0.0f;
    m_stats.simGpuMaxMs = m_measureSimGpu ? static_cast<float>(m_statsWindow.gpuMax / 1.0e6) : 0.0f;
    // A benchmark's GPU wait was part of the frame; GpuTimer's ran alongside it.
    const float gpuWaitMs = m_config.bench ? m_stats.simGpuMs : 0.0f;
    m_stats.otherMs =
        std::max(0.0f, m_stats.frameMs - m_stats.updateMs - m_stats.recordMs - m_stats.waitMs - gpuWaitMs);
    m_statsWindow = {};
}

void App::pumpEvents() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        // F3 toggles the frame stats in any scene, unless a text field has the keyboard.
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F3 && !event.key.repeat &&
            !ImGui::GetIO().WantTextInput) {
            m_showFrameStats = !frameStatsVisible();
            m_frameStatsChosen = true;
            continue;
        }
        // ImGui gets first refusal so that tool panels capture input before the
        // game camera sees it.
        const bool consumed = m_imgui.handleEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                m_running = false;
                continue;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                relayout();
                continue;
            }

            default:
                break;
        }

        if (consumed) {
            continue;
        }

        scene::Scene* active = m_scenes->active();
        const ui::DeviceKind kind = active != nullptr ? active->presentation().device : ui::DeviceKind::None;

        // Scenes on a device see mouse positions in its screen's pixels. SDL
        // reports window units, so scale to swapchain pixels before remapping.
        // Device-less scenes get the raw event, as RmlUi's None context scales
        // for itself.
        SDL_Event routed = event;
        if (kind != ui::DeviceKind::None) {
            ui::scaleMouseEvent(routed, SDL_GetWindowPixelDensity(m_window));
            // A press on the bezel or the background belongs to nobody.
            if (!m_shell.remapEvent(kind, routed)) {
                continue;
            }
        }

        // RmlUi next, so a menu under the pointer keeps clicks from the scene,
        // unless the scene is mid-drag and the pointer is its.
        const bool pointerEvent = event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                  event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL;
        const bool captured = pointerEvent && active != nullptr && active->capturesPointer();
        if (!captured && m_rml.handleEvent(kind, routed)) {
            continue;
        }

        if (active != nullptr) {
            active->handleEvent(routed);
        }
    }
}

void App::updateShell(float deltaTime) {
    m_presentations.clear();
    for (const std::unique_ptr<scene::Scene>& scene : m_scenes->scenes()) {
        m_presentations.push_back(scene->presentation());
    }
    m_shell.setTargets(ui::computeTargets(m_presentations));
    m_shell.update(deltaTime);
    syncDeviceSizes();
}

void App::update(float deltaTime) {
    m_imgui.beginFrame();

    // Devices follow the stack: slides start when a scene on the other device
    // is pushed or popped. Only the top scene updates; covered ones stay frozen.
    updateShell(deltaTime);

    if (scene::Scene* active = m_scenes->active()) {
        const Uint64 start = SDL_GetTicksNS();
        active->update(deltaTime);
        m_lastSceneUpdateNs = SDL_GetTicksNS() - start;
        m_rml.update();
        active->debugUi();
    }

    if (frameStatsVisible()) {
        // Top-right: every other window's first-use position is near the top-left,
        // and this one takes clicks, so it must not sit over them.
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x - 8.0f, 8.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.55f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (ImGui::Begin(tr("Frame stats"), nullptr, flags)) {
            ui::drawFrameStats(m_stats, m_config.bench ? nullptr : &m_measureSimGpu);
        }
        ImGui::End();
    }
}

bool App::frameStatsVisible() const {
    if (m_frameStatsChosen) {
        return m_showFrameStats;
    }
    // Until F3 is pressed: on for full-window scenes, off on the devices.
    const scene::Scene* active = m_scenes->active();
    return active == nullptr || active->presentation().device == ui::DeviceKind::None;
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
        if (m_measureSimGpu && !m_config.bench) {
            // While playing: the compute work goes on a command buffer of its
            // own, after an empty one whose fence marks the GPU reaching it,
            // and GpuTimer times the gap between the two fences on its own
            // thread. Nothing here waits, so the CPU and GPU still overlap as
            // they do unmeasured.
            m_lastGpuNs = 0;
            SDL_GPUCommandBuffer* marker = SDL_AcquireGPUCommandBuffer(m_gpu.device());
            SDL_GPUFence* begin = marker != nullptr ? SDL_SubmitGPUCommandBufferAndAcquireFence(marker) : nullptr;
            SDL_GPUCommandBuffer* computeCommands = SDL_AcquireGPUCommandBuffer(m_gpu.device());
            if (computeCommands != nullptr) {
                active->compute(computeCommands);
                SDL_GPUFence* end = SDL_SubmitGPUCommandBufferAndAcquireFence(computeCommands);
                if (begin != nullptr && end != nullptr) {
                    m_gpuTimer.track(begin, end);
                    begin = nullptr;
                } else if (end != nullptr) {
                    SDL_ReleaseGPUFence(m_gpu.device(), end);
                }
            } else {
                active->compute(commands);
            }
            if (begin != nullptr) {
                SDL_ReleaseGPUFence(m_gpu.device(), begin);
            }
        } else if (m_measureSimGpu) {
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

    const ui::DeviceKind topDevice = active != nullptr ? active->presentation().device : ui::DeviceKind::None;
    // Behind full-window scenes and device screens: slate, or dark wine in Polish mode.
    const SDL_FColor backdrop =
        polish::enabled() ? SDL_FColor{0.29f, 0.03f, 0.08f, 1.0f} : SDL_FColor{0.06f, 0.07f, 0.09f, 1.0f};
    const std::array<ui::DeviceKind, 2> deviceKinds{ui::DeviceKind::Phone, ui::DeviceKind::Tablet};

    // Which device screens get drawn this frame: visible, with a scene on them.
    // Covered scenes are frozen but still show. A device slid fully off is skipped.
    std::array<scene::Scene*, 2> deviceScenes{};
    for (size_t i = 0; i < deviceKinds.size(); ++i) {
        // A device sliding out keeps its last frame: skipping it here also skips
        // its uploads, RmlUi record, screen pass and blur.
        if (m_shell.visible(deviceKinds[i]) && !m_shell.frozen(deviceKinds[i])) {
            deviceScenes[i] = m_scenes->topmostOn(deviceKinds[i]);
        }
    }

    // All uploads happen before any render pass: copy passes cannot nest in one.
    m_batch.begin();
    if (topDevice == ui::DeviceKind::None && active != nullptr) {
        active->render(m_batch);
    }
    m_batch.upload(commands);

    for (size_t i = 0; i < deviceKinds.size(); ++i) {
        if (deviceScenes[i] == nullptr) {
            continue;
        }
        gfx::SpriteBatch& deviceBatch = m_shell.batch(deviceKinds[i]);
        deviceBatch.begin();
        deviceScenes[i]->render(deviceBatch);
        deviceBatch.upload(commands);
    }

    // The composite batch is in window pixels, y down, like the scene cameras.
    const glm::vec2 windowPx = m_shell.windowPx();
    m_compositeBatch.begin();
    if (topDevice != ui::DeviceKind::None && polish::enabled()) {
        // Behind the devices in Polish mode: the flag, white over red.
        const glm::vec4 solid = gfx::atlasCell(1, 1, 4, 4);
        const glm::vec2 half{windowPx.x, windowPx.y * 0.5f};
        gfx::drawCentered(m_compositeBatch, m_atlas.handle(), {windowPx.x * 0.5f, windowPx.y * 0.25f}, half, 0.0f, solid,
                          {0.96f, 0.96f, 0.96f, 1.0f});
        gfx::drawCentered(m_compositeBatch, m_atlas.handle(), {windowPx.x * 0.5f, windowPx.y * 0.75f}, half, 0.0f, solid,
                          {0.86f, 0.08f, 0.24f, 1.0f});
    }
    m_shell.composite(m_compositeBatch, m_atlas.handle());
    m_compositeBatch.upload(commands);
    m_compositeCamera.setViewport(windowPx.x, windowPx.y);
    m_compositeCamera.setCenter(windowPx * 0.5f);
    m_compositeCamera.setZoom(1.0f);

    // ImGui's SDL_GPU backend is explicit about this: skipping PrepareDrawData
    // renders nothing, silently.
    m_imgui.prepare(commands);
    // RmlUi uploads geometry and glyphs while rendering, so it records here
    // and replays inside the pass.
    m_rml.beginFrame();
    for (size_t i = 0; i < deviceKinds.size(); ++i) {
        if (deviceScenes[i] != nullptr) {
            m_rml.record(deviceKinds[i], commands);
        }
    }
    m_rml.record(ui::DeviceKind::None, commands);

    // Each device's scene renders into its own screen texture.
    for (size_t i = 0; i < deviceKinds.size(); ++i) {
        scene::Scene* scene = deviceScenes[i];
        if (scene == nullptr) {
            continue;
        }
        gfx::RenderTarget& target = m_shell.screen(deviceKinds[i]);

        SDL_GPUColorTargetInfo screenTarget{};
        screenTarget.texture = target.texture();
        screenTarget.clear_color = backdrop;
        screenTarget.load_op = SDL_GPU_LOADOP_CLEAR;
        screenTarget.store_op = SDL_GPU_STOREOP_STORE;

        if (SDL_GPURenderPass* screenPass = SDL_BeginGPURenderPass(commands, &screenTarget, 1, nullptr)) {
            m_shell.markDrawn(deviceKinds[i]);
            scene->camera().setViewport(static_cast<float>(target.size().x), static_cast<float>(target.size().y));
            m_shell.batch(deviceKinds[i]).flush(commands, screenPass, scene->camera().viewProjection());
            scene->renderPass(commands, screenPass);
            m_rml.draw(deviceKinds[i], screenPass, commands);
            SDL_EndGPURenderPass(screenPass);
        }
    }

    // Background devices blur their screen texture. These are render passes, so
    // they follow every upload and the device passes that write what they read.
    for (size_t i = 0; i < deviceKinds.size(); ++i) {
        if (deviceScenes[i] != nullptr && m_shell.blurAmount(deviceKinds[i]) > 0.0f) {
            m_shell.applyBlur(deviceKinds[i], commands);
        }
    }

    // A scene without a device keeps its own background; otherwise the devices
    // sit on black, or on the flag drawn into the composite batch.
    const bool direct = topDevice == ui::DeviceKind::None;
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapchain;
    colorTarget.clear_color = direct ? backdrop : SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);

    if (pass != nullptr) {
        if (direct && active != nullptr) {
            active->camera().setViewport(static_cast<float>(swapchainWidth), static_cast<float>(swapchainHeight));
            m_batch.flush(commands, pass, active->camera().viewProjection());
            active->renderPass(commands, pass);
        }

        m_compositeBatch.flush(commands, pass, m_compositeCamera.viewProjection());
        // The window context last, so what it shows (the dancing cow) sits over
        // the devices as well as over device-less scenes.
        m_rml.draw(ui::DeviceKind::None, pass, commands);
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

    m_gpuTimer.stop();
    m_scenes.reset();
    m_music.shutdown();
    m_compositeBatch.shutdown();
    m_shell.shutdown();
    m_rml.shutdown();
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
