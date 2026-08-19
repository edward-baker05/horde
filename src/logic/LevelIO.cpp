#include "logic/LevelIO.hpp"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace horde::logic {
namespace {

using nlohmann::json;

// Every reader below reports a missing or wrongly-typed key as a failure rather
// than defaulting it. A level file missing a wall's centre is a corrupt file,
// not a file with a wall at the origin.
bool readFloat(const json& node, const char* key, float& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_number()) {
        if (error) {
            *error = std::string("missing or non-numeric key: ") + key;
        }
        return false;
    }
    out = node[key].get<float>();
    return true;
}

bool readVec2(const json& node, const char* key, glm::vec2& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_array() || node[key].size() != 2 || !node[key][0].is_number() ||
        !node[key][1].is_number()) {
        if (error) {
            *error = std::string("key is not a two-number array: ") + key;
        }
        return false;
    }
    out = glm::vec2{node[key][0].get<float>(), node[key][1].get<float>()};
    return true;
}

bool readRgb(const json& node, const char* key, Rgb& out, std::string* error) {
    if (!node.contains(key) || !node[key].is_array() || node[key].size() != 3) {
        if (error) {
            *error = std::string("key is not a three-number array: ") + key;
        }
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!node[key][i].is_number_integer()) {
            if (error) {
                *error = std::string("colour component is not an integer: ") + key;
            }
            return false;
        }
    }
    out.r = static_cast<std::uint8_t>(std::clamp(node[key][0].get<int>(), 0, 255));
    out.g = static_cast<std::uint8_t>(std::clamp(node[key][1].get<int>(), 0, 255));
    out.b = static_cast<std::uint8_t>(std::clamp(node[key][2].get<int>(), 0, 255));
    return true;
}

json writeVec2(glm::vec2 v) {
    return json::array({v.x, v.y});
}

json writeRgb(Rgb c) {
    return json::array({static_cast<int>(c.r), static_cast<int>(c.g), static_cast<int>(c.b)});
}

const char* edgeName(Edge edge) {
    switch (edge) {
        case Edge::North:
            return "north";
        case Edge::South:
            return "south";
        case Edge::East:
            return "east";
        case Edge::West:
            return "west";
    }
    return "west";
}

bool parseEdge(const std::string& name, Edge& out) {
    if (name == "north") {
        out = Edge::North;
        return true;
    }
    if (name == "south") {
        out = Edge::South;
        return true;
    }
    if (name == "east") {
        out = Edge::East;
        return true;
    }
    if (name == "west") {
        out = Edge::West;
        return true;
    }
    return false;
}

bool readWall(const json& node, Wall& wall, std::string* error) {
    if (!node.contains("kind") || !node["kind"].is_string()) {
        if (error) {
            *error = "wall has no 'kind'";
        }
        return false;
    }

    if (!readVec2(node, "center", wall.center, error) || !readRgb(node, "color", wall.color, error)) {
        return false;
    }

    // Circles omit rotation, since it is meaningless for them. Any wall without
    // the key reads back as zero. This is the ONLY optional key.
    float degrees = 0.0f;
    if (node.contains("rotation")) {
        if (!readFloat(node, "rotation", degrees, error)) {
            return false;
        }
    }
    wall.rotation = glm::radians(degrees);

    const std::string kind = node["kind"].get<std::string>();

    if (kind == "rectangle" || kind == "triangle") {
        glm::vec2 halfExtents{};
        if (!readVec2(node, "halfExtents", halfExtents, error)) {
            return false;
        }
        if (kind == "rectangle") {
            wall.shape = RectangleShape{halfExtents};
        } else {
            wall.shape = TriangleShape{halfExtents};
        }
        return true;
    }

    if (kind == "circle") {
        float radius = 0.0f;
        if (!readFloat(node, "radius", radius, error)) {
            return false;
        }
        wall.shape = CircleShape{radius};
        return true;
    }

    if (kind == "polyline") {
        PolylineShape line;
        if (!readFloat(node, "thickness", line.thickness, error)) {
            return false;
        }
        if (!node.contains("points") || !node["points"].is_array()) {
            if (error) {
                *error = "polyline has no 'points' array";
            }
            return false;
        }
        for (const json& point : node["points"]) {
            if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number()) {
                if (error) {
                    *error = "polyline point is not a two-number array";
                }
                return false;
            }
            line.points.push_back(glm::vec2{point[0].get<float>(), point[1].get<float>()});
        }
        wall.shape = std::move(line);
        return true;
    }

    if (error) {
        *error = "unknown wall kind: " + kind;
    }
    return false;
}

