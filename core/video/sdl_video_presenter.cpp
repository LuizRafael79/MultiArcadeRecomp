#include "sdl_video_presenter.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace mamerecomp::video {

SdlVideoPresenter::SdlVideoPresenter(std::string title, unsigned width, unsigned height,
                                   unsigned display_aspect_width,
                                   unsigned display_aspect_height,
                                   unsigned window_width,
                                   unsigned window_height)
    : width_(width), height_(height),
      display_aspect_width_(display_aspect_width),
      display_aspect_height_(display_aspect_height)
{
    if (!width_ || !height_) throw std::runtime_error("SDL framebuffer dimensions must be positive");
    if ((display_aspect_width_ == 0) != (display_aspect_height_ == 0))
        throw std::runtime_error("SDL display aspect requires both width and height");
    if ((window_width == 0) != (window_height == 0))
        throw std::runtime_error("SDL window dimensions require both width and height");
#if defined(__linux__)
    // Prefer GNOME's themed XWayland frame when both Wayland and XWayland are
    // available. Keep an explicit SDL driver choice authoritative.
    if (std::getenv("WAYLAND_DISPLAY") && std::getenv("DISPLAY") &&
        !std::getenv("SDL_VIDEODRIVER"))
        setenv("SDL_VIDEODRIVER", "x11", 0);
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        throw std::runtime_error(std::string("SDL video init failed: ") + SDL_GetError());
    window_ = SDL_CreateWindow(title.c_str(),
        static_cast<int>(window_width ? window_width : width_ * 3),
        static_cast<int>(window_height ? window_height : height_ * 3),
        SDL_WINDOW_RESIZABLE);
    if (!window_) {
        const std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        throw std::runtime_error("SDL window creation failed: " + error);
    }
    // The emulated GPU still rasterizes into host-side VRAM in software. Only
    // the final framebuffer upload, scaling, and window presentation use OpenGL.
    renderer_ = SDL_CreateRenderer(window_, "opengl");
    if (!renderer_) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        throw std::runtime_error("SDL renderer creation failed: " + error);
    }
    const char* renderer_name = SDL_GetRendererName(renderer_);
    SDL_Log("MameRecomp SDL video renderer: %s",
            renderer_name ? renderer_name : "unknown");
    if (!SDL_SetRenderVSync(renderer_, 1)) {
        const std::string error = SDL_GetError();
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        throw std::runtime_error("SDL VSync setup failed: " + error);
    }
    try {
        resize_texture(width_, height_);
    } catch (...) {
        SDL_DestroyRenderer(renderer_);
        SDL_DestroyWindow(window_);
        renderer_ = nullptr;
        window_ = nullptr;
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        throw;
    }
}

SdlVideoPresenter::~SdlVideoPresenter()
{
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

bool SdlVideoPresenter::pump_events()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
            return false;
    }
    return true;
}

