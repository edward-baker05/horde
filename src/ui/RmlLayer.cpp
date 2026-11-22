#include "ui/RmlLayer.hpp"

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include <utility>

#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "gfx/GpuContext.hpp"
#include "ui/RmlInput.hpp"
#include "ui/ScreenInput.hpp"

namespace horde::ui {

namespace {

constexpr const char* kContextNames[] = {"none", "phone", "tablet"};

// Transform funcs live in the context's shared type register, not the model,
// so they outlast RemoveDataModel. Registering once here keeps scenes that
// recreate their models from registering the same name again.
void registerTransforms(Rml::Context& context) {
    Rml::DataModelConstructor constructor = context.CreateDataModel("_transforms");
    if (!constructor) {
        return;
    }
    constructor.RegisterTransformFunc("tr", [](const Rml::VariantList& args) -> Rml::Variant {
        if (args.empty()) {
            return {};
        }
        return Rml::Variant(Rml::String(tr(args[0].Get<Rml::String>().c_str())));
    });
    context.RemoveDataModel("_transforms");
}

} // namespace

RmlLayer::~RmlLayer() {
    shutdown();
}

bool RmlLayer::init(gfx::GpuContext& gpu, const gfx::ShaderLoader& shaders) {
    m_window = gpu.window();

    if (!m_renderer.init(gpu.device(), shaders, gpu.swapchainFormat())) {
        return false;
    }

    Rml::SetSystemInterface(&m_system);
    Rml::SetRenderInterface(&m_renderer);
    if (!Rml::Initialise()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::Initialise failed");
        return false;
    }
    m_initialised = true;

    // Also the fallback for glyphs another face lacks, such as Inter's missing ≡.
    if (!Rml::LoadFontFace(paths::asset("fonts/LiberationSans-Regular.ttf").string(), true)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi could not load the UI font");
        return false;
    }
    // Bold is optional: without it RmlUi falls back to the regular face.
    if (!Rml::LoadFontFace(paths::asset("fonts/LiberationSans-Bold.ttf").string())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RmlUi could not load the bold UI font");
    }
    // Inter, the phone and tablet system font. Weights are given, not read, so
    // font-weight 500 and 600 find Medium and SemiBold. Liberation Sans stands
    // in for any that fail to load.
    const std::pair<const char*, Rml::Style::FontWeight> interFaces[]{
        {"fonts/Inter-Regular.ttf", Rml::Style::FontWeight(400)},
        {"fonts/Inter-Medium.ttf", Rml::Style::FontWeight(500)},
        {"fonts/Inter-SemiBold.ttf", Rml::Style::FontWeight(600)},
        {"fonts/Inter-Bold.ttf", Rml::Style::FontWeight(700)},
    };
    for (const auto& [file, weight] : interFaces) {
        if (!Rml::LoadFontFace(paths::asset(file).string(), false, weight)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RmlUi could not load %s", file);
        }
    }
    // The "lucide" icon family; see ui/Icons.hpp.
    if (!Rml::LoadFontFace(paths::asset("fonts/Lucide.ttf").string())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RmlUi could not load the icon font");
    }

    // Phone and tablet get logical-pixel sizes until the shell resizes them.
    m_sizes[static_cast<int>(DeviceKind::None)] = {1, 1};
    m_sizes[static_cast<int>(DeviceKind::Phone)] = {390, 844};
    m_sizes[static_cast<int>(DeviceKind::Tablet)] = {1280, 800};

    for (int i = 0; i < kKinds; ++i) {
        m_contexts[i] = Rml::CreateContext(kContextNames[i], Rml::Vector2i(m_sizes[i].x, m_sizes[i].y));
        if (m_contexts[i] == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rml::CreateContext(%s) failed", kContextNames[i]);
            return false;
        }
        registerTransforms(*m_contexts[i]);
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    setContextSize(DeviceKind::None, {width, height}, SDL_GetWindowDisplayScale(m_window));
    return true;
}

void RmlLayer::shutdown() {
    if (m_initialised) {
        // Contexts are destroyed by Shutdown, which releases their GPU
        // resources through the renderer, so the renderer outlives it.
        Rml::Shutdown();
        m_initialised = false;
        m_contexts = {};
    }
    m_renderer.shutdown();
}

Rml::Context* RmlLayer::context(DeviceKind kind) const {
    return m_contexts[static_cast<int>(kind)];
}

void RmlLayer::setContextSize(DeviceKind kind, glm::ivec2 pixels, float dpRatio) {
    Rml::Context* ctx = context(kind);
    if (ctx == nullptr) {
        return;
    }
    m_sizes[static_cast<int>(kind)] = pixels;
    ctx->SetDimensions(Rml::Vector2i(pixels.x, pixels.y));
    ctx->SetDensityIndependentPixelRatio(dpRatio > 0.0f ? dpRatio : 1.0f);
}

void RmlLayer::update() {
    for (Rml::Context* ctx : m_contexts) {
        if (ctx != nullptr) {
            ctx->Update();
        }
    }
}

void RmlLayer::record(DeviceKind kind, SDL_GPUCommandBuffer* commands) {
    Rml::Context* ctx = context(kind);
    if (ctx == nullptr) {
        return;
    }
    m_renderer.beginRecord(kind, commands, m_sizes[static_cast<int>(kind)]);
    ctx->Render();
    m_renderer.endRecord();
}

void RmlLayer::beginFrame() {
    m_renderer.beginFrame();
}

void RmlLayer::draw(DeviceKind kind, SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands) {
    m_renderer.draw(kind, pass, commands);
}

bool RmlLayer::handleEvent(DeviceKind kind, const SDL_Event& event) {
    Rml::Context* ctx = context(kind);
    if (ctx == nullptr) {
        return false;
    }

    if (kind != DeviceKind::None) {
        return forwardToRml(*ctx, event);
    }

    // The None context is sized in swapchain pixels, but SDL reports the mouse
    // in window units; they differ on high-DPI displays.
    SDL_Event scaled = event;
    scaleMouseEvent(scaled, SDL_GetWindowPixelDensity(m_window));
    return forwardToRml(*ctx, scaled);
}

} // namespace horde::ui
