#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/PolishMode.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelGeometry.hpp"
#include "logic/LevelIO.hpp"
#include "scene/PauseScene.hpp"
#include "scene/Presets.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

LevelScene::~LevelScene() {
    onExit();
}

bool LevelScene::onEnter(Services& services) {
    m_services = &services;
    m_pendingOptions = m_options;
    // The explosion tool is a Dev tool.
    m_explosionArmed = m_options.mode == LevelMode::Dev && m_options.explosionArmed;
    m_explosion = m_options.explosion;
    m_flingDrag = m_options.flingDrag;

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

    // The camera viewport and fit come from the stack's onResize right after
    // this, sized to the tablet screen (or the window for a device-less level).

    m_physics = sim::createPhysics(m_options.physics);
    if (m_physics == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown physics system '%s'", m_options.physics.c_str());
        return false;
    }

    // Walls don't move: flatten and bin them once, for every system to share.
    m_walls->build(m_level, enemy_size * 0.5f);
    m_gridPieces = logic::mergeWallGrid(m_level.wallGrid, true);

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

    if (!m_physics->init(services, config)) {
        return false;
    }
    m_physics->setFlingDrag(m_flingDrag);
    m_cow.show(services);
    return true;
}

void LevelScene::onCovered() {
    m_cow.hide();
}

void LevelScene::onRevealed() {
    m_pausePending = false;
    m_cow.show(*m_services);
}

