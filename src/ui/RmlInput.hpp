#pragma once

#include <RmlUi/Core/Input.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_stdinc.h>

namespace Rml {
class Context;
}

namespace horde::ui {

// Unmapped keys return KI_UNKNOWN.
Rml::Input::KeyIdentifier toRmlKey(SDL_Keycode key);

// SDL button (1=left 2=middle 3=right) to RmlUi index (0=left 1=right 2=middle).
// Returns -1 for buttons RmlUi has no slot for (X1/X2).
int toRmlButton(Uint8 sdlButton);

// SDL_Keymod bits to RmlUi KM_* bits.
int toRmlModifiers(SDL_Keymod mod);

// Feeds a screen-space event (already run through remapMouseEvent for mouse
// events) to the context. Returns true if RmlUi consumed it; RmlUi's Process*
// calls return the opposite, so the result is inverted here.
bool forwardToRml(Rml::Context& context, const SDL_Event& event);

} // namespace horde::ui
