#include "scene/TurretPrep.hpp"

#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "gfx/Camera2D.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "sim/TurretEffects.hpp"
#include "sim/WallColliders.hpp"

namespace horde::scene {

namespace {

// A rotated rectangle: centre, its local +x axis (unit length) and half size.
struct Box {
    glm::vec2 center{0.0f};
    glm::vec2 axis{1.0f, 0.0f};
    glm::vec2 half{0.0f};
};

Box turretBox(const logic::Turret& turret) {
    return {turret.center, {std::cos(turret.facing), std::sin(turret.facing)}, logic::turretDef(turret.kind).halfSize};
}

glm::vec2 perpendicular(glm::vec2 v) {
    return {-v.y, v.x};
}

// The box's corner at local (sx, sy) of its half size, each -1 to 1.
glm::vec2 boxPoint(const Box& box, float sx, float sy) {
    return box.center + box.axis * (box.half.x * sx) + perpendicular(box.axis) * (box.half.y * sy);
}

bool pointInBox(const Box& box, glm::vec2 point) {
    const glm::vec2 d = point - box.center;
    return std::abs(glm::dot(d, box.axis)) <= box.half.x &&
           std::abs(glm::dot(d, perpendicular(box.axis))) <= box.half.y;
}

// Half the box's extent along `axis` (unit length).
float projectedRadius(const Box& box, glm::vec2 axis) {
    return box.half.x * std::abs(glm::dot(box.axis, axis)) +
           box.half.y * std::abs(glm::dot(perpendicular(box.axis), axis));
}

// Separating axis test: two rectangles overlap unless one of their four edge
// directions separates them.
bool boxesOverlap(const Box& a, const Box& b) {
    const glm::vec2 axes[4] = {a.axis, perpendicular(a.axis), b.axis, perpendicular(b.axis)};
    const glm::vec2 d = b.center - a.center;
    for (const glm::vec2 axis : axes) {
        if (std::abs(glm::dot(d, axis)) > projectedRadius(a, axis) + projectedRadius(b, axis)) {
            return false;
        }
    }
    return true;
}

// How far in front of a spawn nothing solid may go, so units don't enter
// inside a blocker.
constexpr float kSpawnClearance = 30.0f;

// Spacing of the points a footprint is checked against walls at: under the
// thinnest wall a level is likely to have, so none slips between them.
constexpr float kWallSampleSpacing = 2.0f;

constexpr float kRotateStep = glm::radians(15.0f);
constexpr float kFineRotateStep = glm::radians(1.0f);

glm::vec4 classColor(logic::TurretClass placement) {
    switch (placement) {
        case logic::TurretClass::Mount:
            return {0.95f, 0.65f, 0.2f, 1.0f};
        case logic::TurretClass::Blocker:
            return {0.6f, 0.55f, 0.5f, 1.0f};
        case logic::TurretClass::FloorTrap:
            return {0.85f, 0.25f, 0.25f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

// A filled cone from `apex` along `dir` (unit length): the atlas triangle,
// whose apex is local -y, turned so the apex sits at `apex` and the base is
// `length` further along.
void drawCone(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec2 apex, float facing, float length, float arc,
              glm::vec4 color) {
    const glm::vec2 dir{std::cos(facing), std::sin(facing)};
    const float width = 2.0f * length * std::tan(std::min(arc, glm::radians(170.0f)) * 0.5f);
    gfx::drawCentered(batch, atlas, apex + dir * (length * 0.5f), {width, length}, facing - 1.5707964f,
                      gfx::cellTriangle(), color);
}

// What a turret reaches: its cone, beam, pulse or blast, faintly.
void drawReach(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Turret& turret, glm::vec4 color) {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    const glm::vec2 dir{std::cos(turret.facing), std::sin(turret.facing)};
    switch (turret.kind) {
        case logic::TurretKind::Flamer:
            drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc, color);
            break;
        case logic::TurretKind::Beam:
            // The arc it sweeps over, and the beam itself at the middle of it.
            drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc, color * glm::vec4(1, 1, 1, 0.4f));
            gfx::drawCentered(batch, atlas, turret.center + dir * (def.range * 0.5f), {def.range, def.blastRadius},
                              turret.facing, gfx::cellSolid(), color);
            break;
        case logic::TurretKind::Zapper:
        case logic::TurretKind::Mortar:
        case logic::TurretKind::Gunner:
            gfx::drawCentered(batch, atlas, turret.center, glm::vec2(def.range * 2.0f), 0.0f, gfx::cellDisc(), color);
            break;
        case logic::TurretKind::Mine:
            gfx::drawCentered(batch, atlas, turret.center, glm::vec2(def.blastRadius * 2.0f), 0.0f, gfx::cellDisc(),
                              color);
            break;
        case logic::TurretKind::SpikeBlock:
        case logic::TurretKind::Barricade:
            break;
    }
}

void drawTurret(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Turret& turret, glm::vec4 color) {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    gfx::drawCentered(batch, atlas, turret.center, def.halfSize * 2.0f, turret.facing, gfx::cellSolid(), color);
    if (def.directional) {
        // A small arrowhead along the facing.
        const float head = std::min(def.halfSize.x, def.halfSize.y) * 1.2f;
        gfx::drawCentered(batch, atlas, turret.center, {head, head}, turret.facing + 1.5707964f, gfx::cellTriangle(),
                          glm::vec4(0.1f, 0.1f, 0.1f, color.a));
    }
}

} // namespace

void TurretPrep::init(const logic::Level& level, const sim::WallColliders& walls) {
    m_level = &level;
    m_walls = &walls;
    m_turrets.clear();
    m_undo.clear();
    m_armed.reset();
    m_dragging.reset();
}

const char* TurretPrep::placementProblem(const logic::Turret& turret, std::optional<std::size_t> ignore) const {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    const Box box = turretBox(turret);

    if (!ignore && m_turrets.size() >= logic::kMaxTurrets) {
        return "Turret limit reached";
    }

    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sy : {-1.0f, 1.0f}) {
            const glm::vec2 corner = boxPoint(box, sx, sy);
            if (corner.x < 0.0f || corner.y < 0.0f || corner.x > m_level->size.x || corner.y > m_level->size.y) {
                return "Outside the level";
            }
        }
    }

