#include "scene/DancingCow.hpp"

#include <SDL3/SDL_log.h>

#include <RmlUi/Core.h>

#include <cstdio>

#include "audio/MusicPlayer.hpp"
#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "scene/Scene.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

namespace {

// Cypis, "Gdzie jest biały węgorz? (Zejście)". Mastered hot, so turned down
// to sit level with the menu music.
constexpr float kTrackGain = 0.22f;

// The GIF: 20 frames of 100 ms, cut out of their white background into
// assets/ui/krowa/00.png .. 19.png.
constexpr int kFrames = 20;
constexpr float kFrameSeconds = 0.1f;

} // namespace

DancingCow::~DancingCow() {
    hide();
}

void DancingCow::show(Services& services) {
    if (!services.dancingCow) {
        return;
    }
    // Normal mode: no cow, and levels play in silence.
    if (!polish::enabled()) {
        if (services.music != nullptr) {
            services.music->stop();
        }
        return;
    }
    if (services.music != nullptr) {
        services.music->play(paths::asset("music/polish_cow.ogg"), kTrackGain);
    }
    if (m_document != nullptr || services.ui == nullptr) {
        return;
    }
    Rml::Context* context = services.ui->context(ui::DeviceKind::None);
    if (context == nullptr) {
        return;
    }
    const std::string path = paths::asset("ui/krowa.rml").string();
    m_document = context->LoadDocument(path);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "dancing cow: could not load %s", path.c_str());
        return;
    }
    m_image = m_document->GetElementById("cow");
    m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void DancingCow::hide() {
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
        m_image = nullptr;
    }
}

void DancingCow::update(float deltaTime) {
    if (m_image == nullptr) {
        return;
    }
    m_time += deltaTime;
    const int frame = static_cast<int>(m_time / kFrameSeconds) % kFrames;
    if (m_time > kFrames * kFrameSeconds) {
        m_time -= kFrames * kFrameSeconds;
    }
    if (frame != m_frame) {
        m_frame = frame;
        char src[32];
        std::snprintf(src, sizeof(src), "krowa/%02d.png", frame);
        m_image->SetAttribute("src", src);
    }
}

} // namespace horde::scene
