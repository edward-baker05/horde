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

// How far two footprints may sink into each other and still count as only
// touching, in world units: rounding in the sines and cosines would otherwise
// make turrets placed edge to edge overlap.
constexpr float kTouchTolerance = 0.05f;

// Separating axis test: two rectangles overlap unless one of their four edge
// directions separates them. Rectangles that only touch don't overlap.
bool boxesOverlap(const Box& a, const Box& b) {
    const glm::vec2 axes[4] = {a.axis, perpendicular(a.axis), b.axis, perpendicular(b.axis)};
    const glm::vec2 d = b.center - a.center;
    for (const glm::vec2 axis : axes) {
        if (std::abs(glm::dot(d, axis)) >= projectedRadius(a, axis) + projectedRadius(b, axis) - kTouchTolerance) {
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
constexpr float kSnapRotateStep = glm::radians(45.0f);

// The grid placement snaps to: the wall grid's cells, so turrets line up with
// painted walls.
constexpr float kSnapGrid = logic::kCellSize;

// Half the width and height of the axis-aligned box around a footprint turned
// by `facing`.
glm::vec2 boundsHalf(glm::vec2 halfSize, float facing) {
    const float c = std::abs(std::cos(facing));
    const float s = std::abs(std::sin(facing));
    return {halfSize.x * c + halfSize.y * s, halfSize.x * s + halfSize.y * c};
}

// Whether a kind goes on open ground, where a line of them can be dragged out.
bool placedOnGround(logic::TurretKind kind) {
    return logic::turretDef(kind).placement != logic::TurretClass::Mount;
}

// A filled cone from `apex` along `dir` (unit length): the atlas triangle,
// whose apex is local -y, turned so the apex sits at `apex` and the base is
// `length` further along.
void drawCone(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec2 apex, float facing, float length, float arc,
              glm::vec4 color) {
    // An arc of a full turn is an aura: a disc around the apex.
    if (arc >= glm::radians(359.0f)) {
        gfx::drawCentered(batch, atlas, apex, glm::vec2(length * 2.0f), 0.0f, gfx::cellDisc(), color);
        return;
    }
    const glm::vec2 dir{std::cos(facing), std::sin(facing)};
    const float width = 2.0f * length * std::tan(std::min(arc, glm::radians(170.0f)) * 0.5f);
    gfx::drawCentered(batch, atlas, apex + dir * (length * 0.5f), {width, length}, facing - 1.5707964f,
                      gfx::cellTriangle(), color);
}

// What a turret reaches: its cone, beam, pulse or blast, faintly.
void drawReach(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Turret& turret, glm::vec4 color) {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    const glm::vec2 dir{std::cos(turret.facing), std::sin(turret.facing)};
    switch (def.delivery) {
        case logic::TurretDelivery::Cone:
            drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc, color);
            break;
        case logic::TurretDelivery::Sweep:
            // The arc it sweeps over, and the beam itself at the middle of it.
            drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc, color * glm::vec4(1, 1, 1, 0.4f));
            gfx::drawCentered(batch, atlas, turret.center + dir * (def.range * 0.5f), {def.range, def.blastRadius},
                              turret.facing, gfx::cellSolid(), color);
            break;
        case logic::TurretDelivery::Pulse:
        case logic::TurretDelivery::Shell:
        case logic::TurretDelivery::Gun:
            gfx::drawCentered(batch, atlas, turret.center, glm::vec2(def.range * 2.0f), 0.0f, gfx::cellDisc(), color);
            break;
        case logic::TurretDelivery::Mine:
            gfx::drawCentered(batch, atlas, turret.center, glm::vec2(def.blastRadius * 2.0f), 0.0f, gfx::cellDisc(),
                              color);
            break;
        case logic::TurretDelivery::None:
            break;
    }
}

// textures/weapons.png is a 4x4 grid; sprite n is cell n - 1, left to right
// then top to bottom. See tools/make_weapon_atlas.py.
constexpr int kWeaponSheetColumns = 4;
constexpr int kWeaponSheetRows = 4;

glm::vec4 weaponSpriteCell(std::uint8_t spriteId) {
    const int index = spriteId - 1;
    return gfx::atlasCell(index % kWeaponSheetColumns, index / kWeaponSheetColumns, kWeaponSheetColumns,
                          kWeaponSheetRows);
}

bool hasSprite(const logic::TurretDef& def, SDL_GPUTexture* weapons) {
    return weapons != nullptr && def.spriteId != 0;
}

// A turret's colour at rest: a sprite keeps its own colours, a plain box takes
// its placement class's.
glm::vec4 restColor(const logic::TurretDef& def, SDL_GPUTexture* weapons) {
    return hasSprite(def, weapons) ? glm::vec4(1.0f) : logic::turretClassColor(def.placement);
}

// A placement the player is still deciding on: green where it can go, red
// where it can't. Sprites are only tinted toward red, so they stay readable.
glm::vec4 previewColor(const logic::TurretDef& def, SDL_GPUTexture* weapons, bool ok) {
    if (hasSprite(def, weapons)) {
        return ok ? glm::vec4(1.0f, 1.0f, 1.0f, 0.7f) : glm::vec4(1.0f, 0.35f, 0.35f, 0.7f);
    }
    return ok ? glm::vec4(0.3f, 0.9f, 0.4f, 0.7f) : glm::vec4(0.9f, 0.2f, 0.2f, 0.7f);
}

// The colour of what a placed turret reaches: its weapon category's, or its
// placement class's for the kinds that predate categories.
glm::vec4 reachColor(const logic::TurretDef& def) {
    return logic::turretColor(def);
}

// How long a newly placed turret takes to grow to full size. Only a look:
// turrets go down in prep, with no units about to be pushed aside.
constexpr float kGrowSeconds = 1.0f / 3.0f;

// `color` multiplies a sprite's own colours, or is a box's colour. `scale`
// shrinks it about its centre, for growing in.
void drawTurret(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons, const logic::Turret& turret,
                glm::vec4 color, float scale = 1.0f) {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    const glm::vec2 size = def.halfSize * 2.0f * scale;
    if (hasSprite(def, weapons)) {
        gfx::drawCentered(batch, weapons, turret.center, size, turret.facing, weaponSpriteCell(def.spriteId), color);
    } else {
        gfx::drawCentered(batch, atlas, turret.center, size, turret.facing, gfx::cellSolid(), color);
    }
    if (def.directional) {
        // A small arrowhead along the facing.
        const float head = std::min(def.halfSize.x, def.halfSize.y) * 1.2f * scale;
        gfx::drawCentered(batch, atlas, turret.center, {head, head}, turret.facing + 1.5707964f, gfx::cellTriangle(),
                          glm::vec4(0.1f, 0.1f, 0.1f, color.a));
    }
}

} // namespace

