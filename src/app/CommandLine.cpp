#include "app/CommandLine.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "sim/PhysicsRegistry.hpp"

namespace horde::app {

namespace {

void printUsage() {
    std::printf(
        "usage: horde [options]\n"
        "\n"
        "With no options the game starts at the main menu.\n"
        "\n"
        "Level setup (boots straight into a level when any is given):\n"
        "  --physics NAME     physics system (see --list-physics), default %s\n"
        "  --units N          number of units, default 10000\n"
        "  --spawn PATTERN    markers | random | grid | column, default markers (random under --bench)\n"
        "  --spawn-rate N     markers: units entering per second, default 1000; 0 = all at once\n"
        "  --no-respawn       units stop at the exit instead of re-entering at a spawn\n"
        "  --seed N           spawn seed, default 1\n"
        "  --level PATH       level file, default the built-in level\n"
        "  --level-size WxH   the built-in level at another size, e.g. 8000x8000\n"
        "  --collision-iterations N   relaxation passes per frame, default 4\n"
        "  --solver NAME      how a Jacobi pass combines a unit's overlaps: relaxed (sum x\n"
        "                     relaxation, the default) | average (mean of the overlaps)\n"
        "  --relaxation F     relaxed solver: fraction of each pass's correction applied, default 0.5\n"
        "  --omega F          average solver: over-relaxation on the mean, 1 (plain) to 2, default 1\n"
        "  --speed F          unit speed toward the exit, default 60\n"
        "  --broadphase NAME  counting | buckets | bitonic, default counting\n"
        "  --bucket-capacity N  units per cell for the buckets broadphase, default 8\n"
        "  --quality NAME     low | medium | high | ultra: sets passes, relaxation and\n"
        "                     broadphase together; later flags still override it\n"
        "\n"
        "Benchmarking:\n"
        "  --bench            run headless-style: time the level, write results, quit\n"
        "  --warmup S         seconds to run before measuring, default 2\n"
        "  --duration S       seconds to measure, default 10\n"
        "  --fixed-dt S       simulation step per frame, default 1/60; 0 = real time\n"
        "  --out FILE         write the JSON summary here\n"
        "  --frames-out FILE  write per-frame timings as CSV here\n"
        "  --units-out FILE   write every unit's final position and velocity as CSV here\n"
        "  --no-gpu-sync      don't time the physics' GPU work separately each frame\n"
        "\n"
        "Window and device:\n"
        "  --width W --height H   window size, default 1600x900\n"
        "  --vsync / --no-vsync   default on, off under --bench\n"
        "  --gpu-debug / --no-gpu-debug   GPU validation, default on, off under --bench\n"
        "  --low-power-gpu    prefer an integrated GPU, if the machine has one\n"
        "\n"
        "  --list-physics     print the registered physics systems and exit\n"
        "  --help             print this and exit\n",
        sim::kDefaultPhysics);
}

template <typename T>
bool parseNumber(std::string_view text, T& out) {
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(text.data(), end, out);
    return ec == std::errc{} && ptr == end;
}

} // namespace

ParseResult parseCommandLine(int argc, char* argv[], AppConfig& config) {
    scene::LevelOptions level;
    bench::BenchConfig benchConfig;
    bool bench = false;
    bool wantsLevel = false;
    bool spawnGiven = false;
    int vsync = -1; // -1 = not given
    int gpuDebug = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        // Flags taking a value read it from the next argument.
        const auto value = [&](std::string_view& out) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "horde: %s needs a value\n", argv[i]);
                return false;
            }
            out = argv[++i];
            return true;
        };
        const auto bad = [&](std::string_view text) {
            std::fprintf(stderr, "horde: bad value '%.*s' for %.*s\n", static_cast<int>(text.size()), text.data(),
                         static_cast<int>(arg.size()), arg.data());
            return ParseResult::Error;
        };

        std::string_view v;

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return ParseResult::Exit;
        } else if (arg == "--list-physics") {
            for (const std::string& name : sim::physicsNames()) {
                std::printf("%-12s %s\n", name.c_str(), sim::physicsDescription(name));
            }
            return ParseResult::Exit;
        } else if (arg == "--bench") {
            bench = true;
        } else if (arg == "--physics") {
            if (!value(v)) return ParseResult::Error;
            level.physics = std::string(v);
            if (sim::createPhysics(level.physics) == nullptr) {
                std::fprintf(stderr, "horde: unknown physics '%s'. Known:", level.physics.c_str());
                for (const std::string& name : sim::physicsNames()) {
                    std::fprintf(stderr, " %s", name.c_str());
                }
                std::fprintf(stderr, "\n");
                return ParseResult::Error;
            }
            wantsLevel = true;
        } else if (arg == "--units") {
            if (!value(v) || !parseNumber(v, level.unitCount)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--spawn") {
            if (!value(v) || !sim::parseSpawnPattern(std::string(v), level.spawn)) return bad(v);
            spawnGiven = true;
            wantsLevel = true;
        } else if (arg == "--spawn-rate") {
            if (!value(v) || !parseNumber(v, level.spawnRate) || level.spawnRate < 0) return bad(v);
            wantsLevel = true;
        } else if (arg == "--no-respawn") {
            level.respawnAtExit = false;
            wantsLevel = true;
        } else if (arg == "--seed") {
            if (!value(v) || !parseNumber(v, level.seed)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--level") {
            if (!value(v)) return ParseResult::Error;
            level.levelPath = std::filesystem::path(v);
            wantsLevel = true;
        } else if (arg == "--level-size") {
            if (!value(v)) return ParseResult::Error;
            const std::size_t x = v.find('x');
            glm::vec2 size{0.0f};
            if (x == std::string_view::npos || !parseNumber(v.substr(0, x), size.x) ||
                !parseNumber(v.substr(x + 1), size.y) || size.x < 50.0f || size.y < 50.0f) {
                return bad(v);
            }
            // The default level, its markers stretched along the new edges.
            logic::Level sized = logic::makeDefaultLevel();
            sized.size = size;
            for (logic::Marker& marker : sized.markers) {
                marker.offset = 0.0f;
                marker.length = logic::edgeLength(sized, marker.edge);
            }
            level.level = sized;
            wantsLevel = true;
        } else if (arg == "--collision-iterations") {
            if (!value(v) || !parseNumber(v, level.collision.iterations)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--relaxation") {
            if (!value(v) || !parseNumber(v, level.collision.relaxation) || level.collision.relaxation <= 0) {
                return bad(v);
            }
            wantsLevel = true;
        } else if (arg == "--speed") {
            if (!value(v) || !parseNumber(v, level.maxSpeed) || level.maxSpeed < 0) return bad(v);
            wantsLevel = true;
        } else if (arg == "--broadphase") {
            if (!value(v) || !sim::parseBroadphase(std::string(v), level.collision.broadphase)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--bucket-capacity") {
            if (!value(v) || !parseNumber(v, level.collision.bucketCapacity) || level.collision.bucketCapacity == 0) {
                return bad(v);
            }
            wantsLevel = true;
        } else if (arg == "--solver") {
            if (!value(v) || !sim::parseSolver(std::string(v), level.collision.solver)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--omega") {
            if (!value(v) || !parseNumber(v, level.collision.averageOmega) || level.collision.averageOmega <= 0 ||
                level.collision.averageOmega > 2.0f) {
                return bad(v);
            }
            wantsLevel = true;
        } else if (arg == "--quality") {
            if (!value(v) || !sim::collisionPreset(std::string(v), level.collision)) return bad(v);
            wantsLevel = true;
        } else if (arg == "--low-power-gpu") {
            config.lowPowerGpu = true;
        } else if (arg == "--warmup") {
            if (!value(v) || !parseNumber(v, benchConfig.warmupSeconds) || benchConfig.warmupSeconds < 0) return bad(v);
        } else if (arg == "--duration") {
            if (!value(v) || !parseNumber(v, benchConfig.durationSeconds) || benchConfig.durationSeconds <= 0) {
                return bad(v);
            }
        } else if (arg == "--fixed-dt") {
            if (!value(v) || !parseNumber(v, benchConfig.fixedDt) || benchConfig.fixedDt < 0) return bad(v);
        } else if (arg == "--out") {
            if (!value(v)) return ParseResult::Error;
            benchConfig.outPath = std::filesystem::path(v);
        } else if (arg == "--frames-out") {
            if (!value(v)) return ParseResult::Error;
            benchConfig.framesPath = std::filesystem::path(v);
        } else if (arg == "--units-out") {
            if (!value(v)) return ParseResult::Error;
            benchConfig.unitsPath = std::filesystem::path(v);
        } else if (arg == "--no-gpu-sync") {
            benchConfig.syncGpu = false;
        } else if (arg == "--width") {
            if (!value(v) || !parseNumber(v, config.width) || config.width <= 0) return bad(v);
        } else if (arg == "--height") {
            if (!value(v) || !parseNumber(v, config.height) || config.height <= 0) return bad(v);
        } else if (arg == "--vsync") {
            vsync = 1;
        } else if (arg == "--no-vsync") {
            vsync = 0;
        } else if (arg == "--gpu-debug") {
            gpuDebug = 1;
        } else if (arg == "--no-gpu-debug") {
            gpuDebug = 0;
        } else {
            std::fprintf(stderr, "horde: unknown option '%s' (try --help)\n", argv[i]);
            return ParseResult::Error;
        }
    }

    if (bench) {
        // Benchmarks measure the uncapped frame rate, without the validation
        // layer's overhead, from a spread-out start — unless told otherwise.
        config.vsync = false;
        config.gpuDebug = false;
        if (!spawnGiven) {
            level.spawn = sim::SpawnPattern::Random;
        }
        level.fitCamera = true;
        benchConfig.level = level;
        config.bench = benchConfig;
    } else if (wantsLevel) {
        config.startLevel = level;
    }

    if (vsync != -1) config.vsync = vsync == 1;
    if (gpuDebug != -1) config.gpuDebug = gpuDebug == 1;

    return ParseResult::Run;
}

} // namespace horde::app
