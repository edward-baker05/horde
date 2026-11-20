#pragma once

#include "scene/Scene.hpp"

namespace horde::ui {

// The frame cost breakdown, drawn into the current ImGui window. Shown as a
// small overlay in any scene when toggled with F3. When measureSimGpu is given,
// it also offers the checkbox that turns GPU physics timing on and off.
void drawFrameStats(const scene::FrameStats& stats, bool* measureSimGpu = nullptr);

} // namespace horde::ui
