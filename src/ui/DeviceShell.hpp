#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>

#include <glm/vec2.hpp>

#include "gfx/Blur.hpp"
#include "gfx/RenderTarget.hpp"
#include "gfx/SpriteBatch.hpp"
#include "ui/DeviceLayout.hpp"
#include "ui/ShellState.hpp"

namespace horde::gfx {
class GpuContext;
class ShaderLoader;
} // namespace horde::gfx

namespace horde::ui {

// The phone and tablet the game lives on: where each sits in the window, the
// offscreen texture each scene draws into, and how they are drawn onto the
// swapchain (shadow, frame, screen at its slide offset, camera; see
// shaders/src/device.frag.hlsl). Scenes with no device bypass it.
class DeviceShell {
public:
    DeviceShell() = default;
    ~DeviceShell();

    DeviceShell(const DeviceShell&) = delete;
    DeviceShell& operator=(const DeviceShell&) = delete;

    bool init(gfx::GpuContext& gpu, const gfx::ShaderLoader& shaders);
    void shutdown();

    // Fits both devices to the window and resizes their screen textures.
    void layout(glm::vec2 windowPx);
    void setTargets(const ShellTargets& targets);
    void update(float dt);

    glm::vec2 windowPx() const {
        return m_windowPx;
    }

    // False once a device has slid fully off screen: nothing to draw or route.
    bool visible(DeviceKind kind) const;
    // Screen texture size in px; zero for DeviceKind::None.
    glm::ivec2 screenSize(DeviceKind kind) const;
    const DeviceRect& rect(DeviceKind kind) const;

    // True while a device slides out (target Hidden, still on screen): its
    // screen keeps its last frame rather than being redrawn from whatever scene
    // is now on top of it, which would swap its content mid-slide. False if the
    // texture was recreated since it was last drawn, as it is then undefined.
    bool frozen(DeviceKind kind) const;
    // Tells the shell the device's screen texture holds a drawn frame.
    void markDrawn(DeviceKind kind);

    gfx::RenderTarget& screen(DeviceKind kind);
    // One batch per device so each frame's uploads do not overwrite each other.
    gfx::SpriteBatch& batch(DeviceKind kind);

    // The device's blur fade, 0..1: above 0 its screen is drawn blurred over the sharp one.
    float blurAmount(DeviceKind kind) const;
    // Blurs the device's screen texture; one Blur per device, as each result
    // must survive until the swapchain pass. Render passes: after all uploads.
    void applyBlur(DeviceKind kind, SDL_GPUCommandBuffer* commands);

    // Rewrites a mouse event (already in window px) into the device's screen
    // px. False means drop it: a button press on the bezel or background.
    bool remapEvent(DeviceKind kind, SDL_Event& event) const;

    // Fills the window with the dark backdrop the devices sit on. Inside the swapchain pass.
    void drawBackdrop(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const;

    // A window-sized scene (one with no device) under a phone overlay: the
    // caller draws it into windowScreen(), blurs it with applyWindowBlur(),
    // and drawWindowBackdrop() shows it in place of the backdrop, fading to
    // its blur as the phone rises.
    bool windowBehind() const {
        return m_windowLayer.behind;
    }
    gfx::RenderTarget& windowScreen() {
        return m_windowLayer.target;
    }
    void applyWindowBlur(SDL_GPUCommandBuffer* commands);
    void drawWindowBackdrop(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const;
    // Draws the visible devices back to front (Background before Centered),
    // each its shadow, frame and screen. Inside the swapchain pass.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const;

private:
    struct Device {
        DeviceKind kind = DeviceKind::None;
        DeviceSpec spec;
        DeviceRect rect;
        Placement placement = Placement::Hidden;
        DeviceAnimator animator;
        gfx::RenderTarget target;
        gfx::SpriteBatch batch;
        gfx::Blur blur;
        // False after the target is recreated, until a frame is drawn into it.
        bool drawn = false;
    };

    struct WindowLayer {
        gfx::RenderTarget target;
        gfx::Blur blur;
        Tween fade;
        bool behind = false;
    };

    Device& device(DeviceKind kind) {
        return kind == DeviceKind::Phone ? m_phone : m_tablet;
    }

    const Device& device(DeviceKind kind) const {
        return kind == DeviceKind::Phone ? m_phone : m_tablet;
    }

    // The slide offset in window px: phones drop down, tablets leave to the right.
    glm::vec2 slideOffset(const Device& d) const;
    void layoutDevice(Device& d);
    void drawDevice(const Device& d, SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) const;

    SDL_GPUDevice* m_gpu = nullptr;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    // Nearest for the sharp screen, drawn 1:1; linear for its half-size blur.
    SDL_GPUSampler* m_nearest = nullptr;
    SDL_GPUSampler* m_linear = nullptr;
    SDL_GPUTextureFormat m_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    glm::vec2 m_windowPx{0.0f};
    Orientation m_phoneOrientation = Orientation::Portrait;
    Device m_phone;
    Device m_tablet;
    WindowLayer m_windowLayer;
};

} // namespace horde::ui
