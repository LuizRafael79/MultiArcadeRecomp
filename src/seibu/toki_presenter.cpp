#include "toki_presenter.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

namespace mamerecomp::seibu {

TokiAudioPresenter::TokiAudioPresenter(uint32_t sample_rate)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        throw std::runtime_error(std::string("SDL audio init failed: ") + SDL_GetError());
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, static_cast<int>(sample_rate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error("SDL audio open failed: " + error);
    }
    if (!SDL_ResumeAudioStreamDevice(stream_)) {
        const std::string error = SDL_GetError();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw std::runtime_error("SDL audio start failed: " + error);
    }
}

TokiAudioPresenter::~TokiAudioPresenter()
{
    if (stream_) SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void TokiAudioPresenter::queue(std::span<const int16_t> samples)
{
    if (!stream_ || samples.empty()) return;
    if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size_bytes())))
        throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
}

} // namespace mamerecomp::seibu
