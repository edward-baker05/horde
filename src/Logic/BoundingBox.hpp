#pragma once

#include <glm/glm.hpp>

#include <algorithm>

struct BoundingBox {
    glm::vec2 min{0.0f, 0.0f};
    glm::vec2 max{0.0f, 0.0f};

    BoundingBox() = default;
    inline BoundingBox(glm::vec2 minVal, glm::vec2 maxVal) : min(minVal), max(maxVal) {}

    [[nodiscard]] static inline BoundingBox fromMinMax(glm::vec2 minVal, glm::vec2 maxVal) {
        return BoundingBox(minVal, maxVal);
    }

    [[nodiscard]] static inline BoundingBox fromPositionSize(glm::vec2 pos, glm::vec2 size) {
        return BoundingBox(pos, pos + size);
    }

    [[nodiscard]] static inline BoundingBox fromCenterHalfExtents(glm::vec2 center, glm::vec2 halfExtents) {
        return BoundingBox(center - halfExtents, center + halfExtents);
    }

    [[nodiscard]] inline bool intersects(const BoundingBox& other) const {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y;
    }

    [[nodiscard]] inline bool contains(glm::vec2 point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }

    [[nodiscard]] inline bool contains(const BoundingBox& other) const {
        return other.min.x >= min.x && other.max.x <= max.x && other.min.y >= min.y && other.max.y <= max.y;
    }

    [[nodiscard]] inline glm::vec2 getCenter() const {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] inline glm::vec2 getSize() const {
        return max - min;
    }

    [[nodiscard]] inline glm::vec2 getHalfSize() const {
        return (max - min) * 0.5f;
    }

    [[nodiscard]] inline BoundingBox expanded(float amount) const {
        return BoundingBox(min - glm::vec2(amount), max + glm::vec2(amount));
    }

    [[nodiscard]] inline BoundingBox translated(glm::vec2 offset) const {
        return BoundingBox(min + offset, max + offset);
    }

    [[nodiscard]] inline glm::vec2 clamp(glm::vec2 point) const {
        return glm::clamp(point, min, max);
    }

    [[nodiscard]] inline bool getOverlap(const BoundingBox& other, glm::vec2& outNormal, float& outDepth) const {
        const float overlapX = std::min(max.x, other.max.x) - std::max(min.x, other.min.x);
        const float overlapY = std::min(max.y, other.max.y) - std::max(min.y, other.min.y);

        if (overlapX <= 0.0f || overlapY <= 0.0f) {
            return false;
        }

        if (overlapX < overlapY) {
            outDepth = overlapX;
            outNormal = (getCenter().x < other.getCenter().x) ? glm::vec2(-1.0f, 0.0f) : glm::vec2(1.0f, 0.0f);
        } else {
            outDepth = overlapY;
            outNormal = (getCenter().y < other.getCenter().y) ? glm::vec2(0.0f, -1.0f) : glm::vec2(0.0f, 1.0f);
        }

        return true;
    }
};
