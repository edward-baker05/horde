#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <functional>
#include <string>

#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

struct SDL_Window;

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::editor {
struct EditorState;
}

namespace horde::scene {

// The editor's menu: the phone, risen over the editor's tablet while it blurs
// behind (assets/ui/editor_menu.rml). Save under the level's name, Save As
// under a new one, or return to the main menu, which asks to save or discard
// unsaved changes first. Pushed by EditorScene on Esc.
class EditorMenuScene : public Scene {
public:
    // `state` must outlive this scene. `onSaved` runs after each save, so the
    // editor can refresh what shows the level's name and the saved levels.
    EditorMenuScene(editor::EditorState& state, std::function<void()> onSaved)
        : m_state(state), m_onSaved(std::move(onSaved)) {}

    bool onEnter(Services& services) override;
    void onExit() override;
    bool handleEvent(const SDL_Event& event) override;

    ui::ScenePresentation presentation() const override {
        return {ui::DeviceKind::Phone, ui::Orientation::Portrait, true};
    }

    // Draws no sprites; the camera only sizes the (empty) screen pass.
    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "EditorMenu";
    }

    void onResize(float width, float height) override {
        m_camera.setViewport(width, height);
    }

private:
    using Args = Rml::VariantList;

    // Mirrors the state's name, saved state and status into the document.
    void refresh();
    void setPage(const char* page);
    // Saves under `name`; on success tells the editor, and leaves if a save
    // on the way out asked to.
    bool saveAs(const std::string& name);
    // Pops this scene and, when `leaveEditor`, the editor under it. Once only.
    void close(bool leaveEditor);
    void stopTyping();

    void onResume(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onSave(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onOpenSaveAs(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onSaveAs(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onNameChange(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onTyping(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onLeave(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onSaveAndLeave(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onDiscard(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onBack(Rml::DataModelHandle, Rml::Event&, const Args&);

    editor::EditorState& m_state;
    std::function<void()> m_onSaved;
    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::DataModelHandle m_model;
    SDL_Window* m_window = nullptr;

    // Model state. page: "root", "save_as", or "confirm" (the save-or-discard
    // popup over root).
    std::string m_page = "root";
    std::string m_levelName;
    std::string m_savedLabel;
    std::string m_saveName;
    std::string m_status;
    bool m_canSave = false;

    // Save As was reached from the popup, so a successful save also leaves.
    bool m_leaveAfterSave = false;
    bool m_typing = false;
    bool m_closing = false;
};

} // namespace horde::scene