json writeWall(const Wall& wall) {
    json node;
    node["center"] = writeVec2(wall.center);
    node["color"] = writeRgb(wall.color);

    if (const auto* rect = std::get_if<RectangleShape>(&wall.shape)) {
        node["kind"] = "rectangle";
        node["rotation"] = glm::degrees(wall.rotation);
        node["halfExtents"] = writeVec2(rect->halfExtents);
    } else if (const auto* tri = std::get_if<TriangleShape>(&wall.shape)) {
        node["kind"] = "triangle";
        node["rotation"] = glm::degrees(wall.rotation);
        node["halfExtents"] = writeVec2(tri->halfExtents);
    } else if (const auto* circle = std::get_if<CircleShape>(&wall.shape)) {
        node["kind"] = "circle"; // no rotation: meaningless for a circle
        node["radius"] = circle->radius;
    } else {
        const auto& line = std::get<PolylineShape>(wall.shape);
        node["kind"] = "polyline";
        node["rotation"] = glm::degrees(wall.rotation);
        node["thickness"] = line.thickness;
        json points = json::array();
        for (const glm::vec2& point : line.points) {
            points.push_back(writeVec2(point));
        }
        node["points"] = std::move(points);
    }

    return node;
}

} // namespace

std::optional<Level> loadLevel(const std::filesystem::path& path, std::string* error) {
    std::ifstream stream(path);
    if (!stream) {
        if (error) {
            *error = "cannot open " + path.string();
        }
        return std::nullopt;
    }

    const json root = json::parse(stream, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error) {
            *error = "malformed JSON in " + path.string();
        }
        return std::nullopt;
    }

    if (!root.contains("version") || !root["version"].is_number_integer() ||
        root["version"].get<int>() != kLevelFormatVersion) {
        if (error) {
            *error = "unsupported or missing level format version";
        }
        return std::nullopt;
    }

    Level level;
    if (!readVec2(root, "size", level.size, error) || !readRgb(root, "backgroundColor", level.backgroundColor, error)) {
        return std::nullopt;
    }

    if (root.contains("walls")) {
        if (!root["walls"].is_array()) {
            if (error) {
                *error = "'walls' is not an array";
            }
            return std::nullopt;
        }
        for (const json& node : root["walls"]) {
            Wall wall;
            if (!readWall(node, wall, error)) {
                return std::nullopt;
            }
            level.walls.push_back(std::move(wall));
        }
    }

    if (root.contains("markers")) {
        if (!root["markers"].is_array()) {
            if (error) {
                *error = "'markers' is not an array";
            }
            return std::nullopt;
        }
        for (const json& node : root["markers"]) {
            Marker marker;
            if (!node.contains("kind") || !node["kind"].is_string() || !node.contains("edge") ||
                !node["edge"].is_string()) {
                if (error) {
                    *error = "marker is missing 'kind' or 'edge'";
                }
                return std::nullopt;
            }
            const std::string kind = node["kind"].get<std::string>();
            if (kind == "spawn") {
                marker.kind = MarkerKind::Spawn;
            } else if (kind == "exit") {
                marker.kind = MarkerKind::Exit;
            } else {
                if (error) {
                    *error = "unknown marker kind: " + kind;
                }
                return std::nullopt;
            }
            if (!parseEdge(node["edge"].get<std::string>(), marker.edge)) {
                if (error) {
                    *error = "unknown marker edge";
                }
                return std::nullopt;
            }
            if (!readFloat(node, "offset", marker.offset, error) || !readFloat(node, "length", marker.length, error)) {
                return std::nullopt;
            }
            level.markers.push_back(marker);
        }
    }

    // A level on disk with no markers predates nothing and means a hand-edited
    // file lost them. Restore the minimum rather than refusing to load.
    if (countMarkers(level, MarkerKind::Spawn) == 0) {
        level.markers.push_back(Marker{MarkerKind::Spawn, Edge::West, (level.size.y - 100.0f) * 0.5f, 100.0f});
    }
    if (countMarkers(level, MarkerKind::Exit) == 0) {
        level.markers.push_back(Marker{MarkerKind::Exit, Edge::East, (level.size.y - 100.0f) * 0.5f, 100.0f});
    }

    return level;
}

bool saveLevel(const Level& level, const std::filesystem::path& path, std::string* error) {
    json root;
    root["version"] = kLevelFormatVersion;
    root["size"] = writeVec2(level.size);
    root["backgroundColor"] = writeRgb(level.backgroundColor);

    json walls = json::array();
    for (const Wall& wall : level.walls) {
        walls.push_back(writeWall(wall));
    }
    root["walls"] = std::move(walls);

    json markers = json::array();
    for (const Marker& marker : level.markers) {
        json node;
        node["kind"] = marker.kind == MarkerKind::Spawn ? "spawn" : "exit";
        node["edge"] = edgeName(marker.edge);
        node["offset"] = marker.offset;
        node["length"] = marker.length;
        markers.push_back(std::move(node));
    }
    root["markers"] = std::move(markers);

    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);

    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        if (error) {
            *error = "cannot write " + path.string();
        }
        return false;
    }

    stream << root.dump(2) << '\n';
    if (!stream) {
        if (error) {
            *error = "write failed for " + path.string();
        }
        return false;
    }

    return true;
}

} // namespace horde::logic
