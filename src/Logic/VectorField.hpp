#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "Logic/BoundingBox.hpp"

enum class VectorFieldPreset {
    CircularClockwise,
    CircularCounterClockwise,
    VortexInward,
    VortexOutward,
    RadialInward,
    RadialOutward,
    Custom
};

class VectorField {
private:
    BoundingBox bounds{glm::vec2(0.0f, 0.0f), glm::vec2(6000.0f, 4000.0f)};
    float cellSize = 64.0f;
    int cols = 0;
    int rows = 0;
    std::vector<glm::vec2> field;

public:
    VectorField() = default;
    VectorField(const BoundingBox& bounds, float cellSize = 64.0f);

    void resize(const BoundingBox& newBounds, float newCellSize = 64.0f);
    void clear();

    // Sampling with bilinear interpolation for smooth vector transitions
    [[nodiscard]] glm::vec2 sample(glm::vec2 worldPos) const;

    // Direct grid access
    [[nodiscard]] glm::vec2 getVector(int col, int row) const;
    void setVector(int col, int row, glm::vec2 vec);
    [[nodiscard]] glm::vec2 getCellCenter(int col, int row) const;

    // Generators
    void generateCircular(glm::vec2 center, bool clockwise = true);
    void generateVortex(glm::vec2 center, float inwardPull = 0.15f, bool clockwise = true);
    void generateRadial(glm::vec2 center, bool outward = true);
    void applyPreset(VectorFieldPreset preset, glm::vec2 center);

    // Getters
    [[nodiscard]] const BoundingBox& getBounds() const {
        return bounds;
    }
    [[nodiscard]] float getCellSize() const {
        return cellSize;
    }
    [[nodiscard]] int getCols() const {
        return cols;
    }
    [[nodiscard]] int getRows() const {
        return rows;
    }
    [[nodiscard]] size_t getGridSize() const {
        return field.size();
    }
    [[nodiscard]] const std::vector<glm::vec2>& getField() const {
        return field;
    }
};
