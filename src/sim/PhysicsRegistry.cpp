#include "sim/PhysicsRegistry.hpp"

#include <functional>

#include "sim/CpuGridPhysics.hpp"
#include "sim/GpuCollidePhysics.hpp"
#include "sim/StaticPhysics.hpp"

namespace horde::sim {

namespace {

struct Entry {
    const char* name;
    const char* description;
    std::function<std::unique_ptr<PhysicsSystem>()> create;
};

// To add a physics system: implement PhysicsSystem, add its .cpp to
// CMakeLists.txt, and add a line here. It is then selectable from the level's
// debug panel and from `horde --bench --physics <name>`.
const std::vector<Entry>& entries() {
    static const std::vector<Entry> table = {
        {"gpu-collide", "GPU: steer to the exit, collide with units and walls; counting-sort broadphase",
         [] { return std::make_unique<GpuCollidePhysics>(); }},
        {"cpu-grid", "CPU single-threaded: linked-list uniform grid, 4 substeps, 6 solver iterations",
         [] { return std::make_unique<CpuGridPhysics>(); }},
        {"static", "No simulation; draws units where they spawned. Baseline for rendering cost",
         [] { return std::make_unique<StaticPhysics>(); }},
    };
    return table;
}

} // namespace

std::vector<std::string> physicsNames() {
    std::vector<std::string> names;
    for (const Entry& entry : entries()) {
        names.emplace_back(entry.name);
    }
    return names;
}

const char* physicsDescription(const std::string& name) {
    for (const Entry& entry : entries()) {
        if (name == entry.name) {
            return entry.description;
        }
    }
    return "";
}

std::unique_ptr<PhysicsSystem> createPhysics(const std::string& name) {
    for (const Entry& entry : entries()) {
        if (name == entry.name) {
            return entry.create();
        }
    }
    return nullptr;
}

} // namespace horde::sim
