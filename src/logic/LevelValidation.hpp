#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "logic/Level.hpp"

namespace horde::logic {

// One reason a level cannot be saved.
struct Problem {
    std::size_t wallIndex = 0; // index into Level::walls
    std::string message;       // shown to the user verbatim
};

// Every reason this level cannot be saved, in wall order. Empty means valid.
//
// Only walls are checked. Marker invariants — staying on an edge, not
// overlapping, always having at least one spawn and one exit — are maintained
// by construction in the editor rather than validated here. See spec section 3.
std::vector<Problem> validate(const Level& level);

} // namespace horde::logic
