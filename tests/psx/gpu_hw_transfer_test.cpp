#include "gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t kSkip = 77;

void gp0(mamerecomp::psx::Gpu& gpu, uint32_t value)
{
    gpu.write_data(value);
}

bool read_rect(mamerecomp::psx::Gpu& gpu, unsigned x, unsigned y,
               unsigned width, unsigned height,
               const std::vector<uint16_t>& expected)
{
    gp0(gpu, 0xc0000000);
    gp0(gpu, (y << 16) | x);
    gp0(gpu, (height << 16) | width);
    for (size_t pixel = 0; pixel < expected.size(); pixel += 2) {
        const uint32_t word = gpu.read_data();
        const uint16_t low = static_cast<uint16_t>(word);
        const uint16_t high = static_cast<uint16_t>(word >> 16);
        if (low != expected[pixel] ||
            (pixel + 1 < expected.size() && high != expected[pixel + 1]) ||
            (pixel + 1 == expected.size() && high != 0)) {
            std::cerr << "GPUREAD mismatch at pixel " << pixel << ": got 0x"
                      << std::hex << word << std::dec << '\n';
            return false;
        }
    }
    return true;
}
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "Skipping OpenGL transfer test: " << SDL_GetError() << '\n';
        return kSkip;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("PSX GPU transfer test", 32, 32,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "Skipping OpenGL transfer test: " << SDL_GetError() << '\n';
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::cerr << "Skipping OpenGL transfer test: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    if (!SDL_GL_MakeCurrent(window, context)) {
        std::cerr << "Skipping OpenGL transfer test: " << SDL_GetError() << '\n';
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer) std::cout << "PSX GPU transfer test renderer: " << renderer << '\n';

    bool passed = false;
    try {
        std::vector<uint8_t> vram(1024 * 512 * 2);
        mamerecomp::psx::Gpu gpu(vram);
        gpu.enable_hardware_renderer(window, context);

        // Odd-width A0 upload wraps at both VRAM edges. GP0(80) then copies
        // from those uploaded texels into another wrapped rectangle; C0 forces
        // the OpenGL pixels back into the CPU-visible path.
        constexpr std::array<uint16_t, 9> uploaded{
            0x1234, 0x2345, 0x3456,
            0x4567, 0x5678, 0x6789,
            0x789a, 0x89ab, 0x9abc,
        };
        gp0(gpu, 0xa0000000);
        gp0(gpu, (511u << 16) | 1022u);
        gp0(gpu, (3u << 16) | 3u);
        for (size_t i = 0; i < uploaded.size(); i += 2) {
            const uint32_t word = uploaded[i] |
                (i + 1 < uploaded.size() ? uint32_t(uploaded[i + 1]) << 16 : 0);
            gp0(gpu, word);
        }
        gp0(gpu, 0x80000000);
        gp0(gpu, (511u << 16) | 1022u);
        gp0(gpu, (511u << 16) | 1008u);
        gp0(gpu, (3u << 16) | 3u);
        if (!read_rect(gpu, 1008, 511, 3, 3,
                       std::vector<uint16_t>(uploaded.begin(), uploaded.end())))
            throw std::runtime_error("A0/80/C0 wrapped transfer check failed");

        // A GPU fill crosses X=1023 and Y=511. C0 must observe every pixel
        // after the renderer synchronizes that wrapped rectangle to the CPU.
        gp0(gpu, 0x0200ff00);
        gp0(gpu, (511u << 16) | 1008u);
        gp0(gpu, (2u << 16) | 32u);
        if (!read_rect(gpu, 1008, 511, 32, 2,
                       std::vector<uint16_t>(64, 0x03e0)))
            throw std::runtime_error("GPU fill/readback wrap check failed");
        passed = true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return passed ? 0 : 1;
}