    // Mounts must be over wall at every point; the rest must be clear of it.
    const int stepsX = std::max(1, static_cast<int>(std::ceil(box.half.x * 2.0f / kWallSampleSpacing)));
    const int stepsY = std::max(1, static_cast<int>(std::ceil(box.half.y * 2.0f / kWallSampleSpacing)));
    const bool mount = def.placement == logic::TurretClass::Mount;
    for (int ix = 0; ix <= stepsX; ++ix) {
        for (int iy = 0; iy <= stepsY; ++iy) {
            const glm::vec2 p = boxPoint(box, -1.0f + 2.0f * static_cast<float>(ix) / static_cast<float>(stepsX),
                                         -1.0f + 2.0f * static_cast<float>(iy) / static_cast<float>(stepsY));
            float distance = 0.0f;
            glm::vec2 normal{0.0f};
            const bool inWall = m_walls->nearest(p, distance, normal) && distance <= 0.0f;
            if (mount && !inWall) {
                return "Mounts go on top of walls";
            }
            if (!mount && inWall) {
                return "Must be clear of walls";
            }
        }
    }

    if (!mount) {
        for (const logic::Marker& marker : m_level->markers) {
            glm::vec2 center;
            glm::vec2 size;
            logic::markerRect(*m_level, marker, center, size);
            const float clearance = marker.kind == logic::MarkerKind::Spawn ? kSpawnClearance : 0.0f;
            if (boxesOverlap(box, {center, {1.0f, 0.0f}, size * 0.5f + clearance})) {
                return marker.kind == logic::MarkerKind::Spawn ? "Too close to a spawn" : "On an exit";
            }
        }
        for (const logic::FieldRegion& region : m_level->fieldRegions) {
            const Box regionBox{region.center, {std::cos(region.rotation), std::sin(region.rotation)},
                                region.halfExtents};
            if (boxesOverlap(box, regionBox)) {
                return "On a field region";
            }
        }
    }

    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        if (ignore && *ignore == i) {
            continue;
        }
        if (boxesOverlap(box, turretBox(m_turrets[i]))) {
            return "Overlaps another turret";
        }
    }
    return nullptr;
}

void TurretPrep::finish() {
    m_armed.reset();
    if (m_dragging) {
        m_turrets = m_beforeDrag;
        m_dragging.reset();
    }
}

