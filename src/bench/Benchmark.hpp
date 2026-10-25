#pragma once

#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <string>
#include <vector>

#include "bench/Quality.hpp"
#include "scene/LevelScene.hpp"

namespace horde::bench {

// A single benchmark run: boot straight into a level, let it settle, time a
// fixed number of seconds of frames, write the results and quit.
struct BenchConfig {
    scene::LevelOptions level;
    double warmupSeconds = 2.0;
    double durationSeconds = 10.0;
    // Simulation step per frame. Fixed so that every run advances the
    // simulation identically however fast it renders; 0 uses real frame time.
    float fixedDt = 1.0f / 60.0f;
    // Submit the scene's compute work separately each frame and wait for it.
    // Costs some CPU/GPU overlap, but gives the simulation's GPU time on its
    // own, which a frame-rate cap cannot hide.
    bool syncGpu = true;
    // Where the JSON summary is written. Empty prints it to stdout only.
    std::filesystem::path outPath;
    // Optional per-frame CSV.
    std::filesystem::path framesPath;
};

// One frame's timings, in nanoseconds.
struct FrameSample {
    Uint64 frame = 0;  // whole loop iteration, start to start
    Uint64 update = 0; // scene update (CPU simulation)
    Uint64 render = 0;  // command recording, swapchain wait and submit
    Uint64 acquire = 0; // the swapchain wait alone: time blocked on the display/GPU
    Uint64 gpu = 0;     // the scene's compute work alone, waited on (syncGpu only)
};

// Run metadata the recorder cannot see for itself.
struct RunInfo {
    std::string videoDriver;
    std::string gpuDriver;
    std::string shaderFormat;
    std::string presentMode;
    int windowWidth = 0;
    int windowHeight = 0;
    // Of the units at the end of the run, if the physics system can read them back.
    CollisionQuality quality;
};

// Collects frame samples across the warmup and measurement windows.
class Recorder {
public:
    explicit Recorder(BenchConfig config) : m_config(std::move(config)) {}

    const BenchConfig& config() const {
        return m_config;
    }

    // Call once per frame. Returns false once the measurement window is over.
    bool record(const FrameSample& sample);

    // Writes the JSON summary (and frame CSV, if asked for). Returns false if
    // a file could not be written.
    bool finish(const RunInfo& info) const;

private:
    BenchConfig m_config;
    Uint64 m_elapsed = 0;
    Uint64 m_warmupFrames = 0;
    std::vector<FrameSample> m_samples;
};

} // namespace horde::bench
