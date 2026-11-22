#pragma once

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <filesystem>

struct stb_vorbis;

namespace horde::audio {

// Streams one looping Ogg Vorbis track to the default playback device. The
// file is decoded on SDL's audio thread a little at a time, so nothing big is
// held in memory. Switching tracks fades the old one to silence first, then
// fades the new one in from its start. Each scene that comes into view asks
// for its own track; asking for the one already playing changes nothing.
//
// Audio is optional: if no device opens (CI, headless, a broken driver) init()
// logs and returns false, and every other call becomes a no-op.
class MusicPlayer {
public:
    MusicPlayer() = default;
    ~MusicPlayer();

    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    bool init();
    void shutdown();

    // Opens `path` and fades it in. Playing the track already loaded only
    // resumes it. `trackGain` evens out loudness between recordings.
    void play(const std::filesystem::path& path, float trackGain = 1.0f);

    // Fades to silence and forgets the track; the next play() starts afresh.
    void stop();

    // 0..1, applied on top of the fades.
    void setVolume(float volume);

private:
    static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void fill(SDL_AudioStream* stream, int bytes);

    SDL_AudioStream* m_stream = nullptr;
    bool m_subsystem = false;

    struct Track {
        stb_vorbis* vorbis = nullptr;
        int channels = 0;
        float gain = 1.0f;
    };

    static void close(Track& track);

    // Guarded by the stream lock, which SDL also holds around the callback.
    // `m_pending` waits there until the current track has faded to silence.
    Track m_current;
    Track m_pending;

    std::filesystem::path m_path; // main thread: the last track asked for

    std::atomic<float> m_target{0.0f}; // fade target: 0 silent, 1 playing
    std::atomic<float> m_volume{0.6f};
    float m_gain = 0.0f; // audio thread only
};

} // namespace horde::audio