void renderTurretReach(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Turret& turret, glm::vec4 color) {
    drawReach(batch, atlas, turret, color);
}

void TurretPrep::init(const logic::Level& level, const sim::WallColliders& walls) {
    m_level = &level;
    m_walls = &walls;
    m_turrets.clear();
    m_grow.clear();
    m_fixed = 0;
    m_gone.clear();
    m_undo.clear();
    m_armed.reset();
    m_dragging.reset();
    m_lineStart.reset();
    m_stretch.reset();
    m_nextLine = 1;
}

void TurretPrep::update(float dt) {
    m_grow.resize(m_turrets.size(), 1.0f);
    for (float& grow : m_grow) {
        grow = std::min(grow + dt / kGrowSeconds, 1.0f);
    }
}

void TurretPrep::arm(std::optional<logic::TurretKind> kind) {
    if (kind == m_armed) {
        return;
    }
    m_armed = kind;
    m_lineStart.reset();
    if (kind && !logic::turretDef(*kind).directional) {
        m_ghostFacing = 0.0f;
    }
}

int TurretPrep::pendingCost() const {
    int cost = 0;
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        cost += static_cast<int>(logic::turretDef(m_turrets[i].kind).cost);
    }
    return cost;
}

std::vector<logic::Turret> TurretPrep::takePlaced(bool disarm) {
    if (disarm) {
        m_armed.reset();
        m_lineStart.reset();
    }
    if (m_dragging) {
        m_turrets = m_beforeDrag;
        m_dragging.reset();
    }
    m_stretch.reset();
    std::vector<logic::Turret> placed(m_turrets.begin() + static_cast<std::ptrdiff_t>(m_fixed), m_turrets.end());
    m_fixed = m_turrets.size();
    // Undo would otherwise reach back past turrets already paid for.
    m_undo.clear();
    return placed;
}

void TurretPrep::markGone(std::size_t index) {
    if (index >= m_fixed) {
        return; // only fixed turrets are in play
    }
    if (m_gone.size() <= index) {
        m_gone.resize(index + 1, false);
    }
    m_gone[index] = true;
}

