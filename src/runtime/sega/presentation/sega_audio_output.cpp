#include "runtime/sega/presentation/sega_audio_output.hpp"

#include <stdexcept>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>
#include <unordered_map>

#ifdef MULTIARCADERECOMP_HAS_MPG123
#include <mpg123.h>
#endif

#ifdef MULTIARCADERECOMP_HAS_SDL3_AUDIO
#include <SDL3/SDL.h>
#endif

namespace multiarcaderecomp {
namespace {
constexpr double kExternalTrackGain = 0.20;
}

struct SegaAudioOutput::Impl {
    struct Track { std::vector<std::int16_t> pcm; };
    std::unordered_map<std::string, Track> tracks;
    std::string current_track;
    double source_position = 0.0;
    std::string one_shot_track;
    double one_shot_position = 0.0;
    double current_track_gain = 1.0;
#ifdef MULTIARCADERECOMP_HAS_SDL3_AUDIO
    SDL_AudioStream* stream = nullptr;
    std::uint32_t sample_rate = 0;
#endif
};

void SegaAudioOutput::load_external_tracks(const std::filesystem::path& directory) {
#ifndef MULTIARCADERECOMP_HAS_MPG123
    (void)directory;
    throw std::runtime_error("external MP3 tracks require libmpg123 development files at build time");
#else
    if (mpg123_init() != MPG123_OK) throw std::runtime_error("could not initialize libmpg123");
    std::size_t loaded_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mp3") continue;
        const auto& path = entry.path();
        mpg123_handle* decoder = mpg123_new(nullptr, nullptr);
        if (!decoder) throw std::runtime_error("could not create MP3 decoder");
        auto fail = [&](const std::string& reason) {
            mpg123_delete(decoder);
            throw std::runtime_error(reason + ": " + path.string());
        };
        mpg123_format_none(decoder);
        mpg123_format(decoder, 48000, MPG123_STEREO, MPG123_ENC_SIGNED_16);
        if (mpg123_open(decoder, path.c_str()) != MPG123_OK) fail("could not open MP3 track");
        std::array<unsigned char, 16384> chunk{};
        std::size_t done = 0;
        for (;;) {
            const int status = mpg123_read(decoder, chunk.data(), chunk.size(), &done);
            if (done) {
                auto& pcm = impl_->tracks[path.filename().string()].pcm;
                const auto old_size = pcm.size();
                pcm.resize(old_size + done / sizeof(std::int16_t));
                std::memcpy(pcm.data() + old_size, chunk.data(), done);
            }
            if (status == MPG123_DONE) break;
            if (status != MPG123_OK && status != MPG123_NEW_FORMAT) fail("failed decoding MP3 track");
        }
        mpg123_close(decoder);
        mpg123_delete(decoder);
        const auto& pcm = impl_->tracks[path.filename().string()].pcm;
        if (pcm.size() < 4)
            throw std::runtime_error("MP3 track contains no decodable audio: " + path.string());
        ++loaded_count;
        std::cout << "Loaded external audio track " << path.filename().string() << " ("
                  << pcm.size() / 2 / 48000 << " s)\n";
    }
    if (loaded_count == 0) throw std::runtime_error("no MP3 tracks found in audio directory: " + directory.string());
#endif
}

bool SegaAudioOutput::play_track(const char* track_id) {
    if (!track_id) return false;
    const auto found = impl_->tracks.find(track_id);
    if (found == impl_->tracks.end() || found->second.pcm.empty()) return false;
    if (impl_->current_track != track_id) {
        impl_->current_track = track_id;
        impl_->source_position = 0.0;
        impl_->current_track_gain = 1.0;
    }
    return true;
}

bool SegaAudioOutput::play_one_shot(const char* track_id) {
    if (!track_id) return false;
    const auto found = impl_->tracks.find(track_id);
    if (found == impl_->tracks.end() || found->second.pcm.empty()) return false;
    impl_->one_shot_track = track_id;
    impl_->one_shot_position = 0.0;
    return true;
}

void SegaAudioOutput::stop_track() {
    impl_->current_track.clear();
    impl_->source_position = 0.0;
}

void SegaAudioOutput::set_track_gain(float gain) {
    impl_->current_track_gain = std::clamp(static_cast<double>(gain), 0.0, 1.0);
}

SegaAudioOutput::SegaAudioOutput() : impl_(std::make_unique<Impl>()) { }

