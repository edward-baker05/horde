#include "ui/DeviceShell.hpp"

#include <SDL3/SDL_log.h>

#include <glm/common.hpp>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "ui/ScreenInput.hpp"

namespace horde::ui {

namespace {

// A fully blurred (background) device is tinted to 1 - this, so it recedes.
constexpr float kBlurDim = 0.35f;

// Matches `DeviceUniforms` in shaders/src/device.frag.hlsl.
struct DeviceUniforms {
    glm::vec2 windowSize{0.0f};
    glm::vec2 screenOrigin{0.0f};
    glm::vec2 screenPx{0.0f};
    float scale = 1.0f;
    float kind = 0.0f;
    float bezel = 0.0f;
    float screenRadius = 0.0f;
    float landscape = 0.0f;
    float blur = 0.0f;
    float dim = 1.0f;
    float padding[3]{};
};

static_assert(sizeof(DeviceUniforms) == 64, "DeviceUniforms must match the HLSL cbuffer");

constexpr float kBackdropKind = 2.0f;
constexpr float kWindowSceneKind = 3.0f;

SDL_GPUSampler* makeSampler(SDL_GPUDevice* gpu, SDL_GPUFilter filter) {
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = filter;
    info.mag_filter = filter;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu, &info);
    if (sampler == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "DeviceShell: SDL_CreateGPUSampler failed: %s", SDL_GetError());
    }
    return sampler;
}

} // namespace

DeviceShell::~DeviceShell() {
    shutdown();
}

bool DeviceShell::init(gfx::GpuContext& gpu, const gfx::ShaderLoader& shaders) {
    m_gpu = gpu.device();
    m_format = gpu.swapchainFormat();
    m_phone.kind = DeviceKind::Phone;
    m_tablet.kind = DeviceKind::Tablet;
    if (!m_phone.batch.init(m_gpu, shaders, m_format) || !m_tablet.batch.init(m_gpu, shaders, m_format) ||
        !m_phone.blur.init(m_gpu, shaders, m_format) || !m_tablet.blur.init(m_gpu, shaders, m_format) ||
        !m_windowLayer.blur.init(m_gpu, shaders, m_format)) {
        return false;
    }

    SDL_GPUShader* vertex = shaders.loadGraphics("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, gfx::ShaderResources{});
    if (vertex == nullptr) {
        return false;
    }
    SDL_GPUShader* fragment = shaders.loadGraphics(
        "device.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        gfx::ShaderResources{.samplers = 2, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 1});
    if (fragment == nullptr) {
        SDL_ReleaseGPUShader(m_gpu, vertex);
        return false;
    }

    // The shader writes premultiplied alpha: everything outside a device's shadow is transparent.
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = m_format;
    colorTarget.blend_state.enable_blend = true;
    colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.target_info.num_color_targets = 1;
    info.target_info.color_target_descriptions = &colorTarget;
    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_gpu, &info);
    SDL_ReleaseGPUShader(m_gpu, vertex);
    SDL_ReleaseGPUShader(m_gpu, fragment);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "DeviceShell: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    m_nearest = makeSampler(m_gpu, SDL_GPU_FILTER_NEAREST);
    m_linear = makeSampler(m_gpu, SDL_GPU_FILTER_LINEAR);
    return m_nearest != nullptr && m_linear != nullptr;
}

void DeviceShell::shutdown() {
    for (Device* d : {&m_phone, &m_tablet}) {
        d->batch.shutdown();
        d->blur.shutdown();
        d->target.release();
    }
    m_windowLayer.blur.shutdown();
    m_windowLayer.target.release();
    if (m_gpu == nullptr) {
        return;
    }
    for (SDL_GPUSampler** sampler : {&m_nearest, &m_linear}) {
        if (*sampler != nullptr) {
            SDL_ReleaseGPUSampler(m_gpu, *sampler);
            *sampler = nullptr;
        }
    }
    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_gpu, m_pipeline);
        m_pipeline = nullptr;
    }
    m_gpu = nullptr;
}

void DeviceShell::layoutDevice(Device& d) {
    // The tablet is stored portrait-swapped, so ask for landscape to get 1280x800.
    d.spec = deviceSpec(d.kind, d.kind == DeviceKind::Phone ? m_phoneOrientation : Orientation::Landscape);
    d.rect = fitDevice(d.spec, m_windowPx);
    const glm::ivec2 before = d.target.size();
    d.target.resize(m_gpu, m_format, screenPixelSize(d.rect));
    if (d.target.size() != before) {
        d.drawn = false;
    }
    // Sized here so the blurred texture the composite refers to is the one the blur pass writes.
    d.blur.resize(d.target.size());
}

void DeviceShell::layout(glm::vec2 windowPx) {
    m_windowPx = windowPx;
    layoutDevice(m_phone);
    layoutDevice(m_tablet);
    const glm::ivec2 size = glm::max(glm::ivec2(glm::round(windowPx)), glm::ivec2(1));
    m_windowLayer.target.resize(m_gpu, m_format, size);
    m_windowLayer.blur.resize(size);
}

void DeviceShell::setTargets(const ShellTargets& targets) {
    m_phone.animator.setTarget(targets.phone);
    m_phone.placement = targets.phone;
    m_tablet.animator.setTarget(targets.tablet);
    m_tablet.placement = targets.tablet;

    if (targets.windowBehind != m_windowLayer.behind) {
        m_windowLayer.behind = targets.windowBehind;
        // Fades in from sharp each time; going away, the scene is drawn directly again.
        m_windowLayer.fade = Tween{0.0f, 1.0f, DeviceAnimator::blurSeconds, 0.0f};
    }

    if (targets.phoneOrientation != m_phoneOrientation) {
        m_phoneOrientation = targets.phoneOrientation;
        layoutDevice(m_phone);
    }
}

