#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelGeometry.hpp"
#include "logic/LevelIO.hpp"
#include "scene/Presets.hpp"
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
    config.collision = m_options.collision;
    config.movement.maxSpeed = m_options.maxSpeed;

    // The green spawns and red exits, as areas units appear in and head for.
    for (const logic::Marker& marker : m_level.markers) {
        glm::vec2 center;
        glm::vec2 size;
        logic::markerRect(m_level, marker, center, size);
        auto& areas = marker.kind == logic::MarkerKind::Spawn ? config.flow.spawns : config.flow.exits;
        areas.push_back({center, size * 0.5f});
    }
    for (const logic::FieldRegion& region : m_level.fieldRegions) {
        config.fieldRegions.push_back(
            {region.center, region.halfExtents, logic::fieldRegionDirection(region), region.strength});
    }
    config.flow.spawnRate = m_options.spawnRate;
    config.flow.respawnAtExit = m_options.respawnAtExit;
    config.health = m_options.health;
    config.flow.seed = m_options.seed;
    // Systems that don't steer to exits head for the first exit's centre, or
    // the middle of a level without one. Positions are top-left corners.
    config.movement.goal = (config.flow.exits.empty() ? m_level.size * 0.5f : config.flow.exits.front().center) -
                           glm::vec2(enemy_size * 0.5f);

    if (m_options.spawn == sim::SpawnPattern::Markers) {
        if (m_physics->supportsFlow()) {
            // Streamed in at the spawns as the level runs.
            config.flow.toSpawn = m_options.unitCount;
        } else {
            std::mt19937 rng(m_options.seed);
            config.initialUnits = sim::spawnInAreas(m_options.unitCount, config.flow.spawns, m_level.size,
                                                    enemy_size, rng, m_walls.get());
        }
    } else {
        config.initialUnits = sim::spawnUnits(m_options.spawn, m_options.unitCount, m_level.size, enemy_size,
                                              m_options.seed, m_walls.get());
    }

    SDL_Log("LevelScene: %u units, physics '%s', spawn '%s' (%zu spawns, %zu exits), %zu walls (%zu primitives)",
            m_options.unitCount, m_options.physics.c_str(), sim::spawnPatternName(m_options.spawn),
            config.flow.spawns.size(), config.flow.exits.size(), m_level.walls.size(), m_walls->primitives().size());

    return m_physics->init(services, config);
}

