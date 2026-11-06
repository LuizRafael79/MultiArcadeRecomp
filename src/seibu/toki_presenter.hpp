#pragma once

#include <cstdint>
#include <span>

struct SDL_AudioStream;

namespace mamerecomp::seibu {

class TokiAudioPresenter {
public:
    explicit TokiAudioPresenter(uint32_t sample_rate);
    ~TokiAudioPresenter();
    TokiAudioPresenter(const TokiAudioPresenter&) = delete;
    TokiAudioPresenter& operator=(const TokiAudioPresenter&) = delete;
    void queue(std::span<const int16_t> samples);
private:
    SDL_AudioStream* stream_ = nullptr;
};

} // namespace mamerecomp::seibu
