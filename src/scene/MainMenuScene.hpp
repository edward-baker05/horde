#pragma once

#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// Entry screen: a button into the level, and a button into the tech tree.
//
// The buttons are ImGui because there is no text renderer yet. See the README
// for wiring up SDL3_ttf when menu art and labels need to be real sprites.
class MainMenuScene : public Scene {
public:
    bool onEnter(Services& services) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "MainMenu";
    }

private:
    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
};

} // namespace horde::scene
