#include "ui/DeviceLayout.hpp"

#include <algorithm>
#include <cmath>

namespace horde::ui {

DeviceSpec deviceSpec(DeviceKind kind, Orientation orientation) {
    glm::vec2 screen{0.0f};
    float bezel = 0.0f;
    switch (kind) {
    case DeviceKind::Phone:
        screen = {390.0f, 844.0f};
        bezel = 14.0f;
        break;
    case DeviceKind::Tablet:
        screen = {800.0f, 1280.0f};
        bezel = 24.0f;
        break;
    case DeviceKind::None:
        break;
    }
    // Specs above are portrait; landscape swaps the axes.
    if (orientation == Orientation::Landscape) {
        std::swap(screen.x, screen.y);
    }
    return {screen, bezel};
}

DeviceRect fitDevice(const DeviceSpec& spec, glm::vec2 windowPx, float margin) {
    const glm::vec2 total = spec.screenLogical + 2.0f * spec.bezelLogical;
    const glm::vec2 avail = windowPx * (1.0f - 2.0f * margin);
    float scale = 1.0f;
    if (total.x > 0.0f && total.y > 0.0f) {
        scale = std::min(avail.x / total.x, avail.y / total.y);
    }
    return {windowPx * 0.5f, spec.screenLogical * scale, scale};
}

glm::ivec2 screenPixelSize(const DeviceRect& rect) {
    return {std::max(1, static_cast<int>(std::lround(rect.screenSize.x))),
            std::max(1, static_cast<int>(std::lround(rect.screenSize.y)))};
}

} // namespace horde::ui
