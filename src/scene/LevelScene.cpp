#include <SDL3/SDL_video.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/LevelScene.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

void LevelScene::applyVectorFieldPreset() {
    unit_manager.SetVectorFieldEnabled(m_uiEnableVectorField);
    unit_manager.SetVectorFieldSpeed(m_uiVectorFieldSpeed);
    unit_manager.SetVectorFieldSteeringStrength(m_uiVectorFieldSteering);
    unit_manager.SetMaxSpeed(m_uiMaxSpeed);

    VectorField& vf = unit_manager.GetVectorField();
    vf.resize(BoundingBox(glm::vec2(0.0f, 0.0f), level_size), m_uiVectorFieldCellSize);

    const glm::vec2 center = level_size * 0.5f;
    vf.applyPreset(static_cast<VectorFieldPreset>(m_uiVectorFieldPreset), center);
}

void LevelScene::spawnUnits(size_t count) {
    unit_manager.ClearUnits();
    unit_manager.Reserve(count);
    unit_manager.SetWorldBounds(BoundingBox(glm::vec2(0.0f, 0.0f), level_size));

    const float r = static_cast<float>(enemy_size);
    const float margin = r * 3.0f;
    const float usableWidth = std::max(10.0f, level_size.x - 2.0f * margin);
    const float usableHeight = std::max(10.0f, level_size.y - 2.0f * margin);

    const float aspectRatio = usableWidth / usableHeight;
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count) * aspectRatio))));
    const int rows = std::max(1, static_cast<int>(std::ceil(static_cast<float>(count) / static_cast<float>(cols))));

    const float spacingX = usableWidth / static_cast<float>(std::max(1, cols));
    const float spacingY = usableHeight / static_cast<float>(std::max(1, rows));

    for (size_t i = 0; i < count; ++i) {
        const int c = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const float x = margin + (static_cast<float>(c) + 0.5f) * spacingX;
        const float y = margin + (static_cast<float>(row) + 0.5f) * spacingY;

        const float angle = static_cast<float>(i) * 0.6283185f;
        const float speed = m_uiUnitSpeed * (0.8f + static_cast<float>(i % 5) * 0.1f);
        const glm::vec2 spawnVel(std::cos(angle) * speed, std::sin(angle) * speed);

        unit_manager.SpawnUnit(glm::vec2(x, y), spawnVel, 10, 0.01f, r);
    }
}

