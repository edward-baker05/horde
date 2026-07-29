#pragma once

#include <SDL3/SDL_events.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

namespace horde::gfx {

// Orthographic 2D camera with pan and zoom.
//
// Shared deliberately between the level view and the tech tree: a pannable,
// zoomable graph of upgrade icons is the same problem as a pannable, zoomable
// top-down world, so both are just this camera plus a different set of sprites.
class Camera2D {
public:
    // Viewport size in pixels. Call on resize.
    void setViewport(float width, float height);

    // World-space point at the centre of the screen.
    void setCenter(glm::vec2 center) {
        m_center = center;
    }

    glm::vec2 center() const {
        return m_center;
    }

    void pan(glm::vec2 worldDelta) {
        m_center += worldDelta;
    }

    // 1.0 = one world unit per pixel. Larger zooms in. Clamped to
    // [minZoom, maxZoom].
    void setZoom(float zoom);

    float zoom() const {
        return m_zoom;
    }

    void setZoomLimits(float minZoom, float maxZoom);

    // Multiplies zoom by `factor` while keeping `screenAnchor` over the same
    // world point — the "zoom towards the cursor" behaviour a tech tree needs.
    void zoomAround(float factor, glm::vec2 screenAnchor);

    glm::vec2 screenToWorld(glm::vec2 screen) const;
    glm::vec2 worldToScreen(glm::vec2 world) const;

    // Column-major view-projection matrix, ready to upload as a uniform.
    glm::mat4 viewProjection() const;

private:
    glm::vec2 m_center{0.0f, 0.0f};
    glm::vec2 m_viewport{1.0f, 1.0f};
    float m_zoom = 1.0f;
    float m_minZoom = 0.1f;
    float m_maxZoom = 10.0f;
};

// Mouse-driven pan and zoom, so the level view and the tech tree share one
// implementation instead of drifting apart.
//
// Returns true if the event was consumed.
class CameraController {
public:
    bool handleEvent(const SDL_Event& event, Camera2D& camera);

    void setZoomSpeed(float speed) {
        m_zoomSpeed = speed;
    }

private:
    bool m_dragging = false;
    glm::vec2 m_lastMouse{0.0f, 0.0f};
    float m_zoomSpeed = 1.1f;
};

} // namespace horde::gfx
