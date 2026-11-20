#pragma once

#include <RmlUi/Core/SystemInterface.h>

namespace horde::ui {

// RmlUi's window onto the platform: clock, log and clipboard, all via SDL.
class RmlSystem : public Rml::SystemInterface {
public:
    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;
};

} // namespace horde::ui
