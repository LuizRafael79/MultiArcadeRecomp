#include "tmnt_presenter.hpp"

#include "tmnt_machine.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace multiarcaderecomp::konami {

struct TmntPresenter::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;

    Impl(const std::string& title, bool video, bool sound, std::uint32_t audio_sample_rate)
    {
        SDL_InitFlags flags = SDL_INIT_EVENTS;
        if (video) flags |= SDL_INIT_VIDEO;
        if (sound) flags |= SDL_INIT_AUDIO;
        if (!SDL_InitSubSystem(flags))
            throw std::runtime_error(std::string("SDL init failed: ") + SDL_GetError());
        try {
            if (video) {
                window = SDL_CreateWindow(title.c_str(), 960, 672, SDL_WINDOW_RESIZABLE);
                if (!window) throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
                // Present frames through SDL's software renderer, separately
                // from the board's emulated video rasterization.
                renderer = SDL_CreateRenderer(window, "software");
                if (!renderer) throw std::runtime_error(std::string("SDL renderer creation failed: ") + SDL_GetError());
                SDL_Log("MultiArcadeRecomp TMNT SDL video renderer: %s",
                    SDL_GetRendererName(renderer) ? SDL_GetRendererName(renderer) : "unknown");
                SDL_SetRenderVSync(renderer, 1);
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                    SDL_TEXTUREACCESS_STREAMING, 320, 224);
                if (!texture) throw std::runtime_error(std::string("SDL texture creation failed: ") + SDL_GetError());
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
            }
            if (sound) {
                const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(audio_sample_rate)};
                audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                if (!audio) throw std::runtime_error(std::string("SDL audio open failed: ") + SDL_GetError());
                if (!SDL_ResumeAudioStreamDevice(audio))
                    throw std::runtime_error(std::string("SDL audio start failed: ") + SDL_GetError());
            }
        } catch (...) {
            if (audio) SDL_DestroyAudioStream(audio);
            if (texture) SDL_DestroyTexture(texture);
            if (renderer) SDL_DestroyRenderer(renderer);
            if (window) SDL_DestroyWindow(window);
            SDL_QuitSubSystem(flags);
            throw;
        }
    }

    ~Impl()
    {
        if (audio) SDL_DestroyAudioStream(audio);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS);
    }
};

TmntPresenter::TmntPresenter(std::string title, bool video, bool audio, std::uint32_t audio_sample_rate)
    : impl_(new Impl(title, video, audio, audio_sample_rate)) {}
TmntPresenter::~TmntPresenter() { delete impl_; }

bool TmntPresenter::pump_events(TmntBoard& board)
{
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
            running = false;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto down = [&](SDL_Scancode code) { return keys && keys[code]; };
    auto player = [&](SDL_Scancode left, SDL_Scancode right, SDL_Scancode up,
                      SDL_Scancode down_key, SDL_Scancode b1,
                      SDL_Scancode b2, SDL_Scancode b3, SDL_Scancode start) {
        std::uint16_t port = 0xff00 | 0x00ff;
        const std::array<std::pair<SDL_Scancode, unsigned>, 8> bits{{
            {left,0}, {right,1}, {up,2}, {down_key,3}, {b1,4}, {b2,5}, {b3,6}, {start,7}
        }};
        for (const auto& [key, bit] : bits)
            if (down(key)) port &= static_cast<std::uint16_t>(~(1u << bit));
        return port;
    };
    std::uint16_t coins = 0xffff;
    if (down(SDL_SCANCODE_5)) coins &= static_cast<std::uint16_t>(~0x01u);
    if (down(SDL_SCANCODE_6)) coins &= static_cast<std::uint16_t>(~0x02u);
    if (down(SDL_SCANCODE_7)) coins &= static_cast<std::uint16_t>(~0x04u);
    if (down(SDL_SCANCODE_8)) coins &= static_cast<std::uint16_t>(~0x08u);
    board.set_input(0, coins);
    board.set_input(1, player(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
        SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_1));
    board.set_input(2, player(SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S,
        SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_2));
    board.set_input(3, player(SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_T, SDL_SCANCODE_G,
        SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_3));
    board.set_input(6, player(SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5,
        SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0, SDL_SCANCODE_4));
    return running;
}

void TmntPresenter::present(std::span<const std::uint8_t> rgb)
{
    if (!impl_->renderer || !impl_->texture) return;
    if (rgb.size() != 320u * 224u * 3u)
        throw std::runtime_error("TMNT renderer received an invalid RGB frame");
    if (!SDL_UpdateTexture(impl_->texture, nullptr, rgb.data(), 320 * 3))
        throw std::runtime_error(std::string("SDL texture upload failed: ") + SDL_GetError());
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, nullptr);
    SDL_RenderPresent(impl_->renderer);
}

void TmntPresenter::queue_audio(std::span<const std::int16_t> samples)
{
    if (!impl_->audio || samples.empty()) return;
    if (!SDL_PutAudioStreamData(impl_->audio, samples.data(), static_cast<int>(samples.size_bytes())))
        throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
}

} // namespace multiarcaderecomp::konami
