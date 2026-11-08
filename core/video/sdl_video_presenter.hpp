#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <string>

#include "../runtime/machine_io.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace mamerecomp::video {

using SdlArcadeInput = mamerecomp::runtime::ArcadeInput;

// Shared OpenGL-backed framebuffer presenter for board modules with an RGB24 frame.
class SdlVideoPresenter {
public:
    SdlVideoPresenter(std::string title, unsigned width, unsigned height,
                      unsigned display_aspect_width = 0,
                      unsigned display_aspect_height = 0,
                      unsigned window_width = 0,
                      unsigned window_height = 0,
                      bool native_opengl_window = false);
    ~SdlVideoPresenter();
    SdlVideoPresenter(const SdlVideoPresenter&) = delete;
    SdlVideoPresenter& operator=(const SdlVideoPresenter&) = delete;

    bool pump_events();
    SdlArcadeInput input_state() const;
    void present(std::span<const std::uint8_t> rgb);
    void present(mamerecomp::runtime::Rgb24FrameView frame);
    void* native_window() const noexcept { return window_; }
    void* native_gl_context() const noexcept { return gl_context_; }
    bool has_native_gl_context() const noexcept { return gl_context_ != nullptr; }

private:
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    unsigned width_ = 0;
    unsigned height_ = 0;
    unsigned display_aspect_width_ = 0;
    unsigned display_aspect_height_ = 0;
    bool native_opengl_window_ = false;

    void resize_texture(unsigned width, unsigned height);
};

} // namespace mamerecomp::video
