#pragma once

#include <RmlUi/Core/Input.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

namespace Rml {
class Context;
}

namespace horde::ui {

// Unmapped keys return KI_UNKNOWN.
Rml::Input::KeyIdentifier toRmlKey(SDL_Keycode key);

// SDL_Keymod bits to RmlUi KM_* bits.
int toRmlModifiers(SDL_Keymod mod);

// Feeds a screen-space event (already run through remapMouseEvent for mouse
// events) to the context. Returns true if RmlUi consumed it; RmlUi's Process*
// calls return the opposite, so the result is inverted here.
bool forwardToRml(Rml::Context& context, const SDL_Event& event);

} // namespace horde::ui
