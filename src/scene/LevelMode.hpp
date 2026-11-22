#pragma once

#include <glm/glm.hpp>

#include <algorithm>

#include "ui/ShellState.hpp"

namespace horde::scene {

// How a level is being run. Player is the game: on the tablet, a fixed view,
// no debug tooling. Dev is the sandbox: full window with the ImGui panel and
// free camera, which is also what the CLI and benchmarks use.
enum class LevelMode { Player, Dev };

// Kept apart from LevelScene so the device rules can be tested without
// pulling in the simulation headers.
inline ui::ScenePresentation levelPresentation(LevelMode mode) {
    return mode == LevelMode::Player ? ui::ScenePresentation{ui::DeviceKind::Tablet, ui::Orientation::Landscape}
                                     : ui::ScenePresentation{};
}

// The round's tower tray across the bottom of the tablet, as a fraction of the
// screen height: 116dp of the landscape tablet's 800. Matches .tray in
// assets/ui/tablet.rcss.
inline constexpr float kRoundTrayFraction = 116.0f / 800.0f;

// A camera view that fits a level into the feed: the screen above a tray
// `trayPx` tall. `center` is the world point at the middle of the screen.
struct FeedFit {
    glm::vec2 center{0.0f};
    float zoom = 1.0f;
};

inline FeedFit fitFeed(glm::vec2 levelSize, glm::vec2 screenPx, float trayPx, float margin = 0.95f) {
    const float feedHeight = std::max(screenPx.y - trayPx, 1.0f);
    const float zoom = margin * std::min(screenPx.x / levelSize.x, feedHeight / levelSize.y);
    // The feed's middle sits trayPx/2 above the screen's, so the camera looks
    // that far (in world units) below the level's middle. y is down.
    return {levelSize * 0.5f + glm::vec2(0.0f, trayPx * 0.5f / zoom), zoom};
}

} // namespace horde::scene