SegaAudioOutput::~SegaAudioOutput() {
#ifdef MULTIARCADERECOMP_HAS_SDL3_AUDIO
    if (impl_->stream) SDL_DestroyAudioStream(impl_->stream);
    if (impl_->sample_rate != 0) SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif
}

void SegaAudioOutput::submit(std::span<const std::int16_t> samples, std::uint32_t sample_rate) {
    if (samples.empty()) return;
#ifdef MULTIARCADERECOMP_HAS_SDL3_AUDIO
    if (sample_rate == 0) throw std::invalid_argument("Sega audio output requires a nonzero sample rate");
    std::vector<std::int16_t> mixed_samples(samples.begin(), samples.end());
    const std::size_t frames = samples.size() / 2;
    const double step = 48000.0 / static_cast<double>(sample_rate);
    static const std::vector<std::int16_t> empty_pcm;
    const auto pcm_for = [&](const std::string& track_id) -> const std::vector<std::int16_t>& {
        const auto found = impl_->tracks.find(track_id);
        return found == impl_->tracks.end() ? empty_pcm : found->second.pcm;
    };
    const auto& bgm = pcm_for(impl_->current_track);
    const auto bgm_frames = impl_->current_track.empty() ? 0 : bgm.size() / 2;
    const auto& one_shot = pcm_for(impl_->one_shot_track);
    const auto one_shot_frames = one_shot.size() / 2;
    const auto interpolated_sample = [](const std::vector<std::int16_t>& pcm, std::size_t source_frames,
        double position, std::size_t channel, bool loop) -> double {
        if (source_frames == 0 || position >= static_cast<double>(source_frames)) return 0.0;
        const auto index = static_cast<std::size_t>(position);
        const auto next = loop ? (index + 1) % source_frames : std::min(index + 1, source_frames - 1);
        const double fraction = position - std::floor(position);
        const double a = pcm[index * 2 + channel];
        const double b = pcm[next * 2 + channel];
        return a + (b - a) * fraction;
    };
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            double mixed = mixed_samples[frame * 2 + channel];
            if (!impl_->current_track.empty() && bgm_frames != 0) {
                mixed += interpolated_sample(bgm, bgm_frames, impl_->source_position, channel, true)
                    * kExternalTrackGain * impl_->current_track_gain;
            }
            if (!impl_->one_shot_track.empty() && one_shot_frames != 0) {
                mixed += interpolated_sample(one_shot, one_shot_frames,
                    impl_->one_shot_position, channel, false) * kExternalTrackGain;
            }
            mixed_samples[frame * 2 + channel] = static_cast<std::int16_t>(
                std::clamp(static_cast<int>(mixed), -32768, 32767));
        }
        if (!impl_->current_track.empty() && bgm_frames != 0) {
            impl_->source_position += step;
            if (impl_->source_position >= static_cast<double>(bgm_frames))
                impl_->source_position = std::fmod(impl_->source_position, static_cast<double>(bgm_frames));
        }
        if (!impl_->one_shot_track.empty() && one_shot_frames != 0) {
            impl_->one_shot_position += step;
            if (impl_->one_shot_position >= static_cast<double>(one_shot_frames)) {
                impl_->one_shot_position = static_cast<double>(one_shot_frames);
                impl_->one_shot_track.clear();
            }
        }
    }
    if (impl_->stream && impl_->sample_rate != sample_rate) {
        SDL_DestroyAudioStream(impl_->stream);
        impl_->stream = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        impl_->sample_rate = 0;
    }
    if (!impl_->stream) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL audio initialization failed: ") + SDL_GetError());
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(sample_rate);
        impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!impl_->stream) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw std::runtime_error(std::string("could not open SDL playback stream: ") + SDL_GetError());
        }
        if (!SDL_ResumeAudioStreamDevice(impl_->stream)) {
            const std::string error = SDL_GetError();
            SDL_DestroyAudioStream(impl_->stream);
            impl_->stream = nullptr;
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw std::runtime_error("could not start SDL playback stream: " + error);
        }
        impl_->sample_rate = sample_rate;
    }
    const auto byte_count = static_cast<int>(mixed_samples.size() * sizeof(std::int16_t));
    if (!SDL_PutAudioStreamData(impl_->stream, mixed_samples.data(), byte_count))
        throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
#else
    (void)sample_rate;
    throw std::runtime_error("audio playback requires an SDL3-enabled build");
#endif
}

} // namespace multiarcaderecomp
