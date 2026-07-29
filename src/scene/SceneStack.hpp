#pragma once

#include <memory>
#include <vector>

#include "scene/Scene.hpp"

namespace horde::scene {

// A stack of scenes, with the top one active.
//
// A stack rather than a single pointer so that transient views (a pause menu
// over a level, a tooltip over the tech tree) can sit on top of what they
// interrupt without that scene having to tear itself down.
//
// Push/pop/replace requests are queued and applied by applyPending(), so a
// scene can safely request a transition from inside its own update or event
// handler without destroying the object currently executing.
class SceneStack {
public:
    explicit SceneStack(Services services);
    ~SceneStack();

    SceneStack(const SceneStack&) = delete;
    SceneStack& operator=(const SceneStack&) = delete;

    void push(std::unique_ptr<Scene> scene);
    void pop();
    void replace(std::unique_ptr<Scene> scene);

    // Applies queued transitions. Called once per frame by App.
    // Returns false if a scene failed to enter.
    bool applyPending();

    Scene* active() const {
        return m_scenes.empty() ? nullptr : m_scenes.back().get();
    }

    bool empty() const {
        return m_scenes.empty();
    }

    Services& services() {
        return m_services;
    }

private:
    enum class Action { Push, Pop, Replace };

    struct Request {
        Action action;
        std::unique_ptr<Scene> scene;
    };

    Services m_services;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<Request> m_pending;
};

} // namespace horde::scene
