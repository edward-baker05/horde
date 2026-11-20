#include "scene/PauseScene.hpp"

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include "core/Paths.hpp"
#include "scene/SceneStack.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

bool PauseScene::onEnter(Services& services) {
    m_services = &services;
    if (services.ui == nullptr) {
        return false;
    }
    m_context = services.ui->context(ui::DeviceKind::Phone);
    if (m_context == nullptr) {
        return false;
    }

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("pause");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pause: could not create the data model");
        return false;
    }
    constructor.Bind("to_editor", &m_returnsToEditor);
    constructor.BindEventCallback("resume", &PauseScene::onResume, this);
    constructor.BindEventCallback("restart", &PauseScene::onRestartClicked, this);
    constructor.BindEventCallback("quit", &PauseScene::onQuit, this);

    // The menu's document is still in this shared context. This one is shown
    // after it, so it sits on top, and its opaque body covers the whole phone
    // screen, so the menu is neither seen nor clicked through it.
    const std::string documentPath = paths::asset("ui/pause.rml").string();
    m_document = m_context->LoadDocument(documentPath);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pause: could not load %s", documentPath.c_str());
        m_context->RemoveDataModel("pause");
        return false;
    }
    m_document->Show();
    return true;
}

void PauseScene::onExit() {
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
    }
    if (m_context != nullptr) {
        m_context->RemoveDataModel("pause");
        m_context = nullptr;
    }
}

bool PauseScene::close() {
    if (m_closing) {
        return false;
    }
    m_closing = true;
    m_services->scenes->pop();
    return true;
}

bool PauseScene::handleEvent(const SDL_Event& event) {
    // RmlUi sees keys first and passes Esc on, as nothing in the document binds it.
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
        close();
        return true;
    }
    return false;
}

void PauseScene::onResume(Rml::DataModelHandle, Rml::Event&, const Args&) {
    close();
}

void PauseScene::onRestartClicked(Rml::DataModelHandle, Rml::Event&, const Args&) {
    // Pop first so the replacement queued by onRestart lands on the level, not on us.
    if (close() && m_onRestart) {
        m_onRestart();
    }
}

void PauseScene::onQuit(Rml::DataModelHandle, Rml::Event&, const Args&) {
    // Pause, then the level under it.
    if (!m_closing) {
        m_closing = true;
        m_services->scenes->pop();
        m_services->scenes->pop();
    }
}

} // namespace horde::scene