const char* TurretPrep::ghostProblem() const {
    if (!m_armed || !m_cursorInWorld) {
        return nullptr;
    }
    return placementProblem(ghost(), std::nullopt);
}

const char* TurretPrep::placementProblem(const logic::Turret& turret, std::optional<std::size_t> ignore) const {
    const logic::TurretDef& def = logic::turretDef(turret.kind);

    if (!ignore && m_turrets.size() >= logic::kMaxTurrets) {
        return "Turret limit reached";
    }
    if (!ignore && m_budget && pendingCost() + static_cast<int>(def.cost) > *m_budget) {
        return "Not enough money";
    }
    return siteProblem(turret, ignore, 0);
}

const char* TurretPrep::replaceProblem(const std::vector<logic::Turret>& members, std::uint32_t line) const {
    std::size_t count = m_turrets.size();
    int cost = pendingCost();
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        if (m_turrets[i].line == line) {
            --count;
            cost -= static_cast<int>(logic::turretDef(m_turrets[i].kind).cost);
        }
    }
    for (const logic::Turret& member : members) {
        ++count;
        cost += static_cast<int>(logic::turretDef(member.kind).cost);
    }
    if (count > logic::kMaxTurrets) {
        return "Turret limit reached";
    }
    if (m_budget && cost > *m_budget) {
        return "Not enough money";
    }
    for (const logic::Turret& member : members) {
        if (const char* problem = siteProblem(member, std::nullopt, line)) {
            return problem;
        }
    }
    return nullptr;
}

const char* TurretPrep::dragProblem() const {
    if (!m_dragging) {
        return nullptr;
    }
    const std::uint32_t line = m_turrets[*m_dragging].line;
    if (line == 0) {
        return placementProblem(m_turrets[*m_dragging], m_dragging);
    }
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        if (m_turrets[i].line == line) {
            if (const char* problem = siteProblem(m_turrets[i], std::nullopt, line)) {
                return problem;
            }
        }
    }
    return nullptr;
}

const char* TurretPrep::siteProblem(const logic::Turret& turret, std::optional<std::size_t> ignore,
                                    std::uint32_t ignoreLine) const {
    const logic::TurretDef& def = logic::turretDef(turret.kind);
    const Box box = turretBox(turret);

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
            const Box regionBox{
                region.center, {std::cos(region.rotation), std::sin(region.rotation)}, region.halfExtents};
            if (boxesOverlap(box, regionBox)) {
                return "On a field region";
            }
        }
    }

    for (std::size_t i = 0; i < m_turrets.size(); ++i) {
        if ((ignore && *ignore == i) || (ignoreLine != 0 && m_turrets[i].line == ignoreLine) ||
            (i < m_gone.size() && m_gone[i])) {
            continue;
        }
        if (boxesOverlap(box, turretBox(m_turrets[i]))) {
            return "Overlaps another turret";
        }
    }
    return nullptr;
}

glm::vec2 TurretPrep::snapCenter(logic::TurretKind kind, glm::vec2 point, float facing) const {
    if (!m_snap) {
        return point;
    }
    // Snap the footprint's low edges, not its centre: footprints aren't all a
    // whole number of cells across, and it's the edges that should meet walls.
    const glm::vec2 half = boundsHalf(logic::turretDef(kind).halfSize, facing);
    const glm::vec2 low = glm::round((point - half) / kSnapGrid) * kSnapGrid;
    return low + half;
}

logic::Turret TurretPrep::ghost() const {
    return logic::makeTurret(*m_armed, snapCenter(*m_armed, m_cursor, m_ghostFacing), m_ghostFacing);
}

namespace {

// A line's facing for `kind` laid along `dir`: directional kinds turn their
// long side along it, so a line of barriers is one long wall; the rest keep
// `facing`.
float lineFacing(logic::TurretKind kind, glm::vec2 dir, float facing) {
    const logic::TurretDef& def = logic::turretDef(kind);
    if (!def.directional) {
        return facing;
    }
    float along = std::atan2(dir.y, dir.x);
    if (def.halfSize.y > def.halfSize.x) {
        along += glm::radians(90.0f);
    }
    return along;
}

// The footprint's long axis: the way a lone turret's line runs.
glm::vec2 longAxis(const logic::Turret& turret) {
    const glm::vec2 halfSize = logic::turretDef(turret.kind).halfSize;
    const float angle = turret.facing + (halfSize.y > halfSize.x ? glm::radians(90.0f) : 0.0f);
    return {std::cos(angle), std::sin(angle)};
}

} // namespace

