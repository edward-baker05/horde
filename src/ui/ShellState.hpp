#pragma once

#include <span>

#include "ui/DeviceLayout.hpp"
#include "ui/Tween.hpp"

namespace horde::ui {

// What a scene needs from the device shell. Light enough for Scene.hpp to
// include; the default (no device) means the scene draws straight to the
// swapchain as before.
struct ScenePresentation {
    DeviceKind device = DeviceKind::None;
    Orientation orientation = Orientation::Landscape;
    // An overlay (e.g. pause) leaves the scene beneath visible, blurred behind it.
    bool overlay = false;
};

enum class Placement { Hidden, Centered, Background };

struct ShellTargets {
    Placement phone = Placement::Hidden;
    Placement tablet = Placement::Hidden;
    Orientation phoneOrientation = Orientation::Portrait;
};

// Decides where each device sits for a scene stack ordered bottom to top.
ShellTargets computeTargets(std::span<const ScenePresentation> stack);

// Slides one device on/off screen and fades its background blur. offscreen() is
// a window-independent 0..1 fraction so the renderer maps it to pixels itself.
class DeviceAnimator {
public:
    static constexpr float slideSeconds = 0.35f;
    static constexpr float blurSeconds = 0.25f;

    void setTarget(Placement placement);
    void update(float dt);

    float offscreen() const {
        return offscreen_.value();
    } // 0 on screen, 1 hidden

    float blur() const {
        return blur_.value();
    } // 0..1

    bool visible() const {
        return offscreen() < 1.0f;
    }

    bool settled() const {
        return offscreen_.done() && blur_.done();
    }

private:
    // Starts Hidden and settled: duration 0 makes value() return `to`.
    Tween offscreen_{1.0f, 1.0f, 0.0f, 0.0f};
    Tween blur_{0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace horde::ui