SdlArcadeInput SdlVideoPresenter::input_state() const
{
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto down = [&](SDL_Scancode code) { return keys && keys[code]; };
    SdlArcadeInput input;
    if (down(SDL_SCANCODE_5)) input.coins_pressed |= 1U << 0;
    if (down(SDL_SCANCODE_6)) input.coins_pressed |= 1U << 1;
    if (down(SDL_SCANCODE_7)) input.coins_pressed |= 1U << 2;
    if (down(SDL_SCANCODE_8)) input.coins_pressed |= 1U << 3;
    input.service_pressed = down(SDL_SCANCODE_F2);
    const auto set_player = [&](unsigned player, SDL_Scancode left, SDL_Scancode right,
                                SDL_Scancode up, SDL_Scancode down_key,
                                SDL_Scancode button1, SDL_Scancode button2,
                                SDL_Scancode button3, SDL_Scancode start) {
        auto& bits = input.players_pressed[player];
        if (down(left)) bits |= SdlArcadeInput::left;
        if (down(right)) bits |= SdlArcadeInput::right;
        if (down(up)) bits |= SdlArcadeInput::up;
        if (down(down_key)) bits |= SdlArcadeInput::down;
        if (down(button1)) bits |= SdlArcadeInput::button1;
        if (down(button2)) bits |= SdlArcadeInput::button2;
        if (down(button3)) bits |= SdlArcadeInput::button3;
        if (down(start)) bits |= SdlArcadeInput::start;
    };
    set_player(0, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
        SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_1);
    set_player(1, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S,
        SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_2);
    set_player(2, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_T, SDL_SCANCODE_G,
        SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I, SDL_SCANCODE_3);
    set_player(3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_5,
        SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0, SDL_SCANCODE_4);
    if (down(SDL_SCANCODE_Q)) input.players_pressed[0] |= SdlArcadeInput::button4;
    if (down(SDL_SCANCODE_E)) input.players_pressed[0] |= SdlArcadeInput::button5;
    if (down(SDL_SCANCODE_R)) input.players_pressed[0] |= SdlArcadeInput::button6;
    if (down(SDL_SCANCODE_U)) input.players_pressed[1] |= SdlArcadeInput::button4;
    if (down(SDL_SCANCODE_O)) input.players_pressed[1] |= SdlArcadeInput::button5;
    if (down(SDL_SCANCODE_P)) input.players_pressed[1] |= SdlArcadeInput::button6;
    return input;
}

void SdlVideoPresenter::present(std::span<const std::uint8_t> rgb)
{
    present({width_, height_, static_cast<std::size_t>(width_) * 3, rgb});
}

void SdlVideoPresenter::present(mamerecomp::runtime::Rgb24FrameView frame)
{
    const std::size_t row_bytes = static_cast<std::size_t>(frame.width) * 3;
    const std::size_t expected = frame.pitch * frame.height;
    if (!frame.width || !frame.height || frame.pitch < row_bytes ||
        frame.pixels.size() < expected)
        throw std::runtime_error("SDL presenter received an invalid RGB24 frame view");
    resize_texture(frame.width, frame.height);
    if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels.data(), static_cast<int>(frame.pitch)))
        throw std::runtime_error(std::string("SDL framebuffer upload failed: ") + SDL_GetError());
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (display_aspect_width_ && display_aspect_height_) {
        int output_width = 0;
        int output_height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer_, &output_width, &output_height))
            throw std::runtime_error(std::string("SDL render output size query failed: ") + SDL_GetError());
        int destination_width = output_width;
        int destination_height = output_height;
        if (static_cast<std::uint64_t>(output_width) * display_aspect_height_ >
            static_cast<std::uint64_t>(output_height) * display_aspect_width_) {
            destination_width = static_cast<int>(
                static_cast<std::uint64_t>(output_height) * display_aspect_width_ /
                display_aspect_height_);
        } else {
            destination_height = static_cast<int>(
                static_cast<std::uint64_t>(output_width) * display_aspect_height_ /
                display_aspect_width_);
        }
        const SDL_FRect destination{
            static_cast<float>((output_width - destination_width) / 2),
            static_cast<float>((output_height - destination_height) / 2),
            static_cast<float>(destination_width),
            static_cast<float>(destination_height)};
        SDL_RenderTexture(renderer_, texture_, nullptr, &destination);
    } else {
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    }
    SDL_RenderPresent(renderer_);
}

void SdlVideoPresenter::resize_texture(unsigned width, unsigned height)
{
    if (width == width_ && height == height_ && texture_) return;
    SDL_Texture* replacement = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING, static_cast<int>(width), static_cast<int>(height));
    if (!replacement)
        throw std::runtime_error(std::string("SDL framebuffer texture creation failed: ") + SDL_GetError());
    if (!SDL_SetTextureScaleMode(replacement, SDL_SCALEMODE_NEAREST)) {
        const std::string error = SDL_GetError();
        SDL_DestroyTexture(replacement);
        throw std::runtime_error("SDL framebuffer scale mode setup failed: " + error);
    }
    if (texture_) SDL_DestroyTexture(texture_);
    texture_ = replacement;
    width_ = width;
    height_ = height;
}

} // namespace mamerecomp::video
