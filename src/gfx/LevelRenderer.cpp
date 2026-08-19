#include "gfx/LevelRenderer.hpp"

#include <cmath>

#include "gfx/SpriteBatch.hpp"
#include "logic/LevelGeometry.hpp"

namespace horde::gfx {

glm::vec4 cellSquare() {
    return atlasCell(0, 0, 4, 4);
}

glm::vec4 cellDisc() {
    return atlasCell(1, 0, 4, 4);
}

glm::vec4 cellTriangle() {
    return atlasCell(2, 0, 4, 4);
}

glm::vec4 cellSolid() {
    return atlasCell(1, 1, 4, 4);
}

glm::vec4 toFloatColor(logic::Rgb color) {
    return {static_cast<float>(color.r) / 255.0f, static_cast<float>(color.g) / 255.0f,
            static_cast<float>(color.b) / 255.0f, 1.0f};
}

void renderWall(const logic::Wall& wall, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color) {
    if (const auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
        drawCentered(batch, atlas, wall.center, rect->halfExtents * 2.0f, wall.rotation, cellSolid(), color);
        return;
    }

    if (const auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
        drawCentered(batch, atlas, wall.center, tri->halfExtents * 2.0f, wall.rotation, cellTriangle(), color);
        return;
    }

    if (const auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
        const float diameter = circle->radius * 2.0f;
        drawCentered(batch, atlas, wall.center, {diameter, diameter}, 0.0f, cellDisc(), color);
        return;
    }

    // A polyline is one thin quad per segment, each centred on its segment's
    // midpoint and rotated to its direction. Points are in local space, so
    // transform each into world space first.
    const auto& line = std::get<logic::PolylineShape>(wall.shape);
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
        const glm::vec2 a = logic::localToWorld(wall, line.points[i]);
        const glm::vec2 b = logic::localToWorld(wall, line.points[i + 1]);
        const glm::vec2 delta = b - a;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (length <= 0.0f) {
            continue;
        }

        drawCentered(batch, atlas, (a + b) * 0.5f, {length, line.thickness}, std::atan2(delta.y, delta.x), cellSolid(),
                     color);
    }
}

void renderLevel(const logic::Level& level, SpriteBatch& batch, SDL_GPUTexture* atlas) {
    // Background: exactly the bounds, drawn first so everything lands on top.
    drawCentered(batch, atlas, level.size * 0.5f, level.size, 0.0f, cellSolid(), toFloatColor(level.backgroundColor));

    for (const logic::Wall& wall : level.walls) {
        renderWall(wall, batch, atlas, toFloatColor(wall.color));
    }

    for (const logic::Marker& marker : level.markers) {
        glm::vec2 center{};
        glm::vec2 size{};
        logic::markerRect(level, marker, center, size);

        // Spawns green, exits red. Fixed, not authored: markers are placeholders
        // for a future integration rather than art.
        const glm::vec4 color = marker.kind == logic::MarkerKind::Spawn ? glm::vec4{0.25f, 0.85f, 0.35f, 1.0f}
                                                                        : glm::vec4{0.9f, 0.3f, 0.25f, 1.0f};
        drawCentered(batch, atlas, center, size, 0.0f, cellSolid(), color);
    }
}

} // namespace horde::gfx
