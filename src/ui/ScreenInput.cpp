#include "ui/ScreenInput.hpp"

#include <glm/common.hpp>

namespace horde::ui {

ScreenPoint mapMouseToScreen(const DeviceRect& rect, glm::vec2 offset, glm::vec2 windowPx) {
    const glm::vec2 p = windowPx - (rect.center + offset) + rect.screenSize * 0.5f;
    ScreenPoint out;
    out.inside = p.x >= 0.0f && p.y >= 0.0f && p.x < rect.screenSize.x && p.y < rect.screenSize.y;
    out.pixel = glm::clamp(p, glm::vec2(0.0f), rect.screenSize);
    return out;
}

void scaleMouseEvent(SDL_Event& event, float density) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        event.motion.x *= density;
        event.motion.y *= density;
        event.motion.xrel *= density;
        event.motion.yrel *= density;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        event.button.x *= density;
        event.button.y *= density;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        event.wheel.mouse_x *= density;
        event.wheel.mouse_y *= density;
        break;
    default:
        break;
    }
}

bool remapMouseEvent(SDL_Event& event, const DeviceRect& rect, glm::vec2 offset) {
    float* x = nullptr;
    float* y = nullptr;
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        x = &event.motion.x;
        y = &event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        x = &event.button.x;
        y = &event.button.y;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        x = &event.wheel.mouse_x;
        y = &event.wheel.mouse_y;
        break;
    default:
        return true;
    }
    const ScreenPoint sp = mapMouseToScreen(rect, offset, {*x, *y});
    *x = sp.pixel.x;
    *y = sp.pixel.y;
    return sp.inside || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN;
}

} // namespace horde::ui
