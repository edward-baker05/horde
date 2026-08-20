#pragma once

#include <glm/vec2.hpp>

#include <cstddef>

#include "logic/Level.hpp"

namespace horde::editor {

// Shorter than this and a marker is a mis-click rather than a placement.
inline constexpr float kMinimumMarkerLength = 16.0f;

// Which edge of the level a world point is closest to.
logic::Edge nearestEdge(const logic::Level& level, glm::vec2 world);

// Distance along `edge` from its start corner, clamped to the edge. North and
// south run along x; east and west run along y.
float projectOntoEdge(const logic::Level& level, logic::Edge edge, glm::vec2 world);

// The largest free interval on `edge` that contains `at`, ignoring the marker at
// `ignoreIndex` (pass level.markers.size() when placing a new one).
//
// This is what makes overlap impossible: a drag is clamped into this span, so
// two markers can never occupy the same stretch of an edge even transiently.
void freeSpan(const logic::Level& level, logic::Edge edge, float at, std::size_t ignoreIndex, float& outLow,
              float& outHigh);

// Clamps the marker at `index` into its free span, shortening it if needed.
// Call after anything that could invalidate it, including a level resize.
void clampMarker(logic::Level& level, std::size_t index);

// Clamps every marker. Call after the level's size changes.
void clampAllMarkers(logic::Level& level);

// Builds a marker from a drag along the edge nearest `start`. Returns false if
// the resulting band would be shorter than kMinimumMarkerLength or if there is
// no free room where the drag began.
bool makeMarkerFromDrag(const logic::Level& level, logic::MarkerKind kind, glm::vec2 start, glm::vec2 end,
                        logic::Marker& out);

// The world positions of a marker's two end grips: the low-offset end first.
void markerEndHandles(const logic::Level& level, const logic::Marker& marker, glm::vec2& outLow, glm::vec2& outHigh);

} // namespace horde::editor
