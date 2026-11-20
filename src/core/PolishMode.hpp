#pragma once

namespace horde {

// Polish mode: the game in Polish, in folk colours, with Chopin on the menu
// and the dancing cow in the levels. Off, it is the plain English game.
// Toggled from the phone's settings page; every launch starts with it off.
namespace polish {

bool enabled();
void setEnabled(bool on);

} // namespace polish

// UI text: the English string, or its Polish translation in Polish mode.
// Strings without a translation come back unchanged. The pointer stays valid
// for the life of the program, so it can be kept or passed to ImGui.
const char* tr(const char* english);

} // namespace horde
