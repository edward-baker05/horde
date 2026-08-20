#include "gfx/Camera2D.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <algorithm>

namespace horde::gfx {

void Camera2D::setViewport(float width, float height) {
    m_viewport = {std::max(width, 1.0f), std::max(height, 1.0f)};
}

void Camera2D::setZoom(float zoom) {
    m_zoom = std::clamp(zoom, m_minZoom, m_maxZoom);
}

void Camera2D::setZoomLimits(float minZoom, float maxZoom) {
    m_minZoom = std::max(minZoom, 0.0001f);
    m_maxZoom = std::max(maxZoom, m_minZoom);
    setZoom(m_zoom);
}

void Camera2D::zoomAround(float factor, glm::vec2 screenAnchor) {
    const glm::vec2 worldBefore = screenToWorld(screenAnchor);
    setZoom(m_zoom * factor);
    const glm::vec2 worldAfter = screenToWorld(screenAnchor);

    // Shift the centre so the anchor lands back on the same world point. If the
    // zoom clamped, the two are equal and this is a no-op.
    m_center += worldBefore - worldAfter;
}

glm::vec2 Camera2D::screenToWorld(glm::vec2 screen) const {
    // Screen origin is top-left with y down; world is y down as well, so the
    // conversion is a straight scale about the viewport centre.
    const glm::vec2 fromCenter = screen - m_viewport * 0.5f;
    return m_center + fromCenter / m_zoom;
}

glm::vec2 Camera2D::worldToScreen(glm::vec2 world) const {
    return (world - m_center) * m_zoom + m_viewport * 0.5f;
}

BoundingBox Camera2D::visibleBounds(float margin) const {
    const glm::vec2 halfExtent = (m_viewport * 0.5f / m_zoom) + glm::vec2(margin);
    return BoundingBox::fromCenterHalfExtents(m_center, halfExtent);
}

glm::mat4 Camera2D::viewProjection() const {
    const glm::vec2 halfExtent = m_viewport * 0.5f / m_zoom;

    // y is flipped (top < bottom) so that world +y points down, matching screen
    // and tile coordinates.
    const glm::mat4 projection = glm::orthoRH_ZO(m_center.x - halfExtent.x, m_center.x + halfExtent.x,
                                                 m_center.y + halfExtent.y, m_center.y - halfExtent.y, -1.0f, 1.0f);

    return projection;
}

bool CameraController::handleEvent(const SDL_Event& event, Camera2D& camera) {
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_dragging = true;
                m_lastMouse = {event.button.x, event.button.y};
                return true;
            }
            return false;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_dragging = false;
                return true;
            }
            return false;

        case SDL_EVENT_MOUSE_MOTION: {
            if (!m_dragging) {
                return false;
            }

            const glm::vec2 mouse{event.motion.x, event.motion.y};
            // Convert the pixel delta to world units so the grabbed point stays
            // under the cursor at any zoom level.
            camera.pan((m_lastMouse - mouse) / camera.zoom());
            m_lastMouse = mouse;
            return true;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            if (event.wheel.y == 0.0f) {
                return false;
            }

            float mouseX = 0.0f;
            float mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);

            const float factor = event.wheel.y > 0.0f ? m_zoomSpeed : 1.0f / m_zoomSpeed;
            camera.zoomAround(factor, {mouseX, mouseY});
            return true;
        }

        default:
            return false;
    }
}

} // namespace horde::gfx