float TurretPrep::lineSpacing(logic::TurretKind kind, glm::vec2 dir) const {
    const logic::TurretDef& def = logic::turretDef(kind);
    const float facing = lineFacing(kind, dir, 0.0f);
    // How far apart: the footprint's length along the line, so they meet end
    // to end. Along the grid that's rounded up to whole cells so every one
    // stays on it; a diagonal keeps it exact, as cell-sized steps would leave
    // gaps the crowd could slip through.
    const glm::vec2 axisX{std::cos(facing), std::sin(facing)};
    const glm::vec2 axisY{-axisX.y, axisX.x};
    float spacing =
        2.0f * (def.halfSize.x * std::abs(glm::dot(dir, axisX)) + def.halfSize.y * std::abs(glm::dot(dir, axisY)));
    const bool alongGrid = std::abs(dir.x) < 1.0e-3f || std::abs(dir.y) < 1.0e-3f;
    if (m_snap && alongGrid) {
        spacing = std::ceil(spacing / kSnapGrid - 1.0e-3f) * kSnapGrid;
    }
    return std::max(spacing, 1.0f);
}

std::vector<logic::Turret> TurretPrep::layLine(logic::TurretKind kind, glm::vec2 first, glm::vec2 dir,
                                               std::size_t count, float facing) const {
    facing = lineFacing(kind, dir, facing);
    // The first turret stays where it was asked for, re-snapped for the facing.
    const glm::vec2 start = snapCenter(kind, first, facing);
    const float spacing = lineSpacing(kind, dir);
    std::vector<logic::Turret> line;
    for (std::size_t i = 0; i < std::min(count, logic::kMaxTurrets); ++i) {
        line.push_back(logic::makeTurret(kind, start + dir * (spacing * static_cast<float>(i)), facing));
    }
    return line;
}

namespace {

// The way from `from` to `to` and how far, to lay a line along: with snapping,
// the nearest of eight directions (along the grid, or diagonally across it)
// and the distance along it. Zero length when they meet.
glm::vec2 lineDirection(glm::vec2 from, glm::vec2 to, bool snap, float& length) {
    glm::vec2 span = to - from;
    if (snap && glm::length(span) > 0.0f) {
        const float step = glm::radians(45.0f);
        const float angle = std::round(std::atan2(span.y, span.x) / step) * step;
        const glm::vec2 dir{std::cos(angle), std::sin(angle)};
        span = dir * glm::dot(span, dir);
    }
    length = glm::length(span);
    return length < 1.0e-3f ? glm::vec2(1.0f, 0.0f) : span / length;
}

} // namespace

std::vector<logic::Turret> TurretPrep::lineTurrets() const {
    const logic::TurretKind kind = *m_armed;
    float length = 0.0f;
    const glm::vec2 dir = lineDirection(*m_lineStart, m_cursor, m_snap, length);
    if (length < 1.0e-3f) {
        return {logic::makeTurret(kind, *m_lineStart, m_ghostFacing)};
    }
    const auto count = static_cast<std::size_t>(length / lineSpacing(kind, dir)) + 1;
    return layLine(kind, *m_lineStart, dir, count, m_ghostFacing);
}

std::vector<logic::Turret> TurretPrep::stretchTurrets() const {
    const Stretch& stretch = *m_stretch;
    float length = 0.0f;
    const glm::vec2 dir = lineDirection(stretch.anchor, m_cursor + stretch.offset, m_snap, length);
    std::vector<logic::Turret> line;
    if (length < 1.0e-3f) {
        // Shrunk to its anchor alone, facing as before.
        line.push_back(logic::makeTurret(stretch.kind, stretch.anchor, stretch.facing));
    } else {
        const auto count = static_cast<std::size_t>(length / lineSpacing(stretch.kind, dir) + 0.5f) + 1;
        line = layLine(stretch.kind, stretch.anchor, dir, count, stretch.facing);
    }
    for (logic::Turret& turret : line) {
        turret.line = stretch.line;
    }
    return line;
}

