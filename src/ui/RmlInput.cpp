#include "ui/RmlInput.hpp"

#include <RmlUi/Core/Context.h>

namespace horde::ui {

using Rml::Input::KeyIdentifier;

KeyIdentifier toRmlKey(SDL_Keycode key) {
    namespace KI = Rml::Input;
    if (key >= SDLK_A && key <= SDLK_Z)
        return static_cast<KeyIdentifier>(KI::KI_A + (key - SDLK_A));
    if (key >= SDLK_0 && key <= SDLK_9)
        return static_cast<KeyIdentifier>(KI::KI_0 + (key - SDLK_0));
    if (key >= SDLK_F1 && key <= SDLK_F12)
        return static_cast<KeyIdentifier>(KI::KI_F1 + (key - SDLK_F1));
    switch (key) {
    case SDLK_LEFT: return KI::KI_LEFT;
    case SDLK_RIGHT: return KI::KI_RIGHT;
    case SDLK_UP: return KI::KI_UP;
    case SDLK_DOWN: return KI::KI_DOWN;
    case SDLK_RETURN: return KI::KI_RETURN;
    case SDLK_ESCAPE: return KI::KI_ESCAPE;
    case SDLK_BACKSPACE: return KI::KI_BACK;
    case SDLK_TAB: return KI::KI_TAB;
    case SDLK_DELETE: return KI::KI_DELETE;
    case SDLK_HOME: return KI::KI_HOME;
    case SDLK_END: return KI::KI_END;
    case SDLK_PAGEUP: return KI::KI_PRIOR;
    case SDLK_PAGEDOWN: return KI::KI_NEXT;
    case SDLK_SPACE: return KI::KI_SPACE;
    default: return KI::KI_UNKNOWN;
    }
}

int toRmlButton(Uint8 sdlButton) {
    switch (sdlButton) {
    case SDL_BUTTON_LEFT: return 0;
    case SDL_BUTTON_RIGHT: return 1;
    case SDL_BUTTON_MIDDLE: return 2;
    default: return -1;
    }
}

int toRmlModifiers(SDL_Keymod mod) {
    int out = 0;
    if (mod & SDL_KMOD_CTRL) out |= Rml::Input::KM_CTRL;
    if (mod & SDL_KMOD_SHIFT) out |= Rml::Input::KM_SHIFT;
    if (mod & SDL_KMOD_ALT) out |= Rml::Input::KM_ALT;
    if (mod & SDL_KMOD_GUI) out |= Rml::Input::KM_META;
    if (mod & SDL_KMOD_CAPS) out |= Rml::Input::KM_CAPSLOCK;
    if (mod & SDL_KMOD_NUM) out |= Rml::Input::KM_NUMLOCK;
    return out;
}

bool forwardToRml(Rml::Context& context, const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return !context.ProcessMouseMove(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y),
                                         toRmlModifiers(SDL_GetModState()));
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        // RmlUi orders buttons left/right/middle, unlike SDL's left/middle/right.
        const int button = toRmlButton(event.button.button);
        if (button < 0)
            return false;
        const int mods = toRmlModifiers(SDL_GetModState());
        return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? !context.ProcessMouseButtonDown(button, mods)
                                                         : !context.ProcessMouseButtonUp(button, mods);
    }
    case SDL_EVENT_MOUSE_WHEEL:
        // SDL scrolls up positive; RmlUi scrolls down positive.
        return !context.ProcessMouseWheel(Rml::Vector2f(event.wheel.x, -event.wheel.y),
                                          toRmlModifiers(SDL_GetModState()));
    case SDL_EVENT_KEY_DOWN:
        return !context.ProcessKeyDown(toRmlKey(event.key.key), toRmlModifiers(static_cast<SDL_Keymod>(event.key.mod)));
    case SDL_EVENT_KEY_UP:
        return !context.ProcessKeyUp(toRmlKey(event.key.key), toRmlModifiers(static_cast<SDL_Keymod>(event.key.mod)));
    case SDL_EVENT_TEXT_INPUT:
        return !context.ProcessTextInput(Rml::String(event.text.text));
    default:
        return false;
    }
}

} // namespace horde::ui
