#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/PolishMode.hpp"
#include "core/SaveData.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelGeometry.hpp"
#include "logic/LevelIO.hpp"
#include "scene/PauseScene.hpp"
#include "scene/Presets.hpp"
#include "scene/SceneStack.hpp"
#include "sim/TowerSite.hpp"

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

    // A round's horde comes from the level, whatever the options say: the
    // level is the round until the campaign exists.
    if (m_options.mode == LevelMode::Player) {
        const logic::RoundSettings& round = m_level.round;
        m_options.unitCount = round.totalUnits;
        m_options.spawn = sim::SpawnPattern::Markers;
        m_options.spawnRate = round.baseSpawnRate;
        m_options.health.maxHp = round.unitHp;
        m_options.respawnAtExit = false;
    }

    // The camera viewport and fit come from the stack's onResize right after
    // this, sized to the tablet screen (or the window for a device-less level).

    m_physics = sim::createPhysics(m_options.physics);
    if (m_physics == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown physics system '%s'", m_options.physics.c_str());
        return false;
    }
    if (m_options.mode == LevelMode::Player && !m_physics->supportsRounds()) {
        SDL_Log("LevelScene: '%s' can't run a round; using '%s'", m_options.physics.c_str(), sim::kDefaultPhysics);
        m_options.physics = sim::kDefaultPhysics;
        m_physics = sim::createPhysics(m_options.physics);
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
    // In a round, whoever gets through is gone, and counts against the player.
    config.flow.removeAtExit = m_options.mode == LevelMode::Player;
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
            config.initialUnits = sim::spawnInAreas(m_options.unitCount, config.flow.spawns, m_level.size, enemy_size,
                                                    rng, m_walls.get());
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
    if (m_options.mode == LevelMode::Player) {
        const float step = m_options.physicsRate > 0.0f ? 1.0f / m_options.physicsRate : 1.0f / 60.0f;
        m_round.emplace(m_level.round, step);
        m_siteWalls->build(m_level, logic::kTowerBodyRadius + enemy_size);
    }
    if (m_options.mode == LevelMode::Player && !m_overlay.show(services)) {
        // The round still plays without its overlay.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LevelScene: no tablet overlay");
    }
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
    m_overlay.hide();
    if (m_physics) {
        m_physics->shutdown();
        m_physics.reset();
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    // Esc pauses a round on the device. Only the top scene gets events, and
    // m_pausePending covers the frame between the push and it taking effect.
    if (m_options.mode == LevelMode::Player && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE &&
        !event.key.repeat) {
        // Over: the results card's buttons are the only way on.
        const bool ended = m_round && m_round->outcome() != logic::RoundOutcome::Playing;
        if (!m_pausePending && !ended) {
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
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            m_cursor = glm::vec2(event.motion.x, event.motion.y);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            tapFeed({event.button.x, event.button.y});
            return true;
        }
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
    m_overlay.update(dt);
    if (m_round) {
        updateRound(dt);
        // Over: the feed freezes on its last frame behind the results.
        if (m_round->outcome() != logic::RoundOutcome::Playing) {
            m_stepsThisFrame = 0;
            return;
        }
    }
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
    if (m_round) {
        renderRound(batch);
    }

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
    const float alpha =
        m_options.physicsRate > 0.0f ? std::clamp(m_stepAccumulator * m_options.physicsRate, 0.0f, 1.0f) : 1.0f;
    m_physics->renderPass(commands, pass, m_camera.viewProjection(), m_camera.viewport(), alpha);
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
        m_pendingOptions.collision.congestionStop =
            congestion ? m_pendingOptions.collision.congestionStart + 0.3f : 0.0f;
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

namespace {

// How long a blast's disc stays on the feed after it goes off.
constexpr float kBlastFadeSeconds = 0.25f;
// How long a pushing blast's shockwave takes to reach its edge.
constexpr float kShockwaveSeconds = 0.35f;
// Blasts are kept for whichever of those lasts longer.
constexpr float kBlastKeepSeconds = std::max(kBlastFadeSeconds, kShockwaveSeconds);
constexpr float kPi = 3.14159265f;

// Each tower's tint in the feed; the tray's icons use the same colours.
glm::vec4 towerColor(logic::TowerKind kind) {
    switch (kind) {
        case logic::TowerKind::Sniper:
            return {0.957f, 0.792f, 0.518f, 1.0f}; // #f4ca84
        case logic::TowerKind::Gunner:
            return {0.565f, 0.843f, 1.0f, 1.0f}; // #90d7ff
        case logic::TowerKind::Pulse:
            return {0.463f, 0.886f, 0.886f, 1.0f}; // #76e2e2
        case logic::TowerKind::Frost:
            return {0.631f, 0.894f, 0.682f, 1.0f}; // #a1e4ae
    }
    return glm::vec4(1.0f);
}

// A tower's body, `radius` across the middle, in its kind's shape.
void drawTower(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, logic::TowerKind kind, glm::vec2 center, float radius,
               glm::vec4 color) {
    const float d = radius * 2.0f;
    switch (kind) {
        case logic::TowerKind::Sniper: // a diamond: a square on its corner
            gfx::drawCentered(batch, atlas, center, glm::vec2(d * 0.7071f), 0.7853982f, gfx::cellSolid(), color);
            break;
        case logic::TowerKind::Gunner:
            gfx::drawCentered(batch, atlas, center, glm::vec2(d * 0.85f), 0.0f, gfx::cellSquare(), color);
            break;
        case logic::TowerKind::Pulse: // a ring
            gfx::drawCentered(batch, atlas, center, glm::vec2(d), 0.0f, gfx::cellDisc(), color);
            gfx::drawCentered(batch, atlas, center, glm::vec2(d * 0.55f), 0.0f, gfx::cellDisc(),
                              {0.1f, 0.12f, 0.15f, color.a});
            break;
        case logic::TowerKind::Frost:
            gfx::drawCentered(batch, atlas, center, glm::vec2(d), 0.0f, gfx::cellTriangle(), color);
            break;
    }
}

glm::vec4 withAlpha(glm::vec4 color, float alpha) {
    return {color.r, color.g, color.b, alpha};
}

// A straight bar from `a` to `b`, `thickness` wide.
void drawLine(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec2 a, glm::vec2 b, float thickness,
              glm::vec4 color) {
    const glm::vec2 delta = b - a;
    const float length = glm::length(delta);
    if (length <= 0.0f) {
        return;
    }
    gfx::drawCentered(batch, atlas, (a + b) * 0.5f, {length, thickness}, std::atan2(delta.y, delta.x), gfx::cellSolid(),
                      color);
}

// A hollow circle, as a loop of bars: the atlas has no ring.
void drawRing(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec2 center, float radius, float thickness,
              glm::vec4 color) {
    if (radius <= 0.0f) {
        return;
    }
    const int segments = std::clamp(static_cast<int>(radius * 0.5f), 16, 72);
    const float step = 2.0f * kPi / static_cast<float>(segments);
    // Each bar a touch longer than its chord, so the joins don't show gaps.
    const float length = 2.0f * radius * std::sin(step * 0.5f) + thickness * 0.5f;
    for (int i = 0; i < segments; ++i) {
        const float angle = (static_cast<float>(i) + 0.5f) * step;
        const glm::vec2 at = center + radius * glm::vec2(std::cos(angle), std::sin(angle));
        gfx::drawCentered(batch, atlas, at, {length, thickness}, angle + kPi * 0.5f, gfx::cellSolid(), color);
    }
}

// How long a tower's muzzle flash and recoil last after it fires.
constexpr float kMuzzleSeconds = 0.07f;

// One shot in the air, drawn as its tower's kind of round.
void drawShot(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Shot& shot) {
    const glm::vec2 delta = shot.to - shot.from;
    const float distance = glm::length(delta);
    if (distance <= 0.0f || shot.flight <= 0.0f) {
        return;
    }
    const glm::vec2 dir = delta / distance;
    const glm::vec2 side{-dir.y, dir.x};
    const float t = std::clamp(shot.age / shot.flight, 0.0f, 1.0f);
    const glm::vec4 color = towerColor(shot.kind);
    const float muzzle = 1.0f - std::clamp(shot.age / kMuzzleSeconds, 0.0f, 1.0f);

    switch (shot.kind) {
        case logic::TowerKind::Sniper: {
            // A high-velocity round: a long bright streak, the faint line of
            // its path left behind it, and a sharp flash at the muzzle.
            const glm::vec2 muzzleAt = shot.from + dir * logic::kTowerBodyRadius * 0.75f;
            const glm::vec2 head = glm::mix(muzzleAt, shot.to, t);
            const float travelled = glm::distance(muzzleAt, head);
            const glm::vec2 tail = head - dir * std::min(travelled, 90.0f);
            drawLine(batch, atlas, muzzleAt, head, 1.0f, withAlpha(color, 0.25f));
            drawLine(batch, atlas, tail, head, 2.5f, withAlpha(color, 0.9f));
            gfx::drawCentered(batch, atlas, head, glm::vec2(4.0f), 0.0f, gfx::cellDisc(), {1.0f, 0.98f, 0.9f, 1.0f});
            if (muzzle > 0.0f) {
                gfx::drawCentered(batch, atlas, muzzleAt, glm::vec2(22.0f * muzzle), 0.0f, gfx::cellDisc(),
                                  {1.0f, 0.95f, 0.75f, 0.9f * muzzle});
                drawLine(batch, atlas, muzzleAt, muzzleAt + dir * 18.0f * muzzle, 5.0f * muzzle,
                         {1.0f, 0.95f, 0.75f, 0.8f * muzzle});
            }
            break;
        }
        case logic::TowerKind::Gunner: {
            // Tracer rounds from two barrels side by side, in turn.
            const float barrel =
                (static_cast<int>(std::floor(shot.to.x * 7.0f + shot.to.y * 13.0f)) & 1) ? 5.0f : -5.0f;
            const glm::vec2 muzzleAt = shot.from + dir * logic::kTowerBodyRadius * 0.8f + side * barrel;
            const glm::vec2 head = glm::mix(muzzleAt, shot.to, t);
            const glm::vec2 tail = head - dir * std::min(glm::distance(muzzleAt, head), 16.0f);
            drawLine(batch, atlas, tail, head, 2.5f, withAlpha(color, 0.95f));
            gfx::drawCentered(batch, atlas, head, glm::vec2(3.0f), 0.0f, gfx::cellDisc(), {1.0f, 1.0f, 1.0f, 1.0f});
            if (muzzle > 0.0f) {
                gfx::drawCentered(batch, atlas, muzzleAt, glm::vec2(10.0f * muzzle), 0.0f, gfx::cellDisc(),
                                  {1.0f, 0.95f, 0.7f, 0.9f * muzzle});
            }
            break;
        }
        case logic::TowerKind::Pulse: {
            // A charge lobbed in an arc: its shadow runs along the ground while
            // the glowing orb rises and falls above it, pulsing as it goes.
            const glm::vec2 ground = glm::mix(shot.from, shot.to, t);
            const float lift = std::sin(kPi * t);
            const float height = std::clamp(distance * 0.35f, 25.0f, 80.0f) * lift;
            gfx::drawCentered(batch, atlas, ground, glm::vec2(12.0f * (1.0f - 0.4f * lift)), 0.0f, gfx::cellDisc(),
                              {0.0f, 0.0f, 0.0f, 0.35f});
            // Screen up is world -y.
            const glm::vec2 orb = ground - glm::vec2(0.0f, height);
            const float size = 10.0f + 6.0f * lift;
            const float throb = 0.5f + 0.5f * std::sin(shot.age * 40.0f);
            gfx::drawCentered(batch, atlas, orb, glm::vec2(size * (2.0f + 0.4f * throb)), 0.0f, gfx::cellDisc(),
                              withAlpha(color, 0.25f));
            gfx::drawCentered(batch, atlas, orb, glm::vec2(size), 0.0f, gfx::cellDisc(), withAlpha(color, 0.95f));
            gfx::drawCentered(batch, atlas, orb, glm::vec2(size * 0.45f), 0.0f, gfx::cellDisc(),
                              {1.0f, 1.0f, 1.0f, 0.9f});
            if (muzzle > 0.0f) {
                drawRing(batch, atlas, shot.from, logic::kTowerBodyRadius * (1.0f + 0.6f * (1.0f - muzzle)), 3.0f,
                         withAlpha(color, 0.8f * muzzle));
            }
            break;
        }
        case logic::TowerKind::Frost:
            break; // never fires
    }
}

// Frost's chill, which never fires: a cold wave rolling out to the edge of
// its zone every couple of seconds, and frost glinting as it drifts round.
void drawChill(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec2 center, float range, float seconds,
               std::size_t slot) {
    constexpr float kPeriod = 2.2f;
    constexpr float kWaveSeconds = 1.4f;
    const glm::vec4 ice{0.75f, 0.95f, 1.0f, 1.0f};
    // Towers placed together don't all breathe in step.
    const float phase = std::fmod(seconds + static_cast<float>(slot) * 0.73f, kPeriod);
    if (phase < kWaveSeconds) {
        const float t = phase / kWaveSeconds;
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        const float radius = glm::mix(logic::kTowerBodyRadius, range, eased);
        drawRing(batch, atlas, center, radius, 2.5f, withAlpha(ice, 0.35f * (1.0f - t)));
    }
    constexpr int kMotes = 10;
    for (int i = 0; i < kMotes; ++i) {
        const float seed = static_cast<float>(i) * 2.399963f + static_cast<float>(slot) * 1.1f; // golden angle
        const float orbit = range * (0.3f + 0.65f * std::fmod(seed * 0.618034f, 1.0f));
        const float angle = seed + seconds * (0.15f + 0.1f * std::fmod(seed, 1.0f));
        const glm::vec2 at = center + orbit * glm::vec2(std::cos(angle), std::sin(angle));
        const float glint = 0.5f + 0.5f * std::sin(seconds * 2.5f + seed * 3.0f);
        gfx::drawCentered(batch, atlas, at, glm::vec2(3.0f + 2.0f * glint), seconds * 1.5f + seed, gfx::cellSolid(),
                          withAlpha(ice, 0.2f + 0.5f * glint));
    }
}

} // namespace

bool LevelScene::siteClear(glm::vec2 world) const {
    return sim::towerSiteClear(m_level, *m_siteWalls, world, enemy_size);
}

void LevelScene::updateRound(float dt) {
    logic::Round& round = *m_round;
    if (round.outcome() == logic::RoundOutcome::Playing) {
        sim::RoundCounters counters;
        if (m_physics->pollCounters(counters)) {
            round.applyCounters(counters);
        }
        if (m_physics->pollDensity(m_density)) {
            m_hasDensity = true;
        } else {
            m_density.age += dt;
        }
        // The last map until a newer one comes: a tenth of a second old at
        // most, and towers lead their targets by its age.
        round.update(dt, m_hasDensity ? &m_density : nullptr);
        for (const sim::Explosion& blast : round.takeBlasts()) {
            m_physics->addExplosion(blast);
            m_recentBlasts.emplace_back(blast, 0.0f);
        }
        m_physics->setCrowdEffects(round.effects());
        m_physics->setSpawnRate(round.spawnRate());
    }
    for (auto& [blast, age] : m_recentBlasts) {
        age += dt;
    }
    std::erase_if(m_recentBlasts, [](const auto& recent) { return recent.second >= kBlastKeepSeconds; });

    // A destroyed tower takes its panel with it.
    if (m_inspected >= 0 && !round.towers()[static_cast<std::size_t>(m_inspected)]) {
        m_inspected = -1;
    }
    presentRound();
}

void LevelScene::presentRound() {
    logic::Round& round = *m_round;

    // Always taken, so a press is never left over for another tower.
    if (m_overlay.takeOfferPressed() && m_inspected >= 0) {
        round.buyOffer(m_inspected);
    }

    const logic::RoundSettings& settings = round.settings();
    m_overlay.setHud({round.money(), round.killed() + round.leaked(), settings.totalUnits, round.leaked(),
                      settings.leakLimit, round.seconds()});
    for (int i = 0; i < logic::kTowerKindCount; ++i) {
        m_overlay.setTrayState(i, round.money() >= logic::towerStats(static_cast<logic::TowerKind>(i), 1).cost, 0.0f);
    }
    for (int i = 0; i < logic::kAbilityKindCount; ++i) {
        const auto kind = static_cast<logic::AbilityKind>(i);
        m_overlay.setTrayState(logic::kTowerKindCount + i, round.money() >= logic::abilityStats(kind).cost,
                               round.abilityCooldown(kind));
    }

    if (m_inspected >= 0 && round.outcome() == logic::RoundOutcome::Playing) {
        const logic::Tower& tower = *round.towers()[static_cast<std::size_t>(m_inspected)];
        const logic::Offer offer = round.offer(m_inspected);
        RoundOverlay::TowerPanel panel;
        panel.name = logic::towerName(tower.kind);
        panel.level = tower.level;
        panel.hp = static_cast<int>(std::ceil(tower.hp));
        panel.maxHp = static_cast<int>(logic::towerStats(tower.kind, tower.level).maxHp);
        panel.action = offer.kind == logic::Offer::Kind::Repair ? "REPAIR" : "UPGRADE";
        panel.cost = offer.cost;
        panel.affordable = round.money() >= offer.cost;
        // Above the tower, kept on the screen: the card is about 190 x 110.
        const glm::vec2 above =
            m_camera.worldToScreen(tower.center - glm::vec2(0.0f, logic::kTowerBodyRadius)) - glm::vec2(95.0f, 120.0f);
        const glm::vec2 limit = m_camera.viewport() - glm::vec2(200.0f, 120.0f);
        panel.screen = glm::clamp(above, glm::vec2(8.0f), glm::max(limit, glm::vec2(8.0f)));
        m_overlay.showTowerPanel(panel);
    } else {
        m_overlay.hideTowerPanel();
    }

    if (round.outcome() != logic::RoundOutcome::Playing && !m_resultsShown) {
        m_resultsShown = true;
        // Upgrade money is banked the moment the round is won, results card
        // or not, so quitting straight away loses nothing.
        save::SaveData saved = save::load(save::defaultPath());
        const int earned = round.upgradeMoney();
        if (earned > 0) {
            saved.upgradeMoney += earned;
            if (!save::write(saved, save::defaultPath())) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LevelScene: could not save upgrade money");
            }
        }
        m_inspected = -1;
        m_overlay.showResults({round.outcome() == logic::RoundOutcome::Won, round.seconds(), round.killed(),
                               round.leaked(), earned, saved.upgradeMoney});
    }

    switch (m_overlay.takeResultAction()) {
        case RoundOverlay::ResultAction::Retry:
            m_services->scenes->replace(std::make_unique<LevelScene>(m_options));
            break;
        case RoundOverlay::ResultAction::Quit:
            m_services->scenes->pop();
            break;
        case RoundOverlay::ResultAction::None:
            break;
    }
}

void LevelScene::tapFeed(glm::vec2 screen) {
    if (!m_round || m_round->outcome() != logic::RoundOutcome::Playing) {
        return;
    }
    logic::Round& round = *m_round;
    const glm::vec2 world = m_camera.screenToWorld(screen);
    const int selected = m_overlay.selected();

    if (selected >= 0 && selected < logic::kTowerKindCount) {
        // A refused spot keeps the selection, so the player can try nearby.
        if (round.place(static_cast<logic::TowerKind>(selected), world, siteClear(world)) >= 0) {
            m_overlay.clearSelection();
        }
        return;
    }
    if (selected >= logic::kTowerKindCount && selected < logic::kTowerKindCount + logic::kAbilityKindCount) {
        if (round.useAbility(static_cast<logic::AbilityKind>(selected - logic::kTowerKindCount), world)) {
            m_overlay.clearSelection();
        }
        return;
    }

    // Towers are small on the fitted feed: never less than a fingertip to tap.
    const float pick = std::max(logic::kTowerBodyRadius, 12.0f / m_camera.zoom());
    const int tower = round.towerAt(world, pick);
    if (tower >= 0) {
        m_inspected = tower;
        return;
    }
    m_inspected = -1;
    round.click(world);
}

void LevelScene::renderRound(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();
    const logic::Round& round = *m_round;

    // Blasts fading out, red for pushes and damage, blue for pulls. A big
    // push (a Pulse charge, a bomb) also throws a shockwave out to its edge.
    for (const auto& [blast, age] : m_recentBlasts) {
        if (age < kBlastFadeSeconds) {
            const float alpha = 0.35f * (1.0f - age / kBlastFadeSeconds);
            const glm::vec4 color =
                blast.strength < 0.0f ? glm::vec4(0.3f, 0.5f, 1.0f, alpha) : glm::vec4(1.0f, 0.35f, 0.2f, alpha);
            gfx::drawCentered(batch, atlas, blast.center, glm::vec2(blast.radius * 2.0f), 0.0f, gfx::cellDisc(), color);
        }
        if (blast.strength > 0.0f && blast.radius >= 40.0f && age < kShockwaveSeconds) {
            const float t = age / kShockwaveSeconds;
            const float eased = 1.0f - (1.0f - t) * (1.0f - t);
            drawRing(batch, atlas, blast.center, blast.radius * glm::mix(0.15f, 1.0f, eased), 4.0f * (1.0f - t) + 1.0f,
                     {1.0f, 0.95f, 0.85f, 0.7f * (1.0f - t)});
        }
    }

    // Zones first, under everything.
    for (std::size_t slot = 0; slot < round.towers().size(); ++slot) {
        const std::optional<logic::Tower>& tower = round.towers()[slot];
        if (!tower) {
            continue;
        }
        const logic::TowerStats stats = logic::towerStats(tower->kind, tower->level);
        if (stats.speedFactor < 1.0f && tower->grow >= 1.0f) {
            gfx::drawCentered(batch, atlas, tower->center, glm::vec2(stats.range * 2.0f), 0.0f, gfx::cellDisc(),
                              {0.63f, 0.89f, 0.68f, 0.07f});
            drawChill(batch, atlas, tower->center, stats.range, round.seconds(), slot);
        }
        if (static_cast<int>(slot) == m_inspected) {
            gfx::drawCentered(batch, atlas, tower->center, glm::vec2(stats.range * 2.0f), 0.0f, gfx::cellDisc(),
                              {1.0f, 1.0f, 1.0f, 0.08f});
        }
    }

    // A tower that has just fired kicks back from its shot.
    std::array<glm::vec2, sim::kMaxTowers> recoil{};
    for (const logic::Shot& shot : round.shots()) {
        const glm::vec2 delta = shot.to - shot.from;
        if (shot.age >= kMuzzleSeconds || shot.slot < 0 || glm::length(delta) <= 0.0f) {
            continue;
        }
        const float kick = shot.kind == logic::TowerKind::Sniper   ? 4.0f
                           : shot.kind == logic::TowerKind::Gunner ? 1.5f
                                                                   : 0.0f;
        recoil[static_cast<std::size_t>(shot.slot)] =
            -glm::normalize(delta) * kick * (1.0f - shot.age / kMuzzleSeconds);
    }

    for (std::size_t slot = 0; slot < round.towers().size(); ++slot) {
        const std::optional<logic::Tower>& tower = round.towers()[slot];
        if (!tower) {
            continue;
        }
        drawTower(batch, atlas, tower->kind, tower->center + recoil[slot], logic::kTowerBodyRadius * tower->grow,
                  towerColor(tower->kind));
        const float maxHp = logic::towerStats(tower->kind, tower->level).maxHp;
        if (tower->hp < maxHp) {
            const float width = logic::kTowerBodyRadius * 2.0f;
            const glm::vec2 at = tower->center - glm::vec2(0.0f, logic::kTowerBodyRadius + 8.0f);
            const float fraction = std::clamp(tower->hp / maxHp, 0.0f, 1.0f);
            gfx::drawCentered(batch, atlas, at, {width, 5.0f}, 0.0f, gfx::cellSolid(), {0.0f, 0.0f, 0.0f, 0.7f});
            gfx::drawCentered(batch, atlas, at - glm::vec2(width * (1.0f - fraction) * 0.5f, 0.0f),
                              {width * fraction, 5.0f}, 0.0f, gfx::cellSolid(),
                              {1.0f - fraction, 0.3f + 0.6f * fraction, 0.2f, 1.0f});
        }
    }

    // Shots over the towers that fired them.
    for (const logic::Shot& shot : round.shots()) {
        drawShot(batch, atlas, shot);
    }

    // What the selected tray item would do where the pointer is.
    const int selected = m_overlay.selected();
    if (!m_cursor || selected < 0 || round.outcome() != logic::RoundOutcome::Playing) {
        return;
    }
    const glm::vec2 world = m_camera.screenToWorld(*m_cursor);
    if (selected < logic::kTowerKindCount) {
        const auto kind = static_cast<logic::TowerKind>(selected);
        const bool ok = round.canPlace(kind, world, siteClear(world)) == logic::PlaceError::None;
        const float range = logic::towerStats(kind, 1).range;
        gfx::drawCentered(batch, atlas, world, glm::vec2(range * 2.0f), 0.0f, gfx::cellDisc(),
                          {1.0f, 1.0f, 1.0f, 0.08f});
        const glm::vec4 tint =
            ok ? towerColor(kind) * glm::vec4(1.0f, 1.0f, 1.0f, 0.6f) : glm::vec4(1.0f, 0.25f, 0.2f, 0.6f);
        drawTower(batch, atlas, kind, world, logic::kTowerBodyRadius, tint);
    } else if (selected < logic::kTowerKindCount + logic::kAbilityKindCount) {
        const logic::AbilityStats stats =
            logic::abilityStats(static_cast<logic::AbilityKind>(selected - logic::kTowerKindCount));
        gfx::drawCentered(batch, atlas, world, glm::vec2(stats.blast.radius * 2.0f), 0.0f, gfx::cellDisc(),
                          stats.blast.strength < 0.0f ? glm::vec4(0.3f, 0.5f, 1.0f, 0.15f)
                                                      : glm::vec4(1.0f, 0.35f, 0.2f, 0.15f));
    }
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
    if (m_options.mode == LevelMode::Player) {
        // One static view fitted to the level above the tower tray, rebuilt for
        // the new screen size.
        const FeedFit fit = fitFeed(m_level.size, {width, height}, height * kRoundTrayFraction);
        m_camera.setZoomLimits(std::min(fit.zoom, 0.1f), 10.0f);
        m_camera.setZoom(fit.zoom);
        m_camera.setCenter(fit.center);
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
    m_overlay.setCamera(m_feed.active, m_feed.views.size());
}

void LevelScene::fitCameraToLevel(float width, float height) {
    const float zoom = 0.95f * std::min(width / m_level.size.x, height / m_level.size.y);
    m_camera.setZoomLimits(std::min(zoom, 0.1f), 10.0f);
    m_camera.setZoom(zoom);
    m_camera.setCenter(m_level.size * 0.5f);
}

} // namespace horde::scene
