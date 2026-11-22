#include "audio/MusicPlayer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace horde::audio {

namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
// Seconds for a full fade in or out.
constexpr float kFadeSeconds = 1.2f;

} // namespace

MusicPlayer::~MusicPlayer() {
    shutdown();
}

bool MusicPlayer::init() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "music off: audio init failed: %s", SDL_GetError());
        return false;
    }
    m_subsystem = true;

    // One fixed output format; SDL converts to whatever the device wants.
    const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kSampleRate};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &MusicPlayer::feed, this);
    if (m_stream == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "music off: no playback device: %s", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_ResumeAudioStreamDevice(m_stream);
    return true;
}

void MusicPlayer::close(Track& track) {
    if (track.vorbis != nullptr) {
        stb_vorbis_close(track.vorbis);
    }
    track = {};
}

void MusicPlayer::shutdown() {
    if (m_stream != nullptr) {
        SDL_DestroyAudioStream(m_stream); // stops the callback before returning
        m_stream = nullptr;
    }
    close(m_current);
    close(m_pending);
    m_path.clear();
    if (m_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_subsystem = false;
    }
}

void MusicPlayer::play(const std::filesystem::path& path, float trackGain) {
    if (m_stream == nullptr) {
        return;
    }
    trackGain = std::max(trackGain, 0.0f);
    if (path == m_path) {
        // Already current or already queued: just make sure it is heard.
        SDL_LockAudioStream(m_stream);
        (m_pending.vorbis != nullptr ? m_pending : m_current).gain = trackGain;
        SDL_UnlockAudioStream(m_stream);
        m_target = 1.0f;
        return;
    }

    int error = 0;
    stb_vorbis* opened = stb_vorbis_open_filename(path.string().c_str(), &error, nullptr);
    if (opened == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "music: could not open %s (stb_vorbis error %d)", path.string().c_str(),
                     error);
        return;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(opened);
    if (static_cast<int>(info.sample_rate) != kSampleRate || info.channels < 1 || info.channels > 2) {
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "music: %s must be 44.1 kHz mono or stereo", path.string().c_str());
        stb_vorbis_close(opened);
        return;
    }

    // Queue it; the audio thread swaps it in once the current track is silent.
    Track replaced;
    SDL_LockAudioStream(m_stream);
    replaced = m_pending;
    m_pending = {opened, info.channels, trackGain};
    SDL_UnlockAudioStream(m_stream);
    close(replaced); // a queued track that never got to play
    m_path = path;
    m_target = 1.0f;
}

void MusicPlayer::stop() {
    if (m_stream == nullptr) {
        return;
    }
    Track dropped;
    SDL_LockAudioStream(m_stream);
    dropped = m_pending;
    m_pending = {};
    SDL_UnlockAudioStream(m_stream);
    close(dropped);
    m_path.clear();
    m_target = 0.0f;
}

void MusicPlayer::setVolume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
}

void SDLCALL MusicPlayer::feed(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/) {
    static_cast<MusicPlayer*>(userdata)->fill(stream, additional);
}

void MusicPlayer::fill(SDL_AudioStream* stream, int bytes) {
    const int frames = bytes / static_cast<int>(sizeof(float) * kChannels);
    if (frames <= 0) {
        return;
    }
    std::vector<float> out(static_cast<size_t>(frames) * kChannels, 0.0f);

    // The old track has faded out: start the queued one from silence.
    if (m_pending.vorbis != nullptr && (m_gain <= 0.0f || m_current.vorbis == nullptr)) {
        close(m_current);
        m_current = m_pending;
        m_pending = {};
        m_gain = 0.0f;
    }

    // While a track waits, the current one heads for silence whatever was asked.
    const float target = m_pending.vorbis != nullptr ? 0.0f : m_target.load();
    const float volume = m_volume * m_current.gain;
    const float step = 1.0f / (kFadeSeconds * kSampleRate);

    // Fully faded out: hold the position and send silence.
    if (m_current.vorbis == nullptr || (m_gain <= 0.0f && target <= 0.0f)) {
        m_gain = 0.0f;
        SDL_PutAudioStreamData(stream, out.data(), static_cast<int>(out.size() * sizeof(float)));
        return;
    }

    const int channels = m_current.channels;
    std::vector<float> decoded(static_cast<size_t>(frames) * channels);
    int filled = 0;
    while (filled < frames) {
        const int got = stb_vorbis_get_samples_float_interleaved(
            m_current.vorbis, channels, decoded.data() + static_cast<size_t>(filled) * channels,
            (frames - filled) * channels);
        if (got == 0) {
            // End of track: loop.
            if (!stb_vorbis_seek_start(m_current.vorbis)) {
                break;
            }
            continue;
        }
        filled += got;
    }

    for (int i = 0; i < filled; ++i) {
        m_gain = target > m_gain ? std::min(target, m_gain + step) : std::max(target, m_gain - step);
        const float g = m_gain * m_gain * volume; // squared: fades sound even to the ear
        const float left = decoded[static_cast<size_t>(i) * channels];
        const float right = channels == 2 ? decoded[static_cast<size_t>(i) * 2 + 1] : left;
        out[static_cast<size_t>(i) * 2] = left * g;
        out[static_cast<size_t>(i) * 2 + 1] = right * g;
    }
    SDL_PutAudioStreamData(stream, out.data(), static_cast<int>(out.size() * sizeof(float)));
}

} // namespace horde::audio
