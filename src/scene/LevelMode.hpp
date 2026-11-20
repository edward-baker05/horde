#pragma once

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

} // namespace horde::scene
