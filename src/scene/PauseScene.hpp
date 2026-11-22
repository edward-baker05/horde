#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <functional>

#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::scene {

// The phone, risen over a round while the tablet blurs behind it (or over the
// dev ground or tech tree, blurred behind it): Resume, Restart and a way out.
// Being on top is what freezes the scene below, as only the top scene updates.
class PauseScene : public Scene {
public:
    // `onRestart` runs after this scene pops; without one there is no Restart.
    // `returnsToEditor` relabels the way out for a play-test, which leaves to
    // the editor, not the menu.
    PauseScene(std::function<void()> onRestart, bool returnsToEditor)
        : m_onRestart(std::move(onRestart)), m_returnsToEditor(returnsToEditor) {}

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
        return "Pause";
    }

    void onResize(float width, float height) override {
        m_camera.setViewport(width, height);
    }

private:
    // One pop per pause, however many Esc presses or clicks arrive before the
    // queued pop is applied: a second would pop the level under it.
    bool close();

    using Args = Rml::VariantList;
    void onResume(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onRestartClicked(Rml::DataModelHandle, Rml::Event&, const Args&);
    void onQuit(Rml::DataModelHandle, Rml::Event&, const Args&);

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;

    std::function<void()> m_onRestart;
    bool m_returnsToEditor = false;
    bool m_canRestart = false;
    bool m_closing = false;
};

} // namespace horde::scene
