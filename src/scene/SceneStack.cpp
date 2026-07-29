#include "scene/SceneStack.hpp"

#include <SDL3/SDL_log.h>

#include <utility>

namespace horde::scene {

SceneStack::SceneStack(Services services) : m_services(services) {
    m_services.scenes = this;
}

SceneStack::~SceneStack() {
    while (!m_scenes.empty()) {
        m_scenes.back()->onExit();
        m_scenes.pop_back();
    }
}

void SceneStack::push(std::unique_ptr<Scene> scene) {
    m_pending.push_back({Action::Push, std::move(scene)});
}

void SceneStack::pop() {
    m_pending.push_back({Action::Pop, nullptr});
}

void SceneStack::replace(std::unique_ptr<Scene> scene) {
    m_pending.push_back({Action::Replace, std::move(scene)});
}

bool SceneStack::applyPending() {
    if (m_pending.empty()) {
        return true;
    }

    // Move out first: entering a scene may itself queue a transition, and that
    // must land in the next batch rather than mutate what we are iterating.
    std::vector<Request> requests = std::move(m_pending);
    m_pending.clear();

    for (Request& request : requests) {
        switch (request.action) {
            case Action::Pop:
                if (!m_scenes.empty()) {
                    m_scenes.back()->onExit();
                    m_scenes.pop_back();
                }
                break;

            case Action::Replace:
                if (!m_scenes.empty()) {
                    m_scenes.back()->onExit();
                    m_scenes.pop_back();
                }
                [[fallthrough]];

            case Action::Push: {
                if (request.scene == nullptr) {
                    break;
                }

                const char* name = request.scene->name();

                if (!request.scene->onEnter(m_services)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scene '%s' failed to enter", name);
                    return false;
                }

                m_scenes.push_back(std::move(request.scene));
                SDL_Log("Scene: %s", name);
                break;
            }
        }
    }

    return true;
}

} // namespace horde::scene
