#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace mamerecomp::konami {
class TmntBoard;

class TmntPresenter {
public:
    TmntPresenter(std::string title, bool video, bool audio, std::uint32_t audio_sample_rate);
    ~TmntPresenter();
    TmntPresenter(const TmntPresenter&) = delete;
    TmntPresenter& operator=(const TmntPresenter&) = delete;

    bool pump_events(TmntBoard& board);
    void present(std::span<const std::uint8_t> rgb);
    void queue_audio(std::span<const std::int16_t> samples);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
} // namespace mamerecomp::konami
