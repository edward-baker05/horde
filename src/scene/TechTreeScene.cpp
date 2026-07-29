#include "scene/TechTreeScene.hpp"

#include <imgui.h>

#include <cmath>

#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

bool TechTreeScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    return true;
}

bool TechTreeScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void TechTreeScene::render(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();

    // The edge, as a thin rotated quad. Sprites rotate about their top-left
    // corner, so offset by half the thickness along the edge normal to centre
    // the line on the two nodes.
    const glm::vec2 delta = m_nodeB - m_nodeA;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const glm::vec2 normal{-delta.y / length, delta.x / length};

    gfx::Sprite edge;
    edge.position = {m_nodeA.x - normal.x * kEdgeThickness * 0.5f, m_nodeA.y - normal.y * kEdgeThickness * 0.5f, 0.0f};
    edge.size = {length, kEdgeThickness};
    edge.rotation = std::atan2(delta.y, delta.x);
    edge.uv = gfx::atlasCell(1, 1, 2, 2);
    edge.color = {0.4f, 0.4f, 0.45f, 1.0f};

    batch.draw(edge, atlas);

    for (const glm::vec2& position : {m_nodeA, m_nodeB}) {
        gfx::Sprite node;
        node.position = {position.x - kNodeSize * 0.5f, position.y - kNodeSize * 0.5f, 0.1f};
        node.size = {kNodeSize, kNodeSize};
        node.uv = gfx::atlasCell(1, 0, 2, 2);
        node.color = {0.7f, 0.9f, 0.7f, 1.0f};

        batch.draw(node, atlas);
    }
}

void TechTreeScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tech tree");

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->pop();
    }

    ImGui::End();
}

void TechTreeScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
