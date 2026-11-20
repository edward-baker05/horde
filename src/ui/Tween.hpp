#pragma once

#include <algorithm>

namespace horde::ui {

// Ease-out cubic: fast start, gentle landing. Used for every device slide so
// motion feels consistent across the shell.
inline float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// A single float animated from `from` to `to` over `duration` seconds.
//
// retarget() restarts from the *current* value rather than from `to`, so a
// transition interrupted half-way (e.g. the player closes a menu while it is
// still opening) reverses smoothly instead of jumping.
struct Tween {
    float from = 0.0f;
    float to = 0.0f;
    float duration = 0.0f;
    float elapsed = 0.0f;

    void retarget(float newTo, float newDuration) {
        from = value();
        to = newTo;
        duration = newDuration;
        elapsed = 0.0f;
    }

    void update(float dt) {
        elapsed = std::min(elapsed + dt, duration);
    }

    float value() const {
        if (duration <= 0.0f) {
            return to;
        }
        return from + (to - from) * easeOutCubic(std::clamp(elapsed / duration, 0.0f, 1.0f));
    }

    bool done() const {
        return elapsed >= duration;
    }
};

} // namespace horde::ui
