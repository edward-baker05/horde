#include "ui/ImGuiLayer.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "core/Paths.hpp"
#include "gfx/GpuContext.hpp"

namespace horde::ui {

ImGuiLayer::~ImGuiLayer() {
    shutdown();
}

bool ImGuiLayer::init(gfx::GpuContext& gpu) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Keyboard nav is deliberately NOT enabled. It auto-focuses a widget on the
    // first frame, so a stray key press reaching the window activates whatever
    // button happens to be focused — which for a game means gameplay keys
    // pressing menu buttons. Enable it only for tool panels that need it.

    ImGui::StyleColorsDark();

    // The bundled Liberation Sans, so tool panels look the same on every
    // machine rather than depending on ImGui's built-in bitmap font.
    const std::string fontPath = paths::asset("fonts/LiberationSans-Regular.ttf").string();

    if (io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f) == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load '%s'; falling back to the built-in font",
                    fontPath.c_str());
        io.Fonts->AddFontDefault();
    }

    if (!ImGui_ImplSDL3_InitForSDLGPU(gpu.window())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }

    ImGui_ImplSDLGPU3_InitInfo info{};
    info.Device = gpu.device();
    info.ColorTargetFormat = gpu.swapchainFormat();
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui_ImplSDLGPU3_Init failed");
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    m_initialised = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_initialised) {
        return;
    }

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    m_initialised = false;
    m_frameStarted = false;
    m_drawData = nullptr;
}

bool ImGuiLayer::handleEvent(const SDL_Event& event) {
    if (!m_initialised) {
        return false;
    }

    ImGui_ImplSDL3_ProcessEvent(&event);

    const ImGuiIO& io = ImGui::GetIO();

    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
            return io.WantCaptureMouse;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return io.WantCaptureKeyboard;

        default:
            return false;
    }
}

void ImGuiLayer::beginFrame() {
    if (!m_initialised) {
        return;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    m_frameStarted = true;
}

void ImGuiLayer::prepare(SDL_GPUCommandBuffer* commands) {
    if (!m_frameStarted) {
        return;
    }

    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

    m_drawData = drawData;
    m_frameStarted = false;
}

void ImGuiLayer::discardFrame() {
    if (!m_frameStarted) {
        return;
    }

    ImGui::EndFrame();
    m_frameStarted = false;
    m_drawData = nullptr;
}

void ImGuiLayer::draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
    if (m_drawData == nullptr) {
        return;
    }

    ImGui_ImplSDLGPU3_RenderDrawData(static_cast<ImDrawData*>(m_drawData), commands, pass);
}

} // namespace horde::ui
