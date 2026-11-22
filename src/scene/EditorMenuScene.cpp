#include "scene/EditorMenuScene.hpp"

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "editor/Actions.hpp"
#include "editor/EditorState.hpp"
#include "gfx/GpuContext.hpp"
#include "logic/LevelFiles.hpp"
#include "scene/SceneStack.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

bool EditorMenuScene::onEnter(Services& services) {
    m_services = &services;
    if (services.ui == nullptr) {
        return false;
    }
    m_context = services.ui->context(ui::DeviceKind::Phone);
    if (m_context == nullptr) {
        return false;
    }
    m_window = services.gpu != nullptr ? services.gpu->window() : nullptr;

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("editor_menu");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "editor menu: could not create the data model");
        return false;
    }
    constructor.Bind("page", &m_page);
    constructor.Bind("level_name", &m_levelName);
    constructor.Bind("saved_label", &m_savedLabel);
    constructor.Bind("save_name", &m_saveName);
    constructor.Bind("status", &m_status);
    constructor.Bind("can_save", &m_canSave);
    constructor.BindEventCallback("resume", &EditorMenuScene::onResume, this);
    constructor.BindEventCallback("save", &EditorMenuScene::onSave, this);
    constructor.BindEventCallback("open_save_as", &EditorMenuScene::onOpenSaveAs, this);
    constructor.BindEventCallback("save_as", &EditorMenuScene::onSaveAs, this);
    constructor.BindEventCallback("name_change", &EditorMenuScene::onNameChange, this);
    constructor.BindEventCallback("typing", &EditorMenuScene::onTyping, this);
    constructor.BindEventCallback("leave", &EditorMenuScene::onLeave, this);
    constructor.BindEventCallback("save_and_leave", &EditorMenuScene::onSaveAndLeave, this);
    constructor.BindEventCallback("discard", &EditorMenuScene::onDiscard, this);
    constructor.BindEventCallback("back", &EditorMenuScene::onBack, this);
    m_model = constructor.GetModelHandle();
    refresh();

    // Shown after the main menu's document in the same context, so on top;
    // its opaque body covers the phone's screen, as the pause's does.
    const std::string path = paths::asset("ui/editor_menu.rml").string();
    m_document = m_context->LoadDocument(path);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "editor menu: could not load %s", path.c_str());
        m_context->RemoveDataModel("editor_menu");
        m_model = {};
        return false;
    }
    m_document->Show();
    return true;
}

void EditorMenuScene::onExit() {
    stopTyping();
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
    }
    if (m_context != nullptr) {
        m_context->RemoveDataModel("editor_menu");
        m_context = nullptr;
    }
    m_model = {};
}

void EditorMenuScene::refresh() {
    const bool named = !m_state.path.empty();
    m_levelName = named ? logic::levelDisplayName(m_state.path) : std::string(tr("Untitled"));
    m_savedLabel = !named ? tr("Not saved yet") : m_state.dirty ? tr("Unsaved changes") : tr("Saved");
    m_canSave = named;
    if (m_saveName.empty()) {
        m_saveName = named ? m_levelName : "untitled";
    }
    if (m_model) {
        for (const char* name : {"level_name", "saved_label", "can_save", "save_name", "status"}) {
            m_model.DirtyVariable(name);
        }
    }
}

void EditorMenuScene::setPage(const char* page) {
    stopTyping();
    if (m_page != page) {
        m_page = page;
        if (m_model) {
            m_model.DirtyVariable("page");
        }
    }
}

bool EditorMenuScene::saveAs(const std::string& name) {
    if (!editor::saveLevelAs(m_state, name)) {
        // Why not, from the editor's status line.
        m_status = m_state.status;
        refresh();
        return false;
    }
    m_status.clear();
    m_saveName = logic::levelDisplayName(m_state.path);
    if (m_onSaved) {
        m_onSaved();
    }
    refresh();
    if (m_leaveAfterSave) {
        close(true);
    } else {
        setPage("root");
    }
    return true;
}

void EditorMenuScene::close(bool leaveEditor) {
    if (m_closing) {
        return;
    }
    m_closing = true;
    stopTyping();
    m_services->scenes->pop();
    if (leaveEditor) {
        m_services->scenes->pop();
    }
}

void EditorMenuScene::stopTyping() {
    if (m_context != nullptr) {
        Rml::Element* focus = m_context->GetFocusElement();
        if (focus != nullptr && focus->GetOwnerDocument() == m_document && focus != m_document) {
            focus->Blur();
        }
    }
    if (m_typing && m_window != nullptr) {
        SDL_StopTextInput(m_window);
    }
    m_typing = false;
}

bool EditorMenuScene::handleEvent(const SDL_Event& event) {
    // Esc steps back: out of the popup or Save As, then out of the menu.
    // RmlUi passes it on, as nothing in the document binds it.
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
        if (m_page == "root") {
            close(false);
        } else {
            m_leaveAfterSave = false;
            setPage("root");
        }
        return true;
    }
    return false;
}

void EditorMenuScene::onResume(Rml::DataModelHandle, Rml::Event&, const Args&) {
    close(false);
}

void EditorMenuScene::onSave(Rml::DataModelHandle, Rml::Event&, const Args&) {
    if (m_canSave) {
        m_leaveAfterSave = false;
        saveAs(logic::levelDisplayName(m_state.path));
    }
}

void EditorMenuScene::onOpenSaveAs(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_leaveAfterSave = false;
    m_status.clear();
    m_model.DirtyVariable("status");
    setPage("save_as");
}

void EditorMenuScene::onSaveAs(Rml::DataModelHandle, Rml::Event&, const Args&) {
    saveAs(m_saveName);
}

void EditorMenuScene::onNameChange(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    // Every keystroke changes the text; Enter saves.
    if (!args.empty() && args[0].Get<bool>(false)) {
        saveAs(m_saveName);
    }
}

void EditorMenuScene::onTyping(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const bool typing = !args.empty() && args[0].Get<bool>(false);
    if (typing != m_typing && m_window != nullptr) {
        if (typing) {
            SDL_StartTextInput(m_window);
        } else {
            SDL_StopTextInput(m_window);
        }
    }
    m_typing = typing;
}

void EditorMenuScene::onLeave(Rml::DataModelHandle, Rml::Event&, const Args&) {
    if (m_state.dirty) {
        setPage("confirm");
    } else {
        close(true);
    }
}

void EditorMenuScene::onSaveAndLeave(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_leaveAfterSave = true;
    if (m_canSave) {
        if (!saveAs(logic::levelDisplayName(m_state.path))) {
            // Back to the menu, where the status says why.
            m_leaveAfterSave = false;
            setPage("root");
        }
    } else {
        // Never saved: it needs a name first.
        setPage("save_as");
    }
}

void EditorMenuScene::onDiscard(Rml::DataModelHandle, Rml::Event&, const Args&) {
    close(true);
}

void EditorMenuScene::onBack(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_leaveAfterSave = false;
    setPage("root");
}

} // namespace horde::scene
