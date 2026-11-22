#pragma once

#include <utility>
#include <vector>

#include "logic/Level.hpp"

namespace horde::editor {

// Undo as whole snapshots of a value: the editor's Level, or the turrets
// placed during a level's prep phase.
//
// Levels hold tens of walls, so copying one is trivially cheap — far cheaper
// than the per-command inverse logic a command pattern would need, and much
// harder to get subtly wrong. Push a snapshot BEFORE each mutation.
template <typename T>
class SnapshotStack {
public:
    static constexpr std::size_t kMaxDepth = 64;

    // Records the state before a mutation. Discards any redo history, since the
    // user has now taken a different branch.
    void push(const T& value) {
        m_past.push_back(value);
        if (m_past.size() > kMaxDepth) {
            m_past.erase(m_past.begin());
        }
        m_future.clear();
    }

    bool undo(T& value) {
        if (m_past.empty()) {
            return false;
        }
        m_future.push_back(value);
        value = std::move(m_past.back());
        m_past.pop_back();
        return true;
    }

    bool redo(T& value) {
        if (m_future.empty()) {
            return false;
        }
        m_past.push_back(value);
        value = std::move(m_future.back());
        m_future.pop_back();
        return true;
    }

    bool canUndo() const {
        return !m_past.empty();
    }

    bool canRedo() const {
        return !m_future.empty();
    }

    void clear() {
        m_past.clear();
        m_future.clear();
    }

private:
    std::vector<T> m_past;
    std::vector<T> m_future;
};

using UndoStack = SnapshotStack<logic::Level>;

} // namespace horde::editor