void DeviceShell::update(float dt) {
    m_phone.animator.update(dt);
    m_tablet.animator.update(dt);
    m_windowLayer.fade.update(dt);
}

bool DeviceShell::visible(DeviceKind kind) const {
    return kind != DeviceKind::None && device(kind).animator.visible();
}

glm::ivec2 DeviceShell::screenSize(DeviceKind kind) const {
    return kind == DeviceKind::None ? glm::ivec2(0) : screenPixelSize(device(kind).rect);
}

const DeviceRect& DeviceShell::rect(DeviceKind kind) const {
    return device(kind).rect;
}

gfx::RenderTarget& DeviceShell::screen(DeviceKind kind) {
    return device(kind).target;
}

gfx::SpriteBatch& DeviceShell::batch(DeviceKind kind) {
    return device(kind).batch;
}

bool DeviceShell::frozen(DeviceKind kind) const {
    if (kind == DeviceKind::None) {
        return false;
    }
    const Device& d = device(kind);
    return d.drawn && d.placement == Placement::Hidden && d.animator.visible();
}

void DeviceShell::markDrawn(DeviceKind kind) {
    if (kind != DeviceKind::None) {
        device(kind).drawn = true;
    }
}

float DeviceShell::blurAmount(DeviceKind kind) const {
    return kind == DeviceKind::None ? 0.0f : device(kind).animator.blur();
}

void DeviceShell::applyBlur(DeviceKind kind, SDL_GPUCommandBuffer* commands) {
    Device& d = device(kind);
    d.blur.apply(commands, d.target.texture(), d.target.size());
}

glm::vec2 DeviceShell::slideOffset(const Device& d) const {
    const float t = d.animator.offscreen();
    return d.kind == DeviceKind::Phone ? glm::vec2(0.0f, t * m_windowPx.y) : glm::vec2(t * m_windowPx.x, 0.0f);
}

bool DeviceShell::remapEvent(DeviceKind kind, SDL_Event& event) const {
    if (kind == DeviceKind::None) {
        return true;
    }
    const Device& d = device(kind);
    return remapMouseEvent(event, d.rect, slideOffset(d));
}

void DeviceShell::drawBackdrop(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const {
    if (m_pipeline == nullptr) {
        return;
    }
    DeviceUniforms uniforms;
    uniforms.windowSize = m_windowPx;
    uniforms.kind = kBackdropKind;
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    // The backdrop samples nothing, but every declared sampler must be bound.
    const SDL_GPUTextureSamplerBinding unused[2]{{m_phone.target.texture(), m_nearest},
                                                 {m_phone.target.texture(), m_linear}};
    if (unused[0].texture == nullptr) {
        return;
    }
    SDL_BindGPUFragmentSamplers(pass, 0, unused, 2);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void DeviceShell::applyWindowBlur(SDL_GPUCommandBuffer* commands) {
    m_windowLayer.blur.apply(commands, m_windowLayer.target.texture(), m_windowLayer.target.size());
}

void DeviceShell::drawWindowBackdrop(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const {
    if (m_pipeline == nullptr || m_windowLayer.target.texture() == nullptr ||
        m_windowLayer.blur.texture() == nullptr) {
        return;
    }
    const float blur = m_windowLayer.fade.value();
    DeviceUniforms uniforms;
    uniforms.windowSize = m_windowPx;
    uniforms.kind = kWindowSceneKind;
    uniforms.blur = blur;
    uniforms.dim = 1.0f - kBlurDim * blur;
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    const SDL_GPUTextureSamplerBinding bindings[2]{{m_windowLayer.target.texture(), m_nearest},
                                                   {m_windowLayer.blur.texture(), m_linear}};
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void DeviceShell::drawDevice(const Device& d, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const {
    // The texture is the rounded integer target, so it is drawn 1:1 (NEAREST
    // drops rows otherwise) with its top-left on a whole pixel.
    const glm::vec2 screen = glm::vec2(d.target.size());
    const float blur = d.animator.blur();

    DeviceUniforms uniforms;
    uniforms.windowSize = m_windowPx;
    uniforms.screenOrigin = glm::round(d.rect.center + slideOffset(d) - screen * 0.5f);
    uniforms.screenPx = screen;
    uniforms.scale = d.rect.scale;
    uniforms.kind = d.kind == DeviceKind::Phone ? 0.0f : 1.0f;
    uniforms.bezel = d.spec.bezelLogical;
    uniforms.screenRadius = d.spec.screenRadiusLogical;
    uniforms.landscape = d.kind == DeviceKind::Phone && m_phoneOrientation == Orientation::Landscape ? 1.0f : 0.0f;
    uniforms.dim = 1.0f - kBlurDim * blur;

    // Without a blur this frame, the sharp screen stands in for it at weight 0.
    SDL_GPUTexture* blurred = d.target.texture();
    if (blur > 0.0f && d.blur.texture() != nullptr) {
        blurred = d.blur.texture();
        uniforms.blur = blur;
    }
    const SDL_GPUTextureSamplerBinding bindings[2]{{d.target.texture(), m_nearest}, {blurred, m_linear}};
    SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void DeviceShell::draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const {
    if (m_pipeline == nullptr) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    // Back to front.
    for (const Placement layer : {Placement::Hidden, Placement::Background, Placement::Centered}) {
        for (const Device* d : {&m_phone, &m_tablet}) {
            // A device still sliding out is Hidden by target but visible: it goes underneath.
            if (d->animator.visible() && d->placement == layer && d->target.texture() != nullptr) {
                drawDevice(*d, commands, pass);
            }
        }
    }
}

} // namespace horde::ui
