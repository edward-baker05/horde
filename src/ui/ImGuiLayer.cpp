#include "ui/ImGuiLayer.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "gfx/GpuContext.hpp"

namespace horde::ui {

ImGuiLayer::~ImGuiLayer() {
    shutdown();
}

namespace {

ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// Polish mode: bialo-czerwona with Lowicz accents. Wine-dark panels, crimson
// title bars and controls, cream text, folk yellow, green and orange for the
// small things. Otherwise ImGui's stock dark style.
void applyStyle(bool polishMode) {
    ImGui::GetStyle() = ImGuiStyle();
    ImGui::StyleColorsDark();
    if (!polishMode) {
        return;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 2.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 2.0f;

    const ImVec4 cream = rgb(247, 240, 225);
    const ImVec4 wine = rgb(74, 8, 20);
    const ImVec4 wineLight = rgb(112, 14, 32);
    const ImVec4 crimson = rgb(200, 16, 46);
    const ImVec4 crimsonHot = rgb(226, 40, 70);
    const ImVec4 yellow = rgb(246, 190, 0);
    const ImVec4 green = rgb(31, 133, 66);
    const ImVec4 orange = rgb(240, 122, 20);
    const ImVec4 black = rgb(20, 16, 18);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = cream;
    c[ImGuiCol_TextDisabled] = rgb(214, 180, 170);
    c[ImGuiCol_WindowBg] = rgb(74, 8, 20, 0.96f);
    c[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_PopupBg] = rgb(60, 6, 16, 0.98f);
    c[ImGuiCol_Border] = black;
    c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_FrameBg] = wineLight;
    c[ImGuiCol_FrameBgHovered] = rgb(150, 18, 40);
    c[ImGuiCol_FrameBgActive] = crimson;
    c[ImGuiCol_TitleBg] = rgb(150, 12, 34);
    c[ImGuiCol_TitleBgActive] = crimson;
    c[ImGuiCol_TitleBgCollapsed] = rgb(150, 12, 34, 0.8f);
    c[ImGuiCol_MenuBarBg] = wineLight;
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0.2f);
    c[ImGuiCol_ScrollbarGrab] = crimson;
    c[ImGuiCol_ScrollbarGrabHovered] = crimsonHot;
    c[ImGuiCol_ScrollbarGrabActive] = yellow;
    c[ImGuiCol_CheckMark] = rgb(255, 255, 255);
    c[ImGuiCol_SliderGrab] = yellow;
    c[ImGuiCol_SliderGrabActive] = orange;
    c[ImGuiCol_Button] = crimson;
    c[ImGuiCol_ButtonHovered] = crimsonHot;
    c[ImGuiCol_ButtonActive] = orange;
    c[ImGuiCol_Header] = green;
    c[ImGuiCol_HeaderHovered] = rgb(46, 160, 86);
    c[ImGuiCol_HeaderActive] = rgb(60, 180, 100);
    c[ImGuiCol_Separator] = yellow;
    c[ImGuiCol_SeparatorHovered] = orange;
    c[ImGuiCol_SeparatorActive] = orange;
    c[ImGuiCol_ResizeGrip] = rgb(246, 190, 0, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = yellow;
    c[ImGuiCol_ResizeGripActive] = orange;
    c[ImGuiCol_Tab] = wineLight;
    c[ImGuiCol_TabHovered] = crimsonHot;
    c[ImGuiCol_TabSelected] = crimson;
    c[ImGuiCol_TabDimmed] = wine;
    c[ImGuiCol_TabDimmedSelected] = wineLight;
    c[ImGuiCol_DockingPreview] = rgb(246, 190, 0, 0.6f);
    c[ImGuiCol_PlotLines] = yellow;
    c[ImGuiCol_PlotHistogram] = orange;
    c[ImGuiCol_TableHeaderBg] = wineLight;
    c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.04f);
    c[ImGuiCol_TextSelectedBg] = rgb(194, 24, 111, 0.6f);
    c[ImGuiCol_DragDropTarget] = yellow;
    c[ImGuiCol_NavCursor] = yellow;
    c[ImGuiCol_ModalWindowDimBg] = rgb(20, 16, 18, 0.5f);
}

} // namespace

bool ImGuiLayer::init(gfx::GpuContext& gpu) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    m_polishStyle = polish::enabled();
    applyStyle(m_polishStyle);

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

    // Follows the settings toggle; styles can only change between frames.
    if (m_polishStyle != polish::enabled()) {
        m_polishStyle = polish::enabled();
        applyStyle(m_polishStyle);
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
