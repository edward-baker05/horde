#pragma once

#include <utility>
#include <vector>

#include "logic/Level.hpp"

namespace horde::editor {

// Undo as whole-Level snapshots.
//
// Levels hold tens of walls, so copying one is trivially cheap — far cheaper
// than the per-command inverse logic a command pattern would need, and much
// harder to get subtly wrong. Push a snapshot BEFORE each mutation.
class UndoStack {
public:
    static constexpr std::size_t kMaxDepth = 64;

    // Records the state before a mutation. Discards any redo history, since the
    // user has now taken a different branch.
    void push(const logic::Level& level) {
        m_past.push_back(level);
        if (m_past.size() > kMaxDepth) {
            m_past.erase(m_past.begin());
        }
        m_future.clear();
    }

    bool undo(logic::Level& level) {
        if (m_past.empty()) {
            return false;
        }
        m_future.push_back(level);
        level = std::move(m_past.back());
        m_past.pop_back();
        return true;
    }

    bool redo(logic::Level& level) {
        if (m_future.empty()) {
            return false;
        }
        m_past.push_back(level);
        level = std::move(m_future.back());
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
    std::vector<logic::Level> m_past;
    std::vector<logic::Level> m_future;
};

} // namespace horde::editor
