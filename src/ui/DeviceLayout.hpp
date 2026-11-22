#pragma once

#include <glm/glm.hpp>

namespace horde::ui {

enum class DeviceKind { None, Phone, Tablet };
enum class Orientation { Portrait, Landscape };

// Physical shape of a device in logical px. The screen is what the UI lays out
// into; the bezel is the frame around it. Logical px are scaled to the window
// by fitDevice(), so UI authored at 390x844 looks the same at any window size.
// The body's corner radius is the screen's plus the bezel, so the two are concentric.
struct DeviceSpec {
    glm::vec2 screenLogical{0.0f};
    float bezelLogical = 0.0f;
    float screenRadiusLogical = 0.0f;
};

// Placement of a device in the window. `screenSize` is in window px and equals
// screenLogical * scale; `center` is the middle of the screen in window px.
struct DeviceRect {
    glm::vec2 center{0.0f};
    glm::vec2 screenSize{0.0f};
    float scale = 1.0f;
};

// Phone is 390x844 (swapped in landscape) with a 12 px bezel and 55 px screen
// corners; tablet is 1280x800 (swapped in portrait) with a 24 px bezel and 22 px
// screen corners. DeviceKind::None yields an empty spec.
DeviceSpec deviceSpec(DeviceKind kind, Orientation orientation);

// Largest scale at which screen plus 2x bezel fits inside the window shrunk by
// `margin` * window on every side, centred in the window.
DeviceRect fitDevice(const DeviceSpec& spec, glm::vec2 windowPx, float margin = 0.06f);

// Integer size of the render target backing the screen: rounded, at least 1x1.
glm::ivec2 screenPixelSize(const DeviceRect& rect);

} // namespace horde::ui