std::optional<std::size_t> TurretPrep::turretAt(glm::vec2 point) const {
    for (std::size_t i = m_turrets.size(); i-- > 0;) {
        if (pointInBox(turretBox(m_turrets[i]), point)) {
            return i;
        }
    }
    return std::nullopt;
}

void TurretPrep::remove(std::size_t index) {
    m_undo.push(m_turrets);
    m_turrets.erase(m_turrets.begin() + static_cast<std::ptrdiff_t>(index));
}

void TurretPrep::rotate(float radians) {
    if (m_armed) {
        if (logic::turretDef(*m_armed).directional) {
            m_ghostFacing += radians;
        }
        return;
    }
    if (m_dragging) {
        // Part of the drag: undone with it.
        logic::Turret& turret = m_turrets[*m_dragging];
        if (logic::turretDef(turret.kind).directional) {
            turret.facing += radians;
        }
        return;
    }
    if (const std::optional<std::size_t> hovered = turretAt(m_cursor)) {
        logic::Turret rotated = m_turrets[*hovered];
        if (!logic::turretDef(rotated.kind).directional) {
            return;
        }
        rotated.facing += radians;
        if (placementProblem(rotated, hovered) == nullptr) {
            m_undo.push(m_turrets);
            m_turrets[*hovered] = rotated;
        }
    }
}

bool TurretPrep::handleEvent(const SDL_Event& event, const gfx::Camera2D& camera) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            m_cursor = camera.screenToWorld({event.motion.x, event.motion.y});
            m_cursorInWorld = true;
            if (m_dragging) {
                m_turrets[*m_dragging].center = m_cursor + m_dragOffset;
                return true;
            }
            return false;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            m_cursor = camera.screenToWorld({event.button.x, event.button.y});
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (m_armed) {
                    const logic::Turret turret = logic::makeTurret(*m_armed, m_cursor, m_ghostFacing);
                    if (placementProblem(turret, std::nullopt) == nullptr) {
                        m_undo.push(m_turrets);
                        m_turrets.push_back(turret);
                    }
                    return true; // not a pan, even where it can't go
                }
                if (const std::optional<std::size_t> hit = turretAt(m_cursor)) {
                    m_beforeDrag = m_turrets;
                    m_dragging = hit;
                    m_dragOffset = m_turrets[*hit].center - m_cursor;
                    return true;
                }
                return false;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
                if (m_armed) {
                    m_armed.reset();
                    return true;
                }
                if (const std::optional<std::size_t> hit = turretAt(m_cursor)) {
                    remove(*hit);
                    return true;
                }
            }
            return false;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT && m_dragging) {
                // Somewhere it can't go snaps back; a real move is one undo step.
                const logic::Turret& moved = m_turrets[*m_dragging];
                const logic::Turret& before = m_beforeDrag[*m_dragging];
                const bool changed = moved.center != before.center || moved.facing != before.facing;
                if (placementProblem(moved, m_dragging) != nullptr) {
                    m_turrets = m_beforeDrag;
                } else if (changed) {
                    m_undo.push(m_beforeDrag);
                }
                m_dragging.reset();
                return true;
            }
            return false;

        case SDL_EVENT_KEY_DOWN: {
            const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            if (m_dragging && event.key.key != SDLK_Q && event.key.key != SDLK_E) {
                return false; // undoing mid-drag would leave the drag pointing at the wrong turret
            }
            switch (event.key.key) {
                case SDLK_Q:
                case SDLK_E: {
                    const float step = shift ? kFineRotateStep : kRotateStep;
                    rotate(event.key.key == SDLK_Q ? -step : step);
                    return true;
                }
                case SDLK_DELETE:
                case SDLK_BACKSPACE:
                    if (const std::optional<std::size_t> hovered = turretAt(m_cursor)) {
                        remove(*hovered);
                        return true;
                    }
                    return false;
                case SDLK_ESCAPE:
                    if (m_armed) {
                        m_armed.reset();
                        return true;
                    }
                    return false;
                case SDLK_Z:
                    if (ctrl) {
                        if (shift) {
                            m_undo.redo(m_turrets);
                        } else {
                            m_undo.undo(m_turrets);
                        }
                        return true;
                    }
                    return false;
                case SDLK_Y:
                    if (ctrl) {
                        m_undo.redo(m_turrets);
                        return true;
                    }
                    return false;
                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

void TurretPrep::render(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas) const {
    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        const logic::Turret& turret = m_turrets[i];
        glm::vec4 color = classColor(logic::turretDef(turret.kind).placement);
        // A drag that would snap back shows red.
        if (m_dragging && *m_dragging == i && placementProblem(turret, m_dragging) != nullptr) {
            color = {0.9f, 0.2f, 0.2f, 0.8f};
        }
        drawReach(batch, atlas, turret, glm::vec4(glm::vec3(color), 0.12f));
        drawTurret(batch, atlas, turret, color);
    }

    // Hidden while the cursor is over a panel: ImGui would get the click.
    if (m_armed && m_cursorInWorld && !ImGui::GetIO().WantCaptureMouse) {
        const logic::Turret ghost = logic::makeTurret(*m_armed, m_cursor, m_ghostFacing);
        const bool ok = placementProblem(ghost, std::nullopt) == nullptr;
        const glm::vec4 color = ok ? glm::vec4(0.3f, 0.9f, 0.4f, 0.7f) : glm::vec4(0.9f, 0.2f, 0.2f, 0.7f);
        drawReach(batch, atlas, ghost, glm::vec4(glm::vec3(color), 0.15f));
        drawTurret(batch, atlas, ghost, color);
    }
}

void renderWaveTurrets(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const sim::TurretEffects& effects) {
    const std::vector<logic::Turret>& turrets = effects.turrets();
    for (std::size_t i = 0; i < turrets.size(); ++i) {
        if (effects.spent(i)) {
            continue;
        }
        logic::Turret turret = turrets[i];
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        glm::vec4 color = classColor(def.placement);
        if (def.maxHp > 0.0f) {
            // Down to a third of its colour as it nears breaking.
            const float left = std::clamp(effects.hp(i) / def.maxHp, 0.0f, 1.0f);
            color = glm::vec4(glm::vec3(color) * (0.33f + 0.67f * left), 1.0f);
        }
        // What is firing all the time shows; pulses and shells show as explosions.
        if (turret.kind == logic::TurretKind::Flamer || turret.kind == logic::TurretKind::Beam) {
            logic::Turret aimed = turret;
            aimed.facing = effects.aimOf(i);
            if (turret.kind == logic::TurretKind::Beam) {
                const glm::vec2 dir{std::cos(aimed.facing), std::sin(aimed.facing)};
                gfx::drawCentered(batch, atlas, turret.center + dir * (def.range * 0.5f),
                                  {def.range, def.blastRadius}, aimed.facing, gfx::cellSolid(),
                                  glm::vec4(1.0f, 0.3f, 0.9f, 0.7f));
            } else {
                drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc,
                         glm::vec4(1.0f, 0.5f, 0.1f, 0.25f));
            }
            turret.facing = aimed.facing;
        }
        drawTurret(batch, atlas, turret, color);
    }
}

