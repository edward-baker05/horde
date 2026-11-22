#include "ui/DeviceShell.hpp"

#include <glm/common.hpp>

#include "gfx/GpuContext.hpp"
#include "ui/ScreenInput.hpp"

namespace horde::ui {

namespace {

// Placeholder bezel: the atlas's solid white cell, tinted dark grey.
constexpr glm::vec4 kBezelColor{0.08f, 0.08f, 0.09f, 1.0f};
// A fully blurred (background) device is tinted to 1 - this, so it recedes.
constexpr float kBlurDim = 0.35f;

} // namespace

bool DeviceShell::init(gfx::GpuContext& gpu, const gfx::ShaderLoader& shaders) {
    m_gpu = gpu.device();
    m_format = gpu.swapchainFormat();
    m_phone.kind = DeviceKind::Phone;
    m_tablet.kind = DeviceKind::Tablet;
    return m_phone.batch.init(m_gpu, shaders, m_format) && m_tablet.batch.init(m_gpu, shaders, m_format) &&
           m_phone.blur.init(m_gpu, shaders, m_format) && m_tablet.blur.init(m_gpu, shaders, m_format);
}

void DeviceShell::shutdown() {
    for (Device* d : {&m_phone, &m_tablet}) {
        d->batch.shutdown();
        d->blur.shutdown();
        d->target.release();
    }
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
}

void DeviceShell::setTargets(const ShellTargets& targets) {
    m_phone.animator.setTarget(targets.phone);
    m_phone.placement = targets.phone;
    m_tablet.animator.setTarget(targets.tablet);
    m_tablet.placement = targets.tablet;

    if (targets.phoneOrientation != m_phoneOrientation) {
        m_phoneOrientation = targets.phoneOrientation;
        layoutDevice(m_phone);
    }
}

void DeviceShell::update(float dt) {
    m_phone.animator.update(dt);
    m_tablet.animator.update(dt);
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

void DeviceShell::queue(const Device& d, gfx::SpriteBatch& compositeBatch, SDL_GPUTexture* atlas) const {
    // The texture is the rounded integer target, so draw it 1:1 (NEAREST drops
    // rows otherwise) with its top-left on a whole pixel; the bezel shares the centre.
    const glm::vec2 screen = glm::vec2(d.target.size());
    const glm::vec2 center = glm::round(d.rect.center + slideOffset(d) - screen * 0.5f) + screen * 0.5f;
    const float bezel = d.spec.bezelLogical * d.rect.scale;

    gfx::drawCentered(compositeBatch, atlas, center, screen + glm::vec2(2.0f * bezel), 0.0f, gfx::atlasCell(1, 1, 4, 4),
                      kBezelColor);

    // The sharp screen, then its blur over it at alpha = blur: a fade between the
    // two, both dimmed as the blur comes in.
    const float blur = d.animator.blur();
    const float shade = 1.0f - kBlurDim * blur;
    const glm::vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
    gfx::drawCentered(compositeBatch, d.target.texture(), center, screen, 0.0f, uv, {shade, shade, shade, 1.0f});
    if (blur > 0.0f && d.blur.texture() != nullptr) {
        gfx::drawCentered(compositeBatch, d.blur.texture(), center, screen, 0.0f, uv, {shade, shade, shade, blur});
    }
}

void DeviceShell::composite(gfx::SpriteBatch& compositeBatch, SDL_GPUTexture* atlas) const {
    // Back to front.
    for (const Placement layer : {Placement::Hidden, Placement::Background, Placement::Centered}) {
        for (const Device* d : {&m_phone, &m_tablet}) {
            // A device still sliding out is Hidden by target but visible: it goes underneath.
            if (d->animator.visible() && d->placement == layer && d->target.texture() != nullptr) {
                queue(*d, compositeBatch, atlas);
            }
        }
    }
}

} // namespace horde::ui