bool LevelScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));
    m_camera.setZoomLimits(0.05f, 5.0f);
    m_camera.setCenter(level_size * 0.5f);
    m_camera.setZoom(0.35f);

    m_uiUnitCount = static_cast<int>(MaxUnits);
    m_uiLevelSize = level_size;

    applyVectorFieldPreset();
    spawnUnits(MaxUnits);

    return true;
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    if (!m_simulationPaused || m_stepFrame) {
        unit_manager.UpdatePhysics(dt);
        m_stepFrame = false;
    }
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    gfx::Sprite rectangle;
    rectangle.position = {0.0f, 0.0f, 0.0f};
    rectangle.size = level_size;
    rectangle.uv = gfx::atlasCell(1, 1, 2, 2);
    rectangle.color = {0.35f, 0.65f, 0.9f, 1.0f};

    batch.draw(rectangle, m_services->atlas->handle());

    // Frustum culling: calculate camera visible bounds with margin
    const float maxRadius = static_cast<float>(enemy_size) * 2.0f;
    const BoundingBox viewBounds = m_camera.visibleBounds(maxRadius);

    // Render Vector Field overlay
    if (m_uiDrawVectorField && m_uiEnableVectorField) {
        const VectorField& vf = unit_manager.GetVectorField();
        const int vfCols = vf.getCols();
        const int vfRows = vf.getRows();
        const float cSize = vf.getCellSize();
        const float arrowLen = std::max(6.0f, cSize * 0.55f);
        const float thickness = 2.5f;

        gfx::Sprite line;
        line.uv = gfx::atlasCell(1, 1, 2, 2);
        line.color = {1.0f, 0.85f, 0.2f, 0.35f};

        gfx::Sprite dot;
        dot.uv = gfx::atlasCell(1, 0, 2, 2);
        dot.color = {1.0f, 0.95f, 0.3f, 0.6f};

        for (int r = 0; r < vfRows; ++r) {
            for (int c = 0; c < vfCols; ++c) {
                const glm::vec2 center = vf.getCellCenter(c, r);
                if (center.x < viewBounds.min.x - cSize || center.x > viewBounds.max.x + cSize ||
                    center.y < viewBounds.min.y - cSize || center.y > viewBounds.max.y + cSize) {
                    continue;
                }

                const glm::vec2 dir = vf.getVector(c, r);
                const float lenSq = dir.x * dir.x + dir.y * dir.y;
                if (lenSq > 1e-4f) {
                    const float angle = std::atan2(dir.y, dir.x);
                    const float cosA = std::cos(angle);
                    const float sinA = std::sin(angle);

                    // Position line so its center lies at cell center
                    line.position = {center.x - 0.5f * (arrowLen * cosA - thickness * sinA),
                                     center.y - 0.5f * (arrowLen * sinA + thickness * cosA), 0.0f};
                    line.size = {arrowLen, thickness};
                    line.rotation = angle;
                    batch.draw(line, m_services->atlas->handle());

                    // Arrowhead tip dot
                    const float tipDist = arrowLen * 0.5f;
                    constexpr float dotSize = 6.0f;
                    dot.position = {center.x + cosA * tipDist - dotSize * 0.5f,
                                    center.y + sinA * tipDist - dotSize * 0.5f, 0.0f};
                    dot.size = {dotSize, dotSize};
                    dot.rotation = 0.0f;
                    batch.draw(dot, m_services->atlas->handle());
                }
            }
        }
    }

    gfx::Sprite unit;
    unit.uv = gfx::atlasCell(1, 0, 2, 2);
    unit.color = {0.0f, 1.0f, 0.2f, 1.0f};

    const size_t unitCount = unit_manager.GetCurrentUnits();
    const glm::vec2* positions = unit_manager.GetPositionsPtr();
    const float* sizes = unit_manager.GetSizesPtr();

    size_t visibleCount = 0;
    for (size_t i = 0; i < unitCount; ++i) {
        const float px = positions[i].x;
        const float py = positions[i].y;

        if (px >= viewBounds.min.x && px <= viewBounds.max.x &&
            py >= viewBounds.min.y && py <= viewBounds.max.y) {
            const float r = sizes[i];
            unit.position = {px - r, py - r, 0.0f};
            unit.size = {r * 2.0f, r * 2.0f};
            batch.draw(unit, m_services->atlas->handle());
            ++visibleCount;
        }
    }

    m_visibleUnitsCount = visibleCount;
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level & Optimization Metrics");

    if (ImGui::Button("Back to menu", ImVec2(140.0f, 0.0f))) {
        m_services->scenes->pop();
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Performance");
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / (fps > 0.0f ? fps : 1.0f));

    const auto& profile = unit_manager.GetProfile();
    ImGui::Text("Worker Threads: %zu", profile.threadCount);
    ImGui::Text("Total Physics: %.2f ms", profile.totalPhysicsTimeMs);
    ImGui::Text("  Positions:   %.3f ms", profile.updatePosTimeMs);
    ImGui::Text("  Grid Build:  %.3f ms", profile.gridBuildTimeMs);
    ImGui::Text("  Collisions:  %.3f ms", profile.collisionTimeMs);
    ImGui::Text("  Boundaries:  %.3f ms", profile.boundaryTimeMs);

    ImGui::SeparatorText("Simulation & Culling Stats");
    ImGui::Text("Total Energy:   %.2f J", profile.totalKineticEnergy);
    ImGui::Text("Active Units:   %zu", unit_manager.GetCurrentUnits());
    ImGui::Text("Rendered Units: %zu (%.1f%%)", m_visibleUnitsCount,
                unit_manager.GetCurrentUnits() > 0
                    ? (100.0f * static_cast<float>(m_visibleUnitsCount) /
                       static_cast<float>(unit_manager.GetCurrentUnits()))
                    : 0.0f);
    ImGui::Text("Occupied Cells: %zu", profile.activeCellCount);
    ImGui::Text("Pairs Checked:  %zu", profile.collisionPairsChecked);
    ImGui::Text("Crammed Eliminated: %zu", profile.crammedUnitsKilled);

    ImGui::SeparatorText("Vector Field (Flow Field)");
    if (ImGui::Checkbox("Enable Vector Field", &m_uiEnableVectorField)) {
        unit_manager.SetVectorFieldEnabled(m_uiEnableVectorField);
    }
    ImGui::Checkbox("Draw Field Overlay", &m_uiDrawVectorField);

    const char* presets[] = {
        "Circular (Clockwise)",
        "Circular (Counter-Clockwise)",
        "Vortex (Inward)",
        "Vortex (Outward)",
        "Radial (Inward)",
        "Radial (Outward)"
    };
    if (ImGui::Combo("Preset", &m_uiVectorFieldPreset, presets, IM_ARRAYSIZE(presets))) {
        applyVectorFieldPreset();
    }

    if (ImGui::SliderFloat("Flow Speed", &m_uiVectorFieldSpeed, 0.0f, 200.0f, "%.1f")) {
        unit_manager.SetVectorFieldSpeed(m_uiVectorFieldSpeed);
    }
    if (ImGui::SliderFloat("Steering Force", &m_uiVectorFieldSteering, 0.1f, 20.0f, "%.1f")) {
        unit_manager.SetVectorFieldSteeringStrength(m_uiVectorFieldSteering);
    }
    if (ImGui::SliderFloat("Max Unit Speed", &m_uiMaxSpeed, 10.0f, 300.0f, "%.1f")) {
        unit_manager.SetMaxSpeed(m_uiMaxSpeed);
    }
    if (ImGui::SliderFloat("Cell Size", &m_uiVectorFieldCellSize, 20.0f, 300.0f, "%.0f")) {
        applyVectorFieldPreset();
    }
    if (ImGui::Button("Reapply Preset")) {
        applyVectorFieldPreset();
    }

    ImGui::SeparatorText("Entity Cramming");
    if (ImGui::Checkbox("Enable Cramming", &m_uiEnableCramming)) {
        unit_manager.SetCrammingEnabled(m_uiEnableCramming);
    }
    if (m_uiEnableCramming) {
        if (ImGui::SliderInt("Max Cram Overlap", &m_uiMaxCrammingLimit, 2, 30)) {
            unit_manager.SetMaxCrammingLimit(m_uiMaxCrammingLimit);
        }
    }

    ImGui::SeparatorText("Simulation Controls");
    ImGui::Checkbox("Pause Simulation", &m_simulationPaused);
    ImGui::SameLine();
    if (ImGui::Button("Step 1 Frame")) {
        m_stepFrame = true;
    }

    if (ImGui::Button("Reset Camera")) {
        m_camera.setCenter(level_size * 0.5f);
        m_camera.setZoom(0.35f);
    }

    ImGui::SeparatorText("Spawn Configuration");
    ImGui::SliderInt("Unit Count", &m_uiUnitCount, 100, 100000);
    ImGui::SliderFloat("Spawn Speed", &m_uiUnitSpeed, 1.0f, 100.0f, "%.1f");
    ImGui::DragFloat2("Level Size", &m_uiLevelSize.x, 50.0f, 500.0f, 20000.0f, "%.0f");

    if (ImGui::Button("Apply & Respawn", ImVec2(180.0f, 0.0f))) {
        level_size = m_uiLevelSize;
        MaxUnits = static_cast<size_t>(m_uiUnitCount);
        applyVectorFieldPreset();
        spawnUnits(MaxUnits);
    }

    ImGui::End();
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
