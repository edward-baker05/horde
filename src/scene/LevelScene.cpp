#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelIO.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

LevelScene::~LevelScene() {
    onExit();
}

bool LevelScene::onEnter(Services& services) {
    m_services = &services;
    m_pendingOptions = m_options;

    // A missing or malformed level must never stop the game booting: fall back
    // to a bare default rather than failing to enter the scene.
    m_level = logic::makeDefaultLevel();
    if (m_options.level) {
        m_level = *m_options.level;
    } else if (!m_options.levelPath.empty()) {
        std::string error;
        if (std::optional<logic::Level> loaded = logic::loadLevel(m_options.levelPath, &error)) {
            m_level = std::move(*loaded);
        } else {
            SDL_Log("LevelScene: falling back to the default level (%s)", error.c_str());
        }
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));
    if (m_options.fitCamera) {
        fitCameraToLevel(static_cast<float>(width), static_cast<float>(height));
    }

    m_physics = sim::createPhysics(m_options.physics);
    if (m_physics == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown physics system '%s'", m_options.physics.c_str());
        return false;
    }

    // Walls don't move: flatten and bin them once, for every system to share.
    m_walls->build(m_level, enemy_size * 0.5f);

    sim::SimConfig config;
    config.worldSize = m_level.size;
    config.unitSize = enemy_size;
    config.walls = m_walls;
    config.initialUnits = sim::spawnUnits(m_options.spawn, m_options.unitCount, m_level.size, enemy_size,
                                          m_options.seed, m_walls.get());
    config.collision = m_options.collision;
    config.movement.maxSpeed = m_options.maxSpeed;
    // Head for the first exit; with none, the middle of the level.
    config.movement.goal = m_level.size * 0.5f;
    for (const logic::Marker& marker : m_level.markers) {
        if (marker.kind == logic::MarkerKind::Exit) {
            glm::vec2 size;
            logic::markerRect(m_level, marker, config.movement.goal, size);
            // Positions are top-left corners; aim the unit's centre at the exit.
            config.movement.goal -= glm::vec2(enemy_size * 0.5f);
            break;
        }
    }

    SDL_Log("LevelScene: %u units, physics '%s', spawn '%s', %zu walls (%zu primitives)", m_options.unitCount,
            m_options.physics.c_str(), sim::spawnPatternName(m_options.spawn), m_level.walls.size(),
            m_walls->primitives().size());

    return m_physics->init(services, config);
}

void LevelScene::onExit() {
    if (m_physics) {
        m_physics->shutdown();
        m_physics.reset();
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    m_physics->update(dt);
}

void LevelScene::compute(SDL_GPUCommandBuffer* commands) {
    m_physics->compute(commands);
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    // Render world geometry through SpriteBatch
    gfx::renderLevel(m_level, batch, m_services->atlas->handle());
}

void LevelScene::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
    m_physics->renderPass(commands, pass, m_camera.viewProjection());
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.1f fps (%.2f ms)", io.Framerate, 1000.0f / std::max(io.Framerate, 0.001f));
    ImGui::Text("Units: %u", m_physics->unitCount());
    ImGui::Text("Physics: %s", m_options.physics.c_str());
    ImGui::TextWrapped("%s", sim::physicsDescription(m_options.physics));
    m_physics->debugUi();

    ImGui::SeparatorText("Restart with");

    if (ImGui::BeginCombo("Physics", m_pendingOptions.physics.c_str())) {
        for (const std::string& name : sim::physicsNames()) {
            if (ImGui::Selectable(name.c_str(), name == m_pendingOptions.physics)) {
                m_pendingOptions.physics = name;
            }
        }
        ImGui::EndCombo();
    }

    int units = static_cast<int>(m_pendingOptions.unitCount);
    if (ImGui::InputInt("Units", &units, 1000, 10000)) {
        m_pendingOptions.unitCount = static_cast<std::uint32_t>(std::clamp(units, 0, 4'000'000));
    }

    constexpr sim::SpawnPattern patterns[] = {sim::SpawnPattern::Column, sim::SpawnPattern::Random,
                                              sim::SpawnPattern::Grid};
    if (ImGui::BeginCombo("Spawn", sim::spawnPatternName(m_pendingOptions.spawn))) {
        for (const sim::SpawnPattern pattern : patterns) {
            if (ImGui::Selectable(sim::spawnPatternName(pattern), pattern == m_pendingOptions.spawn)) {
                m_pendingOptions.spawn = pattern;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Quality preset", "choose...")) {
        for (const char* preset : {"low", "medium", "high", "ultra"}) {
            if (ImGui::Selectable(preset)) {
                sim::collisionPreset(preset, m_pendingOptions.collision);
            }
        }
        ImGui::EndCombo();
    }

    constexpr sim::Broadphase broadphases[] = {sim::Broadphase::CountingSort, sim::Broadphase::Buckets,
                                               sim::Broadphase::BitonicSort};
    if (ImGui::BeginCombo("Broadphase", sim::broadphaseName(m_pendingOptions.collision.broadphase))) {
        for (const sim::Broadphase broadphase : broadphases) {
            if (ImGui::Selectable(sim::broadphaseName(broadphase),
                                  broadphase == m_pendingOptions.collision.broadphase)) {
                m_pendingOptions.collision.broadphase = broadphase;
            }
        }
        ImGui::EndCombo();
    }

    constexpr sim::CollisionSolver solvers[] = {sim::CollisionSolver::Relaxed, sim::CollisionSolver::Average};
    if (ImGui::BeginCombo("Solver", sim::solverName(m_pendingOptions.collision.solver))) {
        for (const sim::CollisionSolver solver : solvers) {
            if (ImGui::Selectable(sim::solverName(solver), solver == m_pendingOptions.collision.solver)) {
                m_pendingOptions.collision.solver = solver;
            }
        }
        ImGui::EndCombo();
    }

    int iterations = static_cast<int>(m_pendingOptions.collision.iterations);
    if (ImGui::SliderInt("Collision passes", &iterations, 0, 16)) {
        m_pendingOptions.collision.iterations = static_cast<std::uint32_t>(iterations);
    }
    if (m_pendingOptions.collision.solver == sim::CollisionSolver::Relaxed) {
        ImGui::SliderFloat("Relaxation", &m_pendingOptions.collision.relaxation, 0.05f, 1.0f);
    } else {
        ImGui::SliderFloat("Omega", &m_pendingOptions.collision.averageOmega, 0.5f, 2.0f);
    }
    ImGui::SliderFloat("Speed", &m_pendingOptions.maxSpeed, 0.0f, 300.0f);

    if (ImGui::Button("Restart level", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->replace(std::make_unique<LevelScene>(m_pendingOptions));
    }

    if (ImGui::Button("Back", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->pop();
    }

    ImGui::End();
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
    if (m_options.fitCamera) {
        fitCameraToLevel(width, height);
    }
}

void LevelScene::fitCameraToLevel(float width, float height) {
    const float zoom = 0.95f * std::min(width / m_level.size.x, height / m_level.size.y);
    m_camera.setZoomLimits(std::min(zoom, 0.1f), 10.0f);
    m_camera.setZoom(zoom);
    m_camera.setCenter(m_level.size * 0.5f);
}

} // namespace horde::scene
