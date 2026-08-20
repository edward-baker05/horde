#include <algorithm>
#include <cmath>

#include "Logic/VectorField.hpp"

VectorField::VectorField(const BoundingBox& bounds, float cellSize) : bounds(bounds), cellSize(cellSize) {
    resize(bounds, cellSize);
}

void VectorField::resize(const BoundingBox& newBounds, float newCellSize) {
    bounds = newBounds;
    cellSize = std::max(1.0f, newCellSize);

    const float width = std::max(1.0f, bounds.max.x - bounds.min.x);
    const float height = std::max(1.0f, bounds.max.y - bounds.min.y);

    cols = std::max(1, static_cast<int>(std::ceil(width / cellSize)));
    rows = std::max(1, static_cast<int>(std::ceil(height / cellSize)));

    field.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), glm::vec2(0.0f, 0.0f));
}

void VectorField::clear() {
    std::fill(field.begin(), field.end(), glm::vec2(0.0f, 0.0f));
}

glm::vec2 VectorField::sample(glm::vec2 worldPos) const {
    if (field.empty() || cols <= 0 || rows <= 0) {
        return glm::vec2(0.0f, 0.0f);
    }

    const float invCellSize = 1.0f / cellSize;
    const float u = (worldPos.x - bounds.min.x) * invCellSize - 0.5f;
    const float v = (worldPos.y - bounds.min.y) * invCellSize - 0.5f;

    const int c0 = std::clamp(static_cast<int>(std::floor(u)), 0, cols - 1);
    const int r0 = std::clamp(static_cast<int>(std::floor(v)), 0, rows - 1);
    const int c1 = std::min(c0 + 1, cols - 1);
    const int r1 = std::min(r0 + 1, rows - 1);

    const float tx = std::clamp(u - static_cast<float>(c0), 0.0f, 1.0f);
    const float ty = std::clamp(v - static_cast<float>(r0), 0.0f, 1.0f);

    const glm::vec2 v00 = field[static_cast<size_t>(r0 * cols + c0)];
    const glm::vec2 v10 = field[static_cast<size_t>(r0 * cols + c1)];
    const glm::vec2 v01 = field[static_cast<size_t>(r1 * cols + c0)];
    const glm::vec2 v11 = field[static_cast<size_t>(r1 * cols + c1)];

    const glm::vec2 v0 = glm::mix(v00, v10, tx);
    const glm::vec2 v1 = glm::mix(v01, v11, tx);

    return glm::mix(v0, v1, ty);
}

glm::vec2 VectorField::getVector(int col, int row) const {
    if (col < 0 || col >= cols || row < 0 || row >= rows) {
        return glm::vec2(0.0f, 0.0f);
    }
    return field[static_cast<size_t>(row * cols + col)];
}

void VectorField::setVector(int col, int row, glm::vec2 vec) {
    if (col >= 0 && col < cols && row >= 0 && row < rows) {
        field[static_cast<size_t>(row * cols + col)] = vec;
    }
}

glm::vec2 VectorField::getCellCenter(int col, int row) const {
    return glm::vec2(bounds.min.x + (static_cast<float>(col) + 0.5f) * cellSize,
                     bounds.min.y + (static_cast<float>(row) + 0.5f) * cellSize);
}

void VectorField::generateCircular(glm::vec2 center, bool clockwise) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const glm::vec2 pos = getCellCenter(c, r);
            const glm::vec2 delta = pos - center;
            const float dist = glm::length(delta);

            if (dist > 1e-4f) {
                glm::vec2 tangent = clockwise ? glm::vec2(-delta.y, delta.x) : glm::vec2(delta.y, -delta.x);
                tangent /= dist;
                setVector(c, r, tangent);
            } else {
                setVector(c, r, glm::vec2(0.0f, 0.0f));
            }
        }
    }
}

void VectorField::generateVortex(glm::vec2 center, float inwardPull, bool clockwise) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const glm::vec2 pos = getCellCenter(c, r);
            const glm::vec2 delta = pos - center;
            const float dist = glm::length(delta);

            if (dist > 1e-4f) {
                glm::vec2 tangent = clockwise ? glm::vec2(-delta.y, delta.x) : glm::vec2(delta.y, -delta.x);
                tangent /= dist;

                const glm::vec2 radial = -delta / dist;
                const glm::vec2 combined = tangent + inwardPull * radial;
                const float len = glm::length(combined);

                setVector(c, r, len > 1e-4f ? (combined / len) : tangent);
            } else {
                setVector(c, r, glm::vec2(0.0f, 0.0f));
            }
        }
    }
}

void VectorField::generateRadial(glm::vec2 center, bool outward) {
    const float sign = outward ? 1.0f : -1.0f;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const glm::vec2 pos = getCellCenter(c, r);
            const glm::vec2 delta = pos - center;
            const float dist = glm::length(delta);

            if (dist > 1e-4f) {
                setVector(c, r, sign * (delta / dist));
            } else {
                setVector(c, r, glm::vec2(0.0f, 0.0f));
            }
        }
    }
}

void VectorField::applyPreset(VectorFieldPreset preset, glm::vec2 center) {
    switch (preset) {
        case VectorFieldPreset::CircularClockwise:
            generateCircular(center, true);
            break;
        case VectorFieldPreset::CircularCounterClockwise:
            generateCircular(center, false);
            break;
        case VectorFieldPreset::VortexInward:
            generateVortex(center, 0.25f, true);
            break;
        case VectorFieldPreset::VortexOutward:
            generateVortex(center, -0.25f, true);
            break;
        case VectorFieldPreset::RadialInward:
            generateRadial(center, false);
            break;
        case VectorFieldPreset::RadialOutward:
            generateRadial(center, true);
            break;
        case VectorFieldPreset::Custom:
            break;
    }
}