void LevelScene::onExit() {
    m_cow.hide();
    if (m_physics) {
        m_physics->shutdown();
        m_physics.reset();
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    // Esc pauses a round on the device. Only the top scene gets events, and
    // m_pausePending covers the frame between the push and it taking effect.
    if (m_options.mode == LevelMode::Player && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE && !event.key.repeat) {
        if (!m_pausePending) {
            m_pausePending = true;
            // The copy carries the explosion tool's live settings, as the debug restart does.
            LevelOptions restart = m_options;
            restart.explosionArmed = m_explosionArmed;
            restart.explosion = m_explosion;
            restart.flingDrag = m_flingDrag;
            SceneStack* scenes = m_services->scenes;
            m_services->scenes->push(std::make_unique<PauseScene>(
                [scenes, restart = std::move(restart)] { scenes->replace(std::make_unique<LevelScene>(restart)); },
                m_options.level.has_value()));
        }
        return true;
    }
    if (m_options.mode == LevelMode::Player) {
        return handleCameraGesture(event);
    }
    // Right-click: the camera pans with the left and middle buttons.
    if (m_explosionArmed && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
        sim::Explosion explosion = m_explosion;
        explosion.center = m_camera.screenToWorld({event.button.x, event.button.y});
        return m_physics->addExplosion(explosion);
    }
    return m_cameraController.handleEvent(event, m_camera);
}

bool LevelScene::handleCameraGesture(const SDL_Event& event) {
    (void)event;
    return false;
}

void LevelScene::update(float dt) {
    m_cow.update(dt);
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
    gfx::renderLevel(m_level, batch, m_services->atlas->handle(), m_showFieldRegions, &m_gridPieces);

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

namespace {

// Names for the dev panel's combos: Polish in Polish mode, otherwise the
// sim's own names. Those stay English either way, as benchmark output and
// presets use them.
const char* spawnLabel(sim::SpawnPattern pattern) {
    if (polish::enabled()) {
        switch (pattern) {
            case sim::SpawnPattern::Column:
                return "kolumna";
            case sim::SpawnPattern::Random:
                return "losowo";
            case sim::SpawnPattern::Grid:
                return "siatka";
            case sim::SpawnPattern::Markers:
                return "znaczniki";
        }
    }
    return sim::spawnPatternName(pattern);
}

const char* solverLabel(sim::CollisionSolver solver) {
    if (polish::enabled()) {
        switch (solver) {
            case sim::CollisionSolver::Relaxed:
                return "relaksacyjny";
            case sim::CollisionSolver::Average:
                return "uśredniający";
            case sim::CollisionSolver::Chebyshev:
                return "Czebyszewa";
        }
    }
    return sim::solverName(solver);
}

const char* falloffLabel(sim::ExplosionFalloff falloff) {
    if (polish::enabled()) {
        switch (falloff) {
            case sim::ExplosionFalloff::Constant:
                return "stały";
            case sim::ExplosionFalloff::Linear:
                return "liniowy";
            case sim::ExplosionFalloff::Quadratic:
                return "kwadratowy";
        }
    }
    return sim::falloffName(falloff);
}

} // namespace

void LevelScene::debugUi() {
    if (m_options.mode == LevelMode::Player) {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(tr("Level##dev"));

    ImGui::Text(tr("Units: %u"), m_physics->unitCount());
    healthUi();

    // Drawing only: hidden regions still push units.
    if (!m_level.fieldRegions.empty()) {
        ImGui::Checkbox(tr("Show field regions"), &m_showFieldRegions);
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", m_level.fieldRegions.size());
    }
    explosionUi();

    // Applies immediately: stepping is the scene's, not the physics system's.
    int rate = m_options.physicsRate >= 120.0f ? 1 : 0;
    ImGui::TextUnformatted(tr("Physics rate"));
    ImGui::SameLine();
    const bool to60 = ImGui::RadioButton("60 Hz", &rate, 0);
    ImGui::SameLine();
    const bool to120 = ImGui::RadioButton("120 Hz", &rate, 1);
    if (to60 || to120) {
        m_options.physicsRate = rate == 1 ? 120.0f : 60.0f;
        m_pendingOptions.physicsRate = m_options.physicsRate;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(tr("%.2f steps/frame"), m_stepsPerFrame);

    if (const sim::CollisionSettings* collision = m_physics->collisionSettings()) {
        const float rateHz = m_options.physicsRate;
        if (rateHz > 0.0f) {
            ImGui::Text(tr("%u passes x %.0f Hz = %.0f passes/s"), collision->iterations, rateHz,
                        static_cast<float>(collision->iterations) * rateHz);
        } else {
            ImGui::Text(tr("%u passes, one step per frame"), collision->iterations);
        }
    }

    ImGui::SeparatorText(tr("Restart with"));

    // Loading a preset fills in everything below; edit further before restarting.
    const char* presetLabel = m_pendingOptions.presetName.empty() ? tr("(none)") : m_pendingOptions.presetName.c_str();
    if (ImGui::BeginCombo(tr("Preset"), presetLabel)) {
        for (const std::filesystem::path& path : listPresets()) {
            const std::string name = presetName(path);
            if (ImGui::Selectable(name.c_str(), name == m_pendingOptions.presetName)) {
                Preset preset;
                std::string error;
                LevelOptions base;
                base.fitCamera = m_options.fitCamera;
                base.mode = m_options.mode;
                if (loadPreset(name, base, preset, error)) {
                    m_pendingOptions = std::move(preset.options);
                } else {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
                }
            }
        }
        ImGui::EndCombo();
    }

    int units = static_cast<int>(m_pendingOptions.unitCount);
    if (ImGui::InputInt(tr("Units"), &units, 1000, 10000)) {
        m_pendingOptions.unitCount = static_cast<std::uint32_t>(std::clamp(units, 0, 4'000'000));
    }

    constexpr sim::SpawnPattern patterns[] = {sim::SpawnPattern::Markers, sim::SpawnPattern::Random,
                                              sim::SpawnPattern::Grid, sim::SpawnPattern::Column};
    if (ImGui::BeginCombo(tr("Spawn##pattern"), spawnLabel(m_pendingOptions.spawn))) {
        for (const sim::SpawnPattern pattern : patterns) {
            if (ImGui::Selectable(spawnLabel(pattern), pattern == m_pendingOptions.spawn)) {
                m_pendingOptions.spawn = pattern;
            }
        }
        ImGui::EndCombo();
    }
    if (m_pendingOptions.spawn == sim::SpawnPattern::Markers) {
        ImGui::SliderFloat(tr("Spawn rate"), &m_pendingOptions.spawnRate, 0.0f, 20000.0f, "%.0f/s",
                           ImGuiSliderFlags_Logarithmic);
    }
    int maxHp = static_cast<int>(m_pendingOptions.health.maxHp);
    if (ImGui::InputInt(tr("Max HP"), &maxHp)) {
        m_pendingOptions.health.maxHp = static_cast<std::uint32_t>(std::clamp(maxHp, 1, 100000));
    }
    if (ImGui::InputFloat(tr("Damage at exit"), &m_pendingOptions.health.exitDamage, 0.5f, 5.0f, "%.1f")) {
        m_pendingOptions.health.exitDamage = std::max(m_pendingOptions.health.exitDamage, 0.0f);
    }

    int iterations = static_cast<int>(m_pendingOptions.collision.iterations);
    if (ImGui::SliderInt(tr("Collision passes"), &iterations, 0, 32)) {
        m_pendingOptions.collision.iterations = static_cast<std::uint32_t>(iterations);
    }
    if (m_pendingOptions.collision.solver == sim::CollisionSolver::Average) {
        ImGui::SliderFloat("Omega", &m_pendingOptions.collision.averageOmega, 0.5f, 2.0f);
    } else {
        ImGui::SliderFloat(tr("Relaxation"), &m_pendingOptions.collision.relaxation, 0.0f, 0.5f);
    }
    if (m_pendingOptions.collision.solver == sim::CollisionSolver::Chebyshev) {
        ImGui::SliderFloat("Rho", &m_pendingOptions.collision.chebyshevRho, 0.05f, 0.99f);
        int delay = static_cast<int>(m_pendingOptions.collision.chebyshevDelay);
        if (ImGui::SliderInt(tr("Chebyshev delay"), &delay, static_cast<int>(sim::kMinChebyshevDelay), 8)) {
            m_pendingOptions.collision.chebyshevDelay = static_cast<std::uint32_t>(delay);
        }
    }
    ImGui::SeparatorText(tr("Crowd flow"));
    bool congestion = m_pendingOptions.collision.congestionStop > 0.0f;
    if (ImGui::Checkbox(tr("Slow down for crowds ahead"), &congestion)) {
        m_pendingOptions.collision.congestionStop = congestion ? m_pendingOptions.collision.congestionStart + 0.3f : 0.0f;
    }
    if (congestion) {
        ImGui::SliderFloat(tr("Start (units/cell)"), &m_pendingOptions.collision.congestionStart, 0.1f, 1.5f);
        ImGui::SliderFloat(tr("Stop (units/cell)"), &m_pendingOptions.collision.congestionStop,
                           m_pendingOptions.collision.congestionStart + 0.05f, 2.0f);
    }
    ImGui::SliderFloat(tr("Speed"), &m_pendingOptions.maxSpeed, 0.0f, 300.0f);

    // Settings that rarely need touching, folded away. Presets set the solver.
    if (ImGui::CollapsingHeader(tr("Advanced"))) {
        ImGui::TextDisabled(tr("Live"));
        m_physics->debugUi();

        ImGui::TextDisabled(tr("On restart"));
        ImGui::Checkbox(tr("Reorder units by cell"), &m_pendingOptions.collision.reorderUnits);
        ImGui::Checkbox(tr("Respawn at exit"), &m_pendingOptions.respawnAtExit);
        constexpr sim::CollisionSolver solvers[] = {sim::CollisionSolver::Relaxed, sim::CollisionSolver::Average,
                                                    sim::CollisionSolver::Chebyshev};
        if (ImGui::BeginCombo(tr("Solver"), solverLabel(m_pendingOptions.collision.solver))) {
            for (const sim::CollisionSolver solver : solvers) {
                if (ImGui::Selectable(solverLabel(solver), solver == m_pendingOptions.collision.solver)) {
                    m_pendingOptions.collision.solver = solver;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat(tr("Velocity smoothing"), &m_pendingOptions.collision.velocitySmoothing, 0.0f, 1.0f);
    }

    if (ImGui::Button(tr("Restart level"), ImVec2(160.0f, 0.0f))) {
        // The explosion tool is set live, not as a pending option: take it as
        // it is now, whatever preset was loaded since.
        m_pendingOptions.explosionArmed = m_explosionArmed;
        m_pendingOptions.explosion = m_explosion;
        m_pendingOptions.flingDrag = m_flingDrag;
        m_services->scenes->replace(std::make_unique<LevelScene>(m_pendingOptions));
    }

    if (ImGui::Button(tr("Back"), ImVec2(160.0f, 0.0f))) {
        m_services->scenes->pop();
    }

    ImGui::End();
}

void LevelScene::explosionUi() {
    ImGui::SeparatorText(tr("Explosion"));
    if (!m_physics->supportsExplosions()) {
        ImGui::TextDisabled(tr("Not supported by this physics system"));
        return;
    }

    ImGui::Checkbox(tr("Armed (right-click to fire)"), &m_explosionArmed);
    ImGui::Checkbox(tr("Fling (past max speed)"), &m_explosion.fling);
    ImGui::SliderFloat(tr("Blast radius"), &m_explosion.radius, 5.0f, 1000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    // A fling's strength is a speed, so it wants a wider range than a shove's distance.
    const float range = m_explosion.fling ? 2000.0f : 500.0f;
    ImGui::SliderFloat(tr("Blast strength"), &m_explosion.strength, -range, range,
                       m_explosion.fling ? "%.0f /s" : "%.0f");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(m_explosion.fling
                              ? tr("Speed, in world units per second, a unit at the centre is launched at in all.\n"
                                "Negative pulls units in.")
                              : tr("World units a unit at the centre is moved in all.\nNegative pulls units in."));
    }
    ImGui::SliderFloat(tr("Blast duration"), &m_explosion.duration, 0.0f, 5.0f, "%.2f s");
    ImGui::SliderFloat(tr("Blast damage"), &m_explosion.damage, 0.0f, 100.0f, tr("%.1f hp"));
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("Hit points a unit at the centre loses over the blast, fading toward the edge\n"
                          "by the falloff. Units left with none are despawned."));
    }

    constexpr sim::ExplosionFalloff curves[] = {sim::ExplosionFalloff::Constant, sim::ExplosionFalloff::Linear,
                                                sim::ExplosionFalloff::Quadratic};
    const auto curveCombo = [&](const char* label, sim::ExplosionFalloff& value) {
        if (ImGui::BeginCombo(label, falloffLabel(value))) {
            for (const sim::ExplosionFalloff curve : curves) {
                if (ImGui::Selectable(falloffLabel(curve), curve == value)) {
                    value = curve;
                }
            }
            ImGui::EndCombo();
        }
    };
    curveCombo(tr("Falloff"), m_explosion.falloff);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("How the blast fades from its centre to its edge."));
    }
    curveCombo(tr("Decay"), m_explosion.decay);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("How the blast fades over its duration: constant is even all the way,\n"
                          "linear and quadratic put more of it at the start."));
    }

    if (m_explosion.fling) {
        if (ImGui::SliderFloat(tr("Fling drag"), &m_flingDrag, 0.1f, 20.0f, "%.1f /s", ImGuiSliderFlags_Logarithmic)) {
            m_physics->setFlingDrag(m_flingDrag);
        }
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
            // A unit at 0 has been despawned, so the mean and lowest are of
            // the units still in play.
            double sum = 0.0;
            std::uint32_t alive = 0;
            m_healthLowest = 0.0f;
            m_healthAtZero = 0;
            for (const float hp : health) {
                if (hp <= 0.0f) {
                    ++m_healthAtZero;
                    continue;
                }
                m_healthLowest = alive == 0 ? hp : std::min(m_healthLowest, hp);
                sum += hp;
                ++alive;
            }
            m_healthMean = alive > 0 ? static_cast<float>(sum / alive) : 0.0f;
        }
    }

    if (m_healthSampled) {
        // Max HP is whole; hit points after damage need not be.
        ImGui::Text(tr("Health: mean %.1f / %u, lowest %.1f, %u despawned"), m_healthMean, m_options.health.maxHp,
                    m_healthLowest, m_healthAtZero);
    }
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
    if (m_options.mode == LevelMode::Player) {
        // One static view fitted to the level, rebuilt for the new screen size.
        fitCameraToLevel(width, height);
        m_feed.views.assign(1, m_camera);
        m_feed.active = 0;
        applyFeedCamera();
    } else if (m_options.fitCamera) {
        fitCameraToLevel(width, height);
    }
}

void LevelScene::applyFeedCamera() {
    if (m_feed.active < m_feed.views.size()) {
        const gfx::Camera2D& view = m_feed.views[m_feed.active];
        m_camera.setCenter(view.center());
        m_camera.setZoom(view.zoom());
    }
}

void LevelScene::fitCameraToLevel(float width, float height) {
    const float zoom = 0.95f * std::min(width / m_level.size.x, height / m_level.size.y);
    m_camera.setZoomLimits(std::min(zoom, 0.1f), 10.0f);
    m_camera.setZoom(zoom);
    m_camera.setCenter(m_level.size * 0.5f);
}

} // namespace horde::scene