void LevelScene::onExit() {
    if (m_physics) {
        m_physics->shutdown();
        m_physics.reset();
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    // Right-click: the camera pans with the left and middle buttons.
    if (m_explosionArmed && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
        sim::Explosion explosion = m_explosion;
        explosion.center = m_camera.screenToWorld({event.button.x, event.button.y});
        return m_physics->addExplosion(explosion);
    }
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    // Rate 0: one step per frame, as long as the frame took.
    if (m_options.physicsRate <= 0.0f) {
        m_stepsThisFrame = 1;
        m_stepAccumulator = 0.0f;
        m_physics->update(dt);
    } else {
        const float step = 1.0f / m_options.physicsRate;
        m_stepAccumulator += dt;
        m_stepsThisFrame = std::min(static_cast<std::uint32_t>(m_stepAccumulator / step), kMaxStepsPerFrame);
        m_stepAccumulator -= static_cast<float>(m_stepsThisFrame) * step;
        // Too far behind to catch up: let the simulation run slow rather than spiral.
        m_stepAccumulator = std::min(m_stepAccumulator, step);
        for (std::uint32_t i = 0; i < m_stepsThisFrame; ++i) {
            m_physics->update(step);
        }
    }
    m_stepsPerFrame += (static_cast<float>(m_stepsThisFrame) - m_stepsPerFrame) * 0.05f;
}

void LevelScene::compute(SDL_GPUCommandBuffer* commands) {
    for (std::uint32_t i = 0; i < m_stepsThisFrame; ++i) {
        m_physics->compute(commands);
    }
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    // Render world geometry through SpriteBatch
    gfx::renderLevel(m_level, batch, m_services->atlas->handle(), m_showFieldRegions);

    // Explosions: red pushes, blue pulls. Units are drawn over these.
    const auto explosionColor = [](float strength, float alpha) {
        return strength >= 0.0f ? glm::vec4(1.0f, 0.35f, 0.2f, alpha) : glm::vec4(0.3f, 0.5f, 1.0f, alpha);
    };
    for (const sim::Explosion& explosion : m_physics->activeExplosions()) {
        const float diameter = explosion.radius * 2.0f;
        gfx::drawCentered(batch, m_services->atlas->handle(), explosion.center, {diameter, diameter}, 0.0f,
                          gfx::cellDisc(), explosionColor(explosion.strength, 0.3f));
    }
    // Where a right-click would land, while armed and not over a panel.
    if (m_explosionArmed && !ImGui::GetIO().WantCaptureMouse) {
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        const float diameter = m_explosion.radius * 2.0f;
        gfx::drawCentered(batch, m_services->atlas->handle(), m_camera.screenToWorld({mouseX, mouseY}),
                          {diameter, diameter}, 0.0f, gfx::cellDisc(), explosionColor(m_explosion.strength, 0.12f));
    }
}

void LevelScene::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
    // How far between the last physics step and the next this frame falls.
    const float alpha = m_options.physicsRate > 0.0f ? std::clamp(m_stepAccumulator * m_options.physicsRate, 0.0f, 1.0f)
                                                     : 1.0f;
    m_physics->renderPass(commands, pass, m_camera.viewProjection(), alpha);
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    frameCostUi();
    ImGui::Text("Units: %u", m_physics->unitCount());
    if (!m_options.presetName.empty()) {
        ImGui::Text("Preset: %s", m_options.presetName.c_str());
    }
    ImGui::Text("Physics: %s", m_options.physics.c_str());
    ImGui::TextWrapped("%s", sim::physicsDescription(m_options.physics));
    m_physics->debugUi();
    healthUi();

    // Drawing only: hidden regions still push units.
    if (!m_level.fieldRegions.empty()) {
        ImGui::Checkbox("Show field regions", &m_showFieldRegions);
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", m_level.fieldRegions.size());
    }
    explosionUi();

    // Applies immediately: stepping is the scene's, not the physics system's.
    int rate = m_options.physicsRate >= 120.0f ? 1 : 0;
    ImGui::TextUnformatted("Physics rate");
    ImGui::SameLine();
    const bool to60 = ImGui::RadioButton("60 Hz", &rate, 0);
    ImGui::SameLine();
    const bool to120 = ImGui::RadioButton("120 Hz", &rate, 1);
    if (to60 || to120) {
        m_options.physicsRate = rate == 1 ? 120.0f : 60.0f;
        m_pendingOptions.physicsRate = m_options.physicsRate;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f steps/frame", m_stepsPerFrame);

    // Same collision passes per second two ways: more, smaller steps or fewer
    // steps with more passes each. Both apply live, for side-by-side looks.
    if (const sim::CollisionSettings* collision = m_physics->collisionSettings()) {
        const float rateHz = m_options.physicsRate;
        if (rateHz > 0.0f) {
            ImGui::Text("%u passes x %.0f Hz = %.0f passes/s", collision->iterations, rateHz,
                        static_cast<float>(collision->iterations) * rateHz);
        } else {
            ImGui::Text("%u passes, one step per frame", collision->iterations);
        }

        const bool fast = rateHz >= 120.0f && collision->iterations == 16;
        if (ImGui::Button(fast ? "Swap to 60 Hz x 32 passes" : "Swap to 120 Hz x 16 passes")) {
            const float newRate = fast ? 60.0f : 120.0f;
            const std::uint32_t newPasses = fast ? 32 : 16;
            if (m_physics->setCollisionPasses(newPasses)) {
                m_options.physicsRate = newRate;
                m_options.collision.iterations = newPasses;
                m_pendingOptions.physicsRate = newRate;
                m_pendingOptions.collision.iterations = newPasses;
            }
        }
    }

    ImGui::SeparatorText("Restart with");

    // Loading a preset fills in everything below; edit further before restarting.
    const char* presetLabel = m_pendingOptions.presetName.empty() ? "(none)" : m_pendingOptions.presetName.c_str();
    if (ImGui::BeginCombo("Preset", presetLabel)) {
        for (const std::filesystem::path& path : listPresets()) {
            const std::string name = presetName(path);
            if (ImGui::Selectable(name.c_str(), name == m_pendingOptions.presetName)) {
                Preset preset;
                std::string error;
                LevelOptions base;
                base.fitCamera = m_options.fitCamera;
                if (loadPreset(name, base, preset, error)) {
                    m_pendingOptions = std::move(preset.options);
                } else {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
                }
            }
        }
        ImGui::EndCombo();
    }

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

    constexpr sim::SpawnPattern patterns[] = {sim::SpawnPattern::Markers, sim::SpawnPattern::Random,
                                              sim::SpawnPattern::Grid, sim::SpawnPattern::Column};
    if (ImGui::BeginCombo("Spawn", sim::spawnPatternName(m_pendingOptions.spawn))) {
        for (const sim::SpawnPattern pattern : patterns) {
            if (ImGui::Selectable(sim::spawnPatternName(pattern), pattern == m_pendingOptions.spawn)) {
                m_pendingOptions.spawn = pattern;
            }
        }
        ImGui::EndCombo();
    }
    if (m_pendingOptions.spawn == sim::SpawnPattern::Markers) {
        ImGui::SliderFloat("Spawn rate", &m_pendingOptions.spawnRate, 0.0f, 20000.0f, "%.0f/s",
                           ImGuiSliderFlags_Logarithmic);
    }
    ImGui::Checkbox("Respawn at exit", &m_pendingOptions.respawnAtExit);
    int maxHp = static_cast<int>(m_pendingOptions.health.maxHp);
    if (ImGui::InputInt("Max HP", &maxHp)) {
        m_pendingOptions.health.maxHp = static_cast<std::uint32_t>(std::clamp(maxHp, 1, 100000));
    }
    if (ImGui::InputFloat("Damage at exit", &m_pendingOptions.health.exitDamage, 0.5f, 5.0f, "%.1f")) {
        m_pendingOptions.health.exitDamage = std::max(m_pendingOptions.health.exitDamage, 0.0f);
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
    if (ImGui::SliderInt("Collision passes", &iterations, 0, 32)) {
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

void LevelScene::explosionUi() {
    ImGui::SeparatorText("Explosion");
    if (!m_physics->supportsExplosions()) {
        ImGui::TextDisabled("Not supported by this physics system");
        return;
    }

    ImGui::Checkbox("Armed (right-click to fire)", &m_explosionArmed);
    ImGui::SliderFloat("Blast radius", &m_explosion.radius, 5.0f, 1000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Blast strength", &m_explosion.strength, -500.0f, 500.0f, "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("World units a unit at the centre is moved in all.\nNegative pulls units in.");
    }
    ImGui::SliderFloat("Blast duration", &m_explosion.duration, 0.0f, 5.0f, "%.2f s");

    constexpr sim::ExplosionFalloff falloffs[] = {sim::ExplosionFalloff::Constant, sim::ExplosionFalloff::Linear,
                                                  sim::ExplosionFalloff::Quadratic};
    if (ImGui::BeginCombo("Falloff", sim::falloffName(m_explosion.falloff))) {
        for (const sim::ExplosionFalloff falloff : falloffs) {
            if (ImGui::Selectable(sim::falloffName(falloff), falloff == m_explosion.falloff)) {
                m_explosion.falloff = falloff;
            }
        }
        ImGui::EndCombo();
    }
}

void LevelScene::healthUi() {
    // A GPU readback stalls briefly, so only about once a second.
    const Uint64 now = SDL_GetTicksNS();
    if (!m_healthSampled || now - m_healthSampledAt > 1'000'000'000) {
        m_healthSampledAt = now;
        std::vector<float> health;
        m_healthSampled = m_physics->readHealth(health) && !health.empty();
        if (m_healthSampled) {
            double sum = 0.0;
            m_healthLowest = health.front();
            m_healthAtZero = 0;
            for (const float hp : health) {
                sum += hp;
                m_healthLowest = std::min(m_healthLowest, hp);
                m_healthAtZero += hp <= 0.0f ? 1u : 0u;
            }
            m_healthMean = static_cast<float>(sum / static_cast<double>(health.size()));
        }
    }

    if (m_healthSampled) {
        // Max HP is whole; hit points after damage need not be.
        ImGui::Text("Health: mean %.1f / %u, lowest %.1f, %u at 0", m_healthMean, m_options.health.maxHp,
                    m_healthLowest, m_healthAtZero);
    }
}

void LevelScene::frameCostUi() {
    const FrameStats* stats = m_services->frameStats;
    if (stats == nullptr) {
        return;
    }

    ImGui::Text("%.1f fps: %.2f ms per frame (worst %.2f)", stats->frameMs > 0.0f ? 1000.0f / stats->frameMs : 0.0f,
                stats->frameMs, stats->frameMaxMs);

    // What the frame actually cost, as opposed to how often frames came.
    const float physics = stats->updateMs + stats->simGpuMs;
    const float busy = physics + stats->recordMs + stats->otherMs;
    if (ImGui::BeginTable("frame cost", 2, ImGuiTableFlags_SizingFixedFit)) {
        const auto row = [](const char* label, const char* format, float value, float worst = -1.0f) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            if (worst >= 0.0f) {
                ImGui::Text("%6.3f ms  (worst %.3f)", value, worst);
            } else {
                ImGui::Text(format, value);
            }
        };
        row("Physics, CPU", "%6.3f ms", stats->updateMs);
        if (stats->simGpuMeasured) {
            row("Physics, GPU", "", stats->simGpuMs, stats->simGpuMaxMs);
        } else {
            row("Physics, GPU", "not measured", 0.0f);
        }
        row("Physics total", "%6.3f ms", physics);
        row("Frame building, CPU", "%6.3f ms", stats->recordMs);
        row("Other (events, UI)", "%6.3f ms", stats->otherMs);
        row("Busy per frame", "%6.3f ms", busy);
        row("Waiting (cap, vsync)", "%6.3f ms", stats->waitMs);
        ImGui::EndTable();
    }

    if (busy > 0.0f) {
        ImGui::Text("Could run at ~%.0f fps uncapped", 1000.0f / busy);
    }
    if (m_services->measureSimGpu) {
        ImGui::Checkbox("Measure GPU physics", m_services->measureSimGpu);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Waits for the physics compute work each frame to time it exactly.\n"
                              "Drawing's GPU time is not included in any line.");
        }
    }
    ImGui::Separator();
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
