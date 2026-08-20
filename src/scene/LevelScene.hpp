#pragma once

#include <glm/glm.hpp>

#include "Logic/UnitData.hpp"
#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The game world. Currently a single rectangle and a button back to the menu —
// this is where the actual game goes.
class LevelScene : public Scene {
public:
    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void render(gfx::SpriteBatch& batch) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Level";
    }

private:
    void spawnUnits(size_t count);
    void applyVectorFieldPreset();

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    size_t MaxUnits = 5000;
    UnitManager unit_manager{MaxUnits};

    glm::vec2 level_size = {4000.0f, 3000.0f};
    int enemy_size = 5;

    // UI and Profiling state
    int m_uiUnitCount = 5000;
    float m_uiUnitSpeed = 15.0f;
    glm::vec2 m_uiLevelSize = {4000.0f, 3000.0f};
    size_t m_visibleUnitsCount = 0;
    bool m_simulationPaused = false;
    bool m_stepFrame = false;
    bool m_uiEnableCramming = true;
    int m_uiMaxCrammingLimit = 12;

    // Vector Field UI state
    bool m_uiEnableVectorField = true;
    int m_uiVectorFieldPreset = 0;
    float m_uiVectorFieldSpeed = 50.0f;
    float m_uiVectorFieldSteering = 3.0f;
    float m_uiMaxSpeed = 75.0f;
    float m_uiVectorFieldCellSize = 80.0f;
    bool m_uiDrawVectorField = true;
};

} // namespace horde::scene
