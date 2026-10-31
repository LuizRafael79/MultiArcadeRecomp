#include "naomi_presenter.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace mamerecomp::naomi {

struct NaomiPvrPresenter::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    explicit Impl(const std::string& title) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            throw std::runtime_error(std::string("SDL video init failed: ") + SDL_GetError());
        window = SDL_CreateWindow(title.c_str(), 960, 720, SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError());
        }
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(std::string("SDL renderer creation failed: ") + SDL_GetError());
        }
    }

    ~Impl() {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

NaomiPvrPresenter::NaomiPvrPresenter(const std::string& title) : impl_(new Impl(title)) {}
NaomiPvrPresenter::~NaomiPvrPresenter() { delete impl_; }

void NaomiPvrPresenter::show(const NaomiPvrFrameInfo& info,
                             std::span<const std::uint8_t> rgb) {
    if (info.width == 0 || info.height == 0 || info.rgb_bytes != rgb.size() ||
        rgb.size() != static_cast<std::size_t>(info.width) * info.height * 3u)
        throw std::runtime_error("PVR framebuffer readback has invalid dimensions or byte count");
    if (impl_->texture) SDL_DestroyTexture(impl_->texture);
    impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGB24,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       static_cast<int>(info.width),
                                       static_cast<int>(info.height));
    if (!impl_->texture)
        throw std::runtime_error(std::string("SDL PVR texture creation failed: ") + SDL_GetError());
    if (!SDL_UpdateTexture(impl_->texture, nullptr, rgb.data(),
                           static_cast<int>(info.width * 3u)))
        throw std::runtime_error(std::string("SDL PVR texture upload failed: ") + SDL_GetError());

    const bool enabled = (info.control & 1u) != 0;
    const auto depth = (info.control >> 2u) & 3u;
    char title[192]{};
    std::snprintf(title, sizeof(title),
        "PVR2 FB: %ux%u | ctrl=%08x enabled=%s depth=%u SOF1=%08x | raw VRAM (Esc closes)",
        info.width, info.height, info.control, enabled ? "yes" : "no", depth, info.start);
    SDL_SetWindowTitle(impl_->window, title);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
                running = false;
        }
        if (!running) break;
        SDL_SetRenderDrawColor(impl_->renderer, 8, 12, 20, 255);
        SDL_RenderClear(impl_->renderer);
        SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, nullptr);
        SDL_RenderPresent(impl_->renderer);
        SDL_Delay(10);
    }
}

} // namespace mamerecomp::naomi