std::vector<std::uint32_t> TurretPrep::pendingLines() const {
    std::vector<std::uint32_t> lines;
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        const std::uint32_t line = m_turrets[i].line;
        if (line != 0 && std::find(lines.begin(), lines.end(), line) == lines.end()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::optional<TurretPrep::LineEnds> TurretPrep::lineEnds(std::uint32_t line) const {
    if (line == 0) {
        return std::nullopt;
    }
    std::optional<LineEnds> ends;
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        if (m_turrets[i].line != line) {
            continue;
        }
        if (!ends) {
            ends = LineEnds{i, i, 0};
        }
        ends->last = i;
        ++ends->count;
    }
    if (!ends) {
        return std::nullopt;
    }
    const logic::Turret& first = m_turrets[ends->first];
    const logic::Turret& last = m_turrets[ends->last];
    const glm::vec2 span = last.center - first.center;
    ends->dir = glm::length(span) > 1.0e-3f ? span / glm::length(span) : longAxis(first);
    ends->startTip = first.center - ends->dir * projectedRadius(turretBox(first), ends->dir);
    ends->endTip = last.center + ends->dir * projectedRadius(turretBox(last), ends->dir);
    return ends;
}

void TurretPrep::replaceLine(std::uint32_t line, std::vector<logic::Turret> members) {
    const std::optional<LineEnds> ends = lineEnds(line);
    if (!ends) {
        return;
    }
    m_undo.push(m_turrets);
    // Its turrets sit together, so the new ones take the old ones' slots.
    std::size_t at = ends->first;
    for (std::size_t i = m_turrets.size(); i-- > m_fixed;) {
        if (m_turrets[i].line == line) {
            m_turrets.erase(m_turrets.begin() + static_cast<std::ptrdiff_t>(i));
            m_grow.erase(m_grow.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    for (logic::Turret& member : members) {
        member.line = line;
    }
    m_turrets.insert(m_turrets.begin() + static_cast<std::ptrdiff_t>(at), members.begin(), members.end());
    m_grow.insert(m_grow.begin() + static_cast<std::ptrdiff_t>(at), members.size(), 1.0f);
}

std::optional<std::size_t> TurretPrep::turretAt(glm::vec2 point) const {
    for (std::size_t i = m_turrets.size(); i-- > 0;) {
        if (i < m_gone.size() && m_gone[i]) {
            continue;
        }
        if (pointInBox(turretBox(m_turrets[i]), point)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> TurretPrep::pendingAt(glm::vec2 point) const {
    const std::optional<std::size_t> hit = turretAt(point);
    if (hit && *hit >= m_fixed) {
        return hit;
    }
    return std::nullopt;
}

void TurretPrep::remove(std::size_t index) {
    m_undo.push(m_turrets);
    // A line goes as one.
    const std::uint32_t line = m_turrets[index].line;
    for (std::size_t i = m_turrets.size(); i-- > m_fixed;) {
        if (i == index || (line != 0 && m_turrets[i].line == line)) {
            m_turrets.erase(m_turrets.begin() + static_cast<std::ptrdiff_t>(i));
            m_grow.erase(m_grow.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void TurretPrep::rotate(float radians) {
    // While snapping, land on a whole number of steps, so a turret turned
    // with snapping off comes back into line.
    const float step = std::abs(radians);
    auto turned = [&](float facing) {
        return m_snap ? std::round((facing + radians) / step) * step : facing + radians;
    };
    if (m_armed) {
        if (logic::turretDef(*m_armed).directional) {
            m_ghostFacing = turned(m_ghostFacing);
        }
        return;
    }
    if (m_stretch) {
        return; // the stretch already sets its direction
    }
    if (m_dragging) {
        // Part of the drag: undone with it. A line being moved keeps its turn.
        logic::Turret& turret = m_turrets[*m_dragging];
        const std::optional<LineEnds> ends = lineEnds(turret.line);
        if (logic::turretDef(turret.kind).directional && (!ends || ends->count < 2)) {
            turret.facing = turned(turret.facing);
            turret.center = snapCenter(turret.kind, m_cursor + m_dragOffset, turret.facing);
        }
        return;
    }
    const std::optional<std::size_t> hovered = pendingAt(m_cursor);
    if (!hovered) {
        return;
    }
    const logic::Turret& target = m_turrets[*hovered];
    if (const std::optional<LineEnds> ends = lineEnds(target.line); ends && ends->count >= 2) {
        // Turned about its middle, keeping its length, and laid afresh along
        // the new direction.
        const logic::Turret& first = m_turrets[ends->first];
        const glm::vec2 firstCenter = first.center;
        const glm::vec2 lastCenter = m_turrets[ends->last].center;
        const float length = glm::distance(firstCenter, lastCenter);
        const float angle = turned(std::atan2(ends->dir.y, ends->dir.x));
        const glm::vec2 dir{std::cos(angle), std::sin(angle)};
        const auto count =
            static_cast<std::size_t>(length / lineSpacing(target.kind, ends->dir) + 0.5f) + 1;
        const float spacing = lineSpacing(target.kind, dir);
        const glm::vec2 start =
            (firstCenter + lastCenter) * 0.5f - dir * (spacing * static_cast<float>(count - 1) * 0.5f);
        const std::vector<logic::Turret> members = layLine(target.kind, start, dir, count, first.facing);
        const std::uint32_t line = target.line;
        if (replaceProblem(members, line) == nullptr) {
            replaceLine(line, members);
        }
        return;
    }
    logic::Turret rotated = target;
    if (!logic::turretDef(rotated.kind).directional) {
        return;
    }
    rotated.facing = turned(rotated.facing);
    rotated.center = snapCenter(rotated.kind, rotated.center, rotated.facing);
    if (placementProblem(rotated, hovered) == nullptr) {
        m_undo.push(m_turrets);
        m_turrets[*hovered] = rotated;
    }
}

namespace {

// A line handle's radius on screen, in pixels.
constexpr float kHandlePixels = 6.0f;

} // namespace

bool TurretPrep::handleEvent(const SDL_Event& event, const gfx::Camera2D& camera) {
    m_handleRadius = kHandlePixels / camera.zoom();
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            m_cursor = camera.screenToWorld({event.motion.x, event.motion.y});
            m_cursorInWorld = true;
            if (m_dragging) {
                // The grabbed turret follows the cursor; the rest of its line
                // keeps its place relative to it.
                const logic::Turret& grabbed = m_turrets[*m_dragging];
                const glm::vec2 delta = snapCenter(grabbed.kind, m_cursor + m_dragOffset, grabbed.facing) -
                                        m_beforeDrag[*m_dragging].center;
                const std::uint32_t line = grabbed.line;
                for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
                    if (i == *m_dragging || (line != 0 && m_turrets[i].line == line)) {
                        m_turrets[i].center = m_beforeDrag[i].center + delta;
                    }
                }
                return true;
            }
            return m_lineStart.has_value() || m_stretch.has_value();

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            m_cursor = camera.screenToWorld({event.button.x, event.button.y});
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (m_armed) {
                    const logic::Turret turret = ghost();
                    if (placedOnGround(*m_armed)) {
                        // Placed on release: a drag lays a line of them.
                        m_lineStart = turret.center;
                    } else if (placementProblem(turret, std::nullopt) == nullptr) {
                        m_undo.push(m_turrets);
                        m_turrets.push_back(turret);
                        m_grow.push_back(0.0f);
                    }
                    return true; // not a pan, even where it can't go
                }
                // A line's end handle: stretch it about the other end.
                for (const std::uint32_t line : pendingLines()) {
                    const std::optional<LineEnds> ends = lineEnds(line);
                    const bool atStart = glm::distance(m_cursor, ends->startTip) <= m_handleRadius * 1.5f;
                    const bool atEnd = glm::distance(m_cursor, ends->endTip) <= m_handleRadius * 1.5f;
                    if (!atStart && !atEnd) {
                        continue;
                    }
                    const logic::Turret& dragged = m_turrets[atEnd ? ends->last : ends->first];
                    const logic::Turret& anchor = m_turrets[atEnd ? ends->first : ends->last];
                    m_stretch = Stretch{line, anchor.kind, anchor.center, anchor.facing, dragged.center - m_cursor};
                    return true;
                }
                if (const std::optional<std::size_t> hit = pendingAt(m_cursor)) {
                    m_beforeDrag = m_turrets;
                    m_dragging = hit;
                    m_dragOffset = m_turrets[*hit].center - m_cursor;
                    return true;
                }
                return false;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) {
                if (m_lineStart) {
                    m_lineStart.reset(); // cancels the line, keeps the kind armed
                    return true;
                }
                if (m_stretch) {
                    m_stretch.reset(); // cancels the stretch, leaving the line as it was
                    return true;
                }
                if (m_armed) {
                    m_armed.reset();
                    return true;
                }
                if (m_dragging) {
                    return true; // not mid-drag: the indices would shift under it
                }
                if (const std::optional<std::size_t> hit = pendingAt(m_cursor)) {
                    remove(*hit);
                    return true;
                }
            }
            return false;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT && m_lineStart) {
                m_cursor = camera.screenToWorld({event.button.x, event.button.y});
                // Every one that fits goes down, as one undo step; ones in the
                // way (a wall, a turret, the level's edge) are skipped. They
                // stay one line, unless that's a lone turret with no direction.
                const std::vector<logic::Turret> before = m_turrets;
                const std::uint32_t line = m_nextLine++;
                for (logic::Turret turret : lineTurrets()) {
                    if (placementProblem(turret, std::nullopt) == nullptr) {
                        turret.line = line;
                        m_turrets.push_back(turret);
                        m_grow.push_back(0.0f);
                    }
                }
                const std::size_t placed = m_turrets.size() - before.size();
                if (placed == 1 && !logic::turretDef(m_turrets.back().kind).directional) {
                    m_turrets.back().line = 0;
                }
                if (placed != 0) {
                    m_undo.push(before);
                }
                m_lineStart.reset();
                return true;
            }
            if (event.button.button == SDL_BUTTON_LEFT && m_stretch) {
                m_cursor = camera.screenToWorld({event.button.x, event.button.y});
                // Somewhere it can't go snaps back; a real change is one undo step.
                const std::vector<logic::Turret> members = stretchTurrets();
                const std::uint32_t line = m_stretch->line;
                bool changed = members.size() != lineEnds(line)->count;
                for (std::size_t i = 0, j = 0; !changed && i < m_turrets.size(); ++i) {
                    if (m_turrets[i].line == line) {
                        changed = m_turrets[i].center != members[j].center || m_turrets[i].facing != members[j].facing;
                        ++j;
                    }
                }
                if (changed && replaceProblem(members, line) == nullptr) {
                    replaceLine(line, members);
                }
                m_stretch.reset();
                return true;
            }
            if (event.button.button == SDL_BUTTON_LEFT && m_dragging) {
                // Somewhere it can't go snaps back; a real move is one undo step.
                const logic::Turret& moved = m_turrets[*m_dragging];
                const logic::Turret& before = m_beforeDrag[*m_dragging];
                const bool changed = moved.center != before.center || moved.facing != before.facing;
                if (dragProblem() != nullptr) {
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
            if ((m_dragging || m_lineStart || m_stretch) && event.key.key != SDLK_Q && event.key.key != SDLK_E) {
                return false; // undoing mid-drag would leave the drag pointing at the wrong turret
            }
            switch (event.key.key) {
                case SDLK_G:
                    m_snap = !m_snap;
                    return true;
                case SDLK_Q:
                case SDLK_E: {
                    const float step =
                        m_snap ? (shift ? kRotateStep : kSnapRotateStep) : (shift ? kFineRotateStep : kRotateStep);
                    rotate(event.key.key == SDLK_Q ? -step : step);
                    return true;
                }
                case SDLK_DELETE:
                case SDLK_BACKSPACE:
                    if (const std::optional<std::size_t> hovered = pendingAt(m_cursor)) {
                        remove(*hovered);
                        return true;
                    }
                    return false;
                case SDLK_ESCAPE:
                    if (m_lineStart) {
                        m_lineStart.reset();
                        return true;
                    }
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
                        // Whatever came back is there already: no growing in.
                        m_grow.assign(m_turrets.size(), 1.0f);
                        return true;
                    }
                    return false;
                case SDLK_Y:
                    if (ctrl) {
                        m_undo.redo(m_turrets);
                        m_grow.assign(m_turrets.size(), 1.0f);
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

void TurretPrep::render(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons) const {
    // A drag that would snap back shows red: the turret, or its whole line.
    const bool dragBlocked = dragProblem() != nullptr;
    const std::uint32_t dragLine = m_dragging ? m_turrets[*m_dragging].line : 0;
    for (std::size_t i = m_fixed; i < m_turrets.size(); ++i) {
        const logic::Turret& turret = m_turrets[i];
        if (m_stretch && turret.line == m_stretch->line) {
            continue; // drawn as the stretch's preview below
        }
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        glm::vec4 color = restColor(def, weapons);
        const bool dragged = m_dragging && (*m_dragging == i || (dragLine != 0 && turret.line == dragLine));
        if (dragged && dragBlocked) {
            color = previewColor(def, weapons, false);
        }
        // Growing in: eased out, so it pops up fast and settles.
        const float t = i < m_grow.size() ? m_grow[i] : 1.0f;
        const float grown = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        drawReach(batch, atlas, turret, glm::vec4(glm::vec3(reachColor(def)), 0.12f * grown));
        drawTurret(batch, atlas, weapons, turret, color, grown);
    }

    // Hidden while the cursor is over a panel: ImGui would get the click.
    // A line being dragged shows every turret it would place, even over a panel.
    const bool showGhost = m_cursorInWorld && !ImGui::GetIO().WantCaptureMouse;
    if (m_armed && (m_lineStart || showGhost)) {
        const std::vector<logic::Turret> previews = m_lineStart ? lineTurrets() : std::vector{ghost()};
        for (const logic::Turret& preview : previews) {
            const logic::TurretDef& def = logic::turretDef(preview.kind);
            const bool ok = placementProblem(preview, std::nullopt) == nullptr;
            const glm::vec4 color = previewColor(def, weapons, ok);
            // The reach shows where it can go even when the sprite keeps its colours.
            const glm::vec4 reach = ok ? glm::vec4(0.3f, 0.9f, 0.4f, 0.15f) : glm::vec4(0.9f, 0.2f, 0.2f, 0.15f);
            drawReach(batch, atlas, preview, reach);
            drawTurret(batch, atlas, weapons, preview, color);
        }
    }

    // A line being stretched: green where it can go, all red if it can't.
    if (m_stretch) {
        const std::vector<logic::Turret> members = stretchTurrets();
        const bool ok = replaceProblem(members, m_stretch->line) == nullptr;
        for (const logic::Turret& member : members) {
            const logic::TurretDef& def = logic::turretDef(member.kind);
            drawReach(batch, atlas, member, glm::vec4(glm::vec3(reachColor(def)), 0.12f));
            drawTurret(batch, atlas, weapons, member, previewColor(def, weapons, ok));
        }
    }

    // Each pending line's end handles, while nothing is armed to place.
    if (!m_armed) {
        for (const std::uint32_t line : pendingLines()) {
            if (m_stretch && m_stretch->line == line) {
                continue;
            }
            const std::optional<LineEnds> ends = lineEnds(line);
            for (const glm::vec2 tip : {ends->startTip, ends->endTip}) {
                gfx::drawCentered(batch, atlas, tip, glm::vec2(m_handleRadius * 2.6f), 0.0f, gfx::cellDisc(),
                                  glm::vec4(0.1f, 0.1f, 0.1f, 0.8f));
                gfx::drawCentered(batch, atlas, tip, glm::vec2(m_handleRadius * 2.0f), 0.0f, gfx::cellDisc(),
                                  glm::vec4(1.0f, 1.0f, 1.0f, 0.9f));
            }
        }
    }
}

void renderWaveTurrets(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons,
                       const sim::TurretEffects& effects) {
    const std::vector<logic::Turret>& turrets = effects.turrets();
    for (std::size_t i = 0; i < turrets.size(); ++i) {
        if (effects.spent(i)) {
            continue;
        }
        logic::Turret turret = turrets[i];
        const logic::TurretDef& def = logic::turretDef(turret.kind);
        glm::vec4 color = restColor(def, weapons);
        if (def.maxHp > 0.0f) {
            // Down to a third of its colour as it nears breaking.
            const float left = std::clamp(effects.hp(i) / def.maxHp, 0.0f, 1.0f);
            color = glm::vec4(glm::vec3(color) * (0.33f + 0.67f * left), 1.0f);
        }
        // What is firing all the time shows; pulses and shells show as explosions.
        if (def.delivery == logic::TurretDelivery::Sweep) {
            turret.facing = effects.aimOf(i);
            const glm::vec2 dir{std::cos(turret.facing), std::sin(turret.facing)};
            gfx::drawCentered(batch, atlas, turret.center + dir * (def.range * 0.5f), {def.range, def.blastRadius},
                              turret.facing, gfx::cellSolid(), glm::vec4(1.0f, 0.3f, 0.9f, 0.7f));
        } else if (def.delivery == logic::TurretDelivery::Cone) {
            // Weapons in their category's colour; an aura fainter, as it covers so much.
            const bool aura = def.arc >= glm::radians(359.0f);
            const glm::vec4 cone =
                def.category ? glm::vec4(glm::vec3(logic::weaponCategoryColor(*def.category)), aura ? 0.12f : 0.25f)
                             : glm::vec4(1.0f, 0.5f, 0.1f, 0.25f);
            drawCone(batch, atlas, turret.center, turret.facing, def.range, def.arc, cone);
        }
        drawTurret(batch, atlas, weapons, turret, color);
    }
}

} // namespace horde::scene
