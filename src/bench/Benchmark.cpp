#include "bench/Benchmark.hpp"

#include <SDL3/SDL_log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <numeric>

namespace horde::bench {

namespace {

constexpr double kNsPerMs = 1.0e6;

// Summary statistics of one timing column, in milliseconds.
nlohmann::json summarise(std::vector<double> ms) {
    if (ms.empty()) {
        return nlohmann::json::object();
    }

    std::ranges::sort(ms);
    const auto percentile = [&](double p) {
        const double rank = p * static_cast<double>(ms.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(rank));
        const std::size_t hi = std::min(lo + 1, ms.size() - 1);
        return ms[lo] + (ms[hi] - ms[lo]) * (rank - static_cast<double>(lo));
    };

    const double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
    double variance = 0.0;
    for (const double v : ms) {
        variance += (v - mean) * (v - mean);
    }
    variance /= static_cast<double>(ms.size());

    return {
        {"mean", mean},
        {"stddev", std::sqrt(variance)},
        {"min", ms.front()},
        {"p50", percentile(0.50)},
        {"p95", percentile(0.95)},
        {"p99", percentile(0.99)},
        {"max", ms.back()},
    };
}

std::vector<double> column(const std::vector<FrameSample>& samples, Uint64 FrameSample::* field) {
    std::vector<double> out;
    out.reserve(samples.size());
    for (const FrameSample& sample : samples) {
        out.push_back(static_cast<double>(sample.*field) / kNsPerMs);
    }
    return out;
}

std::string timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

} // namespace

bool Recorder::record(const FrameSample& sample) {
    m_elapsed += sample.frame;

    const auto warmupNs = static_cast<Uint64>(m_config.warmupSeconds * 1.0e9);
    if (m_elapsed <= warmupNs) {
        ++m_warmupFrames;
        return true;
    }

    m_samples.push_back(sample);

    const auto totalNs = static_cast<Uint64>((m_config.warmupSeconds + m_config.durationSeconds) * 1.0e9);
    return m_elapsed < totalNs;
}

bool Recorder::finish(const RunInfo& info) const {
    const std::vector<double> frameMs = column(m_samples, &FrameSample::frame);
    const double totalMs = std::accumulate(frameMs.begin(), frameMs.end(), 0.0);

    // "1% low": the average fps over the slowest 1% of frames.
    std::vector<double> slowest = frameMs;
    std::ranges::sort(slowest, std::greater<>());
    const std::size_t slowCount = std::max<std::size_t>(1, slowest.size() / 100);
    double slowMs = 0.0;
    for (std::size_t i = 0; i < std::min(slowCount, slowest.size()); ++i) {
        slowMs += slowest[i];
    }

    const scene::LevelOptions& level = m_config.level;

    nlohmann::json result = {
        {"status", m_samples.empty() ? "no-samples" : "ok"},
        {"timestamp", timestamp()},
        {"physics", level.physics},
        {"units", level.unitCount},
        {"spawn", sim::spawnPatternName(level.spawn)},
        {"seed", level.seed},
        {"level", level.levelPath.empty() ? "default" : level.levelPath.generic_string()},
        {"warmup_s", m_config.warmupSeconds},
        {"duration_s", m_config.durationSeconds},
        {"fixed_dt", m_config.fixedDt},
#ifdef NDEBUG
        {"build", "release"},
#else
        {"build", "debug"},
#endif
        {"video_driver", info.videoDriver},
        {"gpu_driver", info.gpuDriver},
        {"shader_format", info.shaderFormat},
        {"present_mode", info.presentMode},
        {"window", {info.windowWidth, info.windowHeight}},
        {"warmup_frames", m_warmupFrames},
        {"frames", m_samples.size()},
        {"fps_mean", totalMs > 0.0 ? 1000.0 * static_cast<double>(m_samples.size()) / totalMs : 0.0},
        {"fps_1pct_low", slowMs > 0.0 ? 1000.0 * static_cast<double>(slowCount) / slowMs : 0.0},
        {"frame_ms", summarise(frameMs)},
        {"update_ms", summarise(column(m_samples, &FrameSample::update))},
        {"render_ms", summarise(column(m_samples, &FrameSample::render))},
        {"acquire_ms", summarise(column(m_samples, &FrameSample::acquire))},
        {"gpu_sync", m_config.syncGpu},
        {"collision_iterations", level.collision.iterations},
        {"collision_relaxation", level.collision.relaxation},
        {"max_speed", level.maxSpeed},
    };
    if (info.quality.measured) {
        const CollisionQuality& q = info.quality;
        result["quality"] = {
            {"overlapping_pairs", q.overlappingPairs},
            {"overlapping_units", q.overlappingUnits},
            {"overlapping_fraction", level.unitCount ? static_cast<double>(q.overlappingUnits) / level.unitCount : 0.0},
            {"mean_penetration", q.meanPenetration},
            {"max_penetration", q.maxPenetration},
            {"density", q.density},
        };
    }
    if (m_config.syncGpu) {
        result["sim_gpu_ms"] = summarise(column(m_samples, &FrameSample::gpu));
    }

    const std::string text = result.dump(2);
    std::printf("HORDE_BENCH_RESULT %s\n", result.dump().c_str());
    std::fflush(stdout);

    bool ok = true;

    if (!m_config.outPath.empty()) {
        std::error_code ec;
        if (m_config.outPath.has_parent_path()) {
            std::filesystem::create_directories(m_config.outPath.parent_path(), ec);
        }
        std::ofstream out(m_config.outPath);
        out << text << '\n';
        if (!out) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s", m_config.outPath.string().c_str());
            ok = false;
        }
    }

    if (!m_config.framesPath.empty()) {
        std::error_code ec;
        if (m_config.framesPath.has_parent_path()) {
            std::filesystem::create_directories(m_config.framesPath.parent_path(), ec);
        }
        std::ofstream out(m_config.framesPath);
        out << "frame,frame_ms,update_ms,render_ms\n";
        for (std::size_t i = 0; i < m_samples.size(); ++i) {
            const FrameSample& s = m_samples[i];
            out << i << ',' << static_cast<double>(s.frame) / kNsPerMs << ','
                << static_cast<double>(s.update) / kNsPerMs << ',' << static_cast<double>(s.render) / kNsPerMs
                << '\n';
        }
        if (!out) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not write %s", m_config.framesPath.string().c_str());
            ok = false;
        }
    }

    return ok;
}

} // namespace horde::bench
