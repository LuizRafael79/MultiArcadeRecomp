#pragma once

#include <cstdint>
#include <memory>
#include <filesystem>
#include <span>

namespace mamerecomp {

class SegaAudioOutput {
public:
    SegaAudioOutput();
    ~SegaAudioOutput();
    SegaAudioOutput(const SegaAudioOutput&) = delete;
    SegaAudioOutput& operator=(const SegaAudioOutput&) = delete;

    void submit(std::span<const std::int16_t> interleaved_stereo, std::uint32_t sample_rate);
    void load_external_tracks(const std::filesystem::path& directory);
    bool play_track(const char* track_id);
    bool play_one_shot(const char* track_id);
    void stop_track();
    void set_track_gain(float gain);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mamerecomp
