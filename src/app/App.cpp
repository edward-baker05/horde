#include "app/App.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>

#include "core/Paths.hpp"
#include "gfx/Camera2D.hpp"
#include "gfx/ShaderLoader.hpp"
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
    if (!m_gpu.init(m_window, m_config.gpuDebug)) {
        return false;
    }

    // swap chain stuff, I presume methods to update displayed window from frame buffer
    SDL_SetGPUSwapchainParameters(m_gpu.device(), m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  m_config.vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_MAILBOX);

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

    m_scenes.emplace(services);
    m_scenes->replace(std::make_unique<scene::MainMenuScene>());

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
        // Clamped so a hitch or a dragged window does not teleport the
        // simulation on the next frame.
        // TODO: This will need changing with more complex stuff, likely going to be moved to Logic/sim
        const float deltaTime = std::min(static_cast<float>(now - m_lastTicks) / 1.0e9f, 0.1f);
        m_lastTicks = now;

        //? handles inputs maybe?
        pumpEvents();

        if (!m_running) {
            break;
        }

        // App general update func
        update(deltaTime);
        render();

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
        active->update(deltaTime);
        active->debugUi();
    }
}

void App::render() {
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_gpu.device());

    if (commands == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        m_imgui.discardFrame();
        return;
    }

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 swapchainWidth = 0;
    Uint32 swapchainHeight = 0;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, m_window, &swapchain, &swapchainWidth, &swapchainHeight)) {
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
        active->compute(commands);
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
        }

        m_imgui.draw(commands, pass);
        SDL_EndGPURenderPass(pass);
    }

    SDL_SubmitGPUCommandBuffer(commands);
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
