#include "ui/ShellState.hpp"

#include <cstddef>

namespace horde::ui {

namespace {

void place(ShellTargets& targets, DeviceKind kind, Placement placement) {
    if (kind == DeviceKind::Phone) {
        targets.phone = placement;
    } else if (kind == DeviceKind::Tablet) {
        targets.tablet = placement;
    }
}

} // namespace

ShellTargets computeTargets(std::span<const ScenePresentation> stack) {
    ShellTargets targets;
    if (stack.empty()) {
        return targets;
    }

    const ScenePresentation& top = stack.back();
    place(targets, top.device, top.away ? Placement::Hidden : Placement::Centered);
    if (top.device == DeviceKind::Phone) {
        targets.phoneOrientation = top.orientation;
    }

    if (top.overlay && top.device != DeviceKind::None) {
        // The nearest scene on another device stays visible behind the overlay.
        // Scenes on the same device are skipped: that device is already centred.
        for (size_t i = stack.size() - 1; i-- > 0;) {
            if (stack[i].device != top.device) {
                place(targets, stack[i].device, Placement::Background);
                targets.windowBehind = stack[i].device == DeviceKind::None;
                if (stack[i].device == DeviceKind::Phone) {
                    targets.phoneOrientation = stack[i].orientation;
                }
                break;
            }
        }
    }
    return targets;
}

void DeviceAnimator::setTarget(Placement placement) {
    const float offscreenTo = placement == Placement::Hidden ? 1.0f : 0.0f;
    const float blurTo = placement == Placement::Background ? 1.0f : 0.0f;
    // Only retarget on a real change so repeated calls do not restart the tween.
    if (offscreen_.to != offscreenTo) {
        offscreen_.retarget(offscreenTo, slideSeconds);
    }
    if (blur_.to != blurTo) {
        blur_.retarget(blurTo, blurSeconds);
    }
}

void DeviceAnimator::update(float dt) {
    offscreen_.update(dt);
    blur_.update(dt);
}

} // namespace horde::ui
