#include "ui/RmlSystem.hpp"

#include <SDL3/SDL.h>

namespace horde::ui {

double RmlSystem::GetElapsedTime() {
    return static_cast<double>(SDL_GetTicksNS()) * 1e-9;
}

bool RmlSystem::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    // Keep RmlUi's severity so warnings and errors stand out in the shared log.
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[RmlUi] %s", message.c_str());
            break;
        case Rml::Log::LT_WARNING:
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[RmlUi] %s", message.c_str());
            break;
        default:
            SDL_Log("[RmlUi] %s", message.c_str());
            break;
    }
    return true;
}

void RmlSystem::SetClipboardText(const Rml::String& text) {
    SDL_SetClipboardText(text.c_str());
}

void RmlSystem::GetClipboardText(Rml::String& text) {
    char* clip = SDL_GetClipboardText();
    text = clip ? clip : "";
    SDL_free(clip);
}

} // namespace horde::ui