void TurretPrep::ui() {
    ImGui::SeparatorText("Turrets");
    ImGui::Text("%zu / %zu placed", m_turrets.size(), logic::kMaxTurrets);

    for (std::size_t i = 0; i < logic::kTurretKindCount; ++i) {
        const auto kind = static_cast<logic::TurretKind>(i);
        const logic::TurretDef& def = logic::turretDef(kind);
        if (i % 4 != 0) {
            ImGui::SameLine();
        }
        const bool armed = m_armed && *m_armed == kind;
        if (armed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(def.name, ImVec2(96.0f, 0.0f))) {
            if (armed) {
                m_armed.reset();
            } else {
                m_armed = kind;
                if (!def.directional) {
                    m_ghostFacing = 0.0f;
                }
            }
        }
        if (armed) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            const char* where = def.placement == logic::TurretClass::Mount     ? "on top of a wall"
                                : def.placement == logic::TurretClass::Blocker ? "on open ground; solid"
                                                                               : "on open ground";
            ImGui::SetTooltip("%s: placed %s", def.name, where);
        }
    }

    if (m_armed) {
        const logic::Turret ghost = logic::makeTurret(*m_armed, m_cursor, m_ghostFacing);
        if (const char* problem = m_cursorInWorld ? placementProblem(ghost, std::nullopt) : nullptr) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", problem);
        } else {
            ImGui::TextUnformatted("");
        }
    }
    ImGui::TextDisabled("Left: place / drag   Right: cancel / remove");
    ImGui::TextDisabled("Q/E: rotate   Ctrl+Z/Y: undo/redo");
}

} // namespace horde::scene
