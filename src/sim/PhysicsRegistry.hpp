#pragma once

#include <memory>
#include <string>
#include <vector>

#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// The physics system a level uses when nothing asks for another.
inline constexpr const char* kDefaultPhysics = "gpu-grid";

// Every registered system's name, in registration order.
std::vector<std::string> physicsNames();

// A one-line description for --list-physics and the debug panel.
const char* physicsDescription(const std::string& name);

// nullptr if `name` is not registered.
std::unique_ptr<PhysicsSystem> createPhysics(const std::string& name);

} // namespace horde::sim
