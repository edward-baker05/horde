#pragma once

#include <SDL3/SDL_events.h>

#include <glm/glm.hpp>

#include "ui/DeviceLayout.hpp"

namespace horde::ui {

// A window-space point expressed in screen-texture pixels. `pixel` is clamped
// to the screen; `inside` says whether the original point was in bounds.
struct ScreenPoint {
    glm::vec2 pixel{0.0f};
    bool inside = false;
};

// Multiplies a mouse event's position (and motion delta) by `density`: SDL reports window
// units, contexts and screens are in pixels. Other events are untouched.
void scaleMouseEvent(SDL_Event& event, float density);

// `offset` is the device's current slide offset in window px, so a device that
// is mid-transition still receives correctly mapped input.
ScreenPoint mapMouseToScreen(const DeviceRect& rect, glm::vec2 offset, glm::vec2 windowPx);

// Rewrites a mouse event's x/y from window px into screen px, in place. Returns
// false for a button-down outside the screen: the caller drops it so clicks on
// the bezel or background do not reach the UI. Motion, button-up and wheel are
// clamped instead, so drags and hover-out still terminate cleanly.
bool remapMouseEvent(SDL_Event& event, const DeviceRect& rect, glm::vec2 offset);

} // namespace horde::ui
