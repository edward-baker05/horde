#include "gfx/LevelRenderer.hpp"

#include <algorithm>
#include <array>
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

glm::vec4 cellRightTriangle() {
    return atlasCell(3, 0, 4, 4);
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

    // A disc at each bend fills the notch the two quads leave on its outside.
    // The ends stay square. WallColliders collides with the same shape.
    for (std::size_t i = 1; i + 1 < line.points.size(); ++i) {
        drawCentered(batch, atlas, logic::localToWorld(wall, line.points[i]), {line.thickness, line.thickness}, 0.0f,
                     cellDisc(), color);
    }
}

void renderSlope(glm::ivec2 cell, logic::CellShape shape, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color) {
    // The atlas right triangle is the cell's south-west half, its straight sides
    // on the quad's edges. Drawing it over the whole cell, turned a quarter
    // clockwise per step from SW, lands those sides exactly on the cell's
    // edges, so a slope meets its neighbours with no seam; only the diagonal,
    // the exposed face, is antialiased.
    constexpr float kQuarterTurn = 1.5707964f;
    float quarters = 0.0f;
    switch (shape) {
        case logic::CellShape::SlopeNW:
            quarters = 1.0f;
            break;
        case logic::CellShape::SlopeNE:
            quarters = 2.0f;
            break;
        case logic::CellShape::SlopeSE:
            quarters = 3.0f;
            break;
        default:
            break;
    }
    drawCentered(batch, atlas, (glm::vec2(cell) + 0.5f) * logic::kCellSize, glm::vec2(logic::kCellSize),
                 quarters * kQuarterTurn, cellRightTriangle(), color);
}

void renderWallGrid(const logic::GridPieces& pieces, const logic::WallGrid& grid, SpriteBatch& batch,
                    SDL_GPUTexture* atlas) {
    for (const logic::GridRect& rect : pieces.rects) {
        const glm::vec2 size = glm::vec2(rect.size) * logic::kCellSize;
        drawCentered(batch, atlas, glm::vec2(rect.origin) * logic::kCellSize + size * 0.5f, size, 0.0f, cellSolid(),
                     toFloatColor(grid.palette[rect.colorIndex]));
    }
    for (const logic::GridSlope& slope : pieces.slopes) {
        renderSlope(slope.cell, slope.shape, batch, atlas, toFloatColor(grid.palette[slope.colorIndex]));
    }
}

glm::vec4 fieldRegionColor() {
    return {0.35f, 0.6f, 1.0f, 0.18f};
}

void renderFieldRegion(const logic::FieldRegion& region, SpriteBatch& batch, SDL_GPUTexture* atlas, glm::vec4 color) {
    const glm::vec2 size = region.halfExtents * 2.0f;
    drawCentered(batch, atlas, region.center, size, region.rotation, cellSolid(), color);

    // One arrow through the middle along local +x: a shaft and a triangle head.
    // The atlas triangle's apex is local -y, so turning it a further quarter
    // turn points it along +x.
    const glm::vec2 dir = logic::fieldRegionDirection(region);
    const float length = size.x * 0.6f;
    const float head = std::min(length * 0.4f, size.y * 0.6f);
    const float shaft = std::max(length - head, 0.0f);
    const float width = std::max(head * 0.3f, 1.0f);
    glm::vec4 arrow = color;
    arrow.a = std::min(color.a * 3.0f, 1.0f);

    const glm::vec2 tail = region.center - dir * (length * 0.5f);
    if (shaft > 0.0f) {
        drawCentered(batch, atlas, tail + dir * (shaft * 0.5f), {shaft, width}, region.rotation, cellSolid(), arrow);
    }
    drawCentered(batch, atlas, tail + dir * (shaft + head * 0.5f), {head, head}, region.rotation + 1.5707964f,
                 cellTriangle(), arrow);
}

void renderLevel(const logic::Level& level, SpriteBatch& batch, SDL_GPUTexture* atlas, bool showFieldRegions,
                 const logic::GridPieces* gridPieces) {
    // Background: exactly the bounds, drawn first so everything lands on top.
    drawCentered(batch, atlas, level.size * 0.5f, level.size, 0.0f, cellSolid(), toFloatColor(level.backgroundColor));

    // Under the walls: a region is a hint drawn on the floor, not an object.
    if (showFieldRegions) {
        for (const logic::FieldRegion& region : level.fieldRegions) {
            renderFieldRegion(region, batch, atlas, fieldRegionColor());
        }
    }

    if (gridPieces != nullptr) {
        renderWallGrid(*gridPieces, level.wallGrid, batch, atlas);
    } else {
        renderWallGrid(logic::mergeWallGrid(level.wallGrid, true), level.wallGrid, batch, atlas);
    }

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
