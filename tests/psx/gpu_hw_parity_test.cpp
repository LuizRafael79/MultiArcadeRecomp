#include "gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int kSkip = 77;
constexpr uint32_t kReadbackWidth = 1024;
constexpr uint32_t kReadbackHeight = 512;
constexpr size_t kReadbackWords = size_t(kReadbackWidth) * kReadbackHeight / 2;

void send(mamerecomp::psx::Gpu& gpu, const std::vector<uint32_t>& commands)
{
    for (const uint32_t command : commands) gpu.write_data(command);
}

std::vector<uint16_t> read_vram(mamerecomp::psx::Gpu& gpu)
{
    gpu.write_data(0xc0000000u);
    gpu.write_data(0u);
    gpu.write_data((kReadbackHeight << 16) | kReadbackWidth);
    std::vector<uint16_t> pixels(kReadbackWidth * kReadbackHeight);
    for (size_t word = 0; word < kReadbackWords; ++word) {
        const uint32_t packed = gpu.read_data();
        pixels[word * 2] = static_cast<uint16_t>(packed);
        pixels[word * 2 + 1] = static_cast<uint16_t>(packed >> 16);
    }
    return pixels;
}
}

int main()
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::cerr << "Skipping OpenGL parity test: " << SDL_GetError() << '\n';
        return kSkip;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("PSX GPU parity test", 32, 32,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "Skipping OpenGL parity test: " << SDL_GetError() << '\n';
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context || !SDL_GL_MakeCurrent(window, context)) {
        std::cerr << "Skipping OpenGL parity test: " << SDL_GetError() << '\n';
        if (context) SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer) std::cout << "PSX GPU parity test renderer: " << renderer << '\n';

    bool passed = false;
    try {
        using mamerecomp::psx::Gpu;
        std::vector<uint8_t> software_vram(kReadbackWidth * kReadbackHeight * 2);
        std::vector<uint8_t> hardware_vram(software_vram.size());
        Gpu software(software_vram, 2);
        Gpu hardware(hardware_vram, 2);
        software.set_frame_capture_enabled(false);
        hardware.set_frame_capture_enabled(false);
        hardware.enable_hardware_renderer(window, context);

        const std::vector<uint32_t> commands{
            // Seed a 15bpp texture and draw it through the sprite path.
            0xa0000000u, 0u, (2u << 16) | 2u,
            0x7fff7fffu, 0x7fff7fffu,
            0xe1000100u,
            0x65000000u, (10u << 16) | 10u, 0u, (2u << 16) | 2u,
            // GP0 VRAM copy, including overlapping source/destination.
            0x80000000u, (10u << 16) | 10u, (11u << 16) | 11u,
            (2u << 16) | 2u,
            // Gouraud triangle and quad use the integer scanline path.
            0x300000ffu, 20u | (20u << 16), 0x0000ff00u,
            40u | (20u << 16), 0x00ff0000u, 20u | (40u << 16),
            0x380000ffu, 50u | (20u << 16), 0x0000ff00u,
            70u | (20u << 16), 0x00ff0000u, 50u | (40u << 16),
            0x0000ffffu, 70u | (40u << 16),
            // Flat and Gouraud line cases, with reversed and clipped edges.
            0x400000ffu, 8u | (80u << 16), 72u | (80u << 16),
            0x500000ffu, 72u | (84u << 16), 0x00ff0000u,
            8u | (120u << 16),
            // Seed a 4bpp page and CLUT, then exercise indexed texture lookup.
            0xa0000000u, 0u, (1u << 16) | 4u, 0x4321u, 0u,
            0xa0000000u, 64u, (1u << 16) | 16u,
            0x001f0000u, 0x000003e0u, 0u, 0u, 0u, 0u, 0u, 0x7c000000u,
            0xe1000000u,
            0x65000000u, 90u | (90u << 16), 0x00040000u,
            (1u << 16) | 4u,
            // Texture-window origin changes the packed source word for 4bpp.
            0xa0000000u, 2u, (1u << 16) | 1u, 1u,
            0xe2000400u,
            0x65000000u, 80u | (80u << 16), 0x00040000u,
            (1u << 16) | 1u,
            0xe2000000u,
            // 8bpp uses two indices per source word and the same CLUT lookup.
            0xe1000080u,
            0xa0000000u, 10u, (1u << 16) | 1u, 0x0201u,
            0x65000000u, 110u | (90u << 16), 0x00040014u,
            (1u << 16) | 2u,
            // Gouraud modulation over a raw 15bpp source texture.
            0xe1000100u,
            0x34808080u, 150u | (100u << 16), 0u,
            0x0000ffffu, 158u | (100u << 16), 0x01000000u,
            0x00ff0080u, 150u | (108u << 16), 0u,
            // Textured triangle with UVs and the CLUT encoded in the packet.
            0x25000000u, 100u | (100u << 16), 0x00040000u,
            108u | (100u << 16), 0u,
            100u | (108u << 16), 3u | (1u << 8),
            // Mask set/check and semitransparent rectangle overlap.
            0xe6000001u,
            0x600000ffu, 120u | (120u << 16), (8u << 16) | 8u,
            0xe6000003u,
            0x620000ffu, 124u | (124u << 16), 0x00040000u,
            (8u << 16) | 8u,
            // Draw-area clipping, then restore a full-VRAM drawing region.
            0xe3000000u | (60u << 10) | 60u,
            0xe4000000u | (65u << 10) | 65u,
            0x600000ffu, 60u | (60u << 16), (12u << 16) | 12u,
            0xe3000000u, 0xe4000000u | 0x0007ffffu,
            // Fill at the lower/right VRAM boundary.
            0x020000ffu, (1020u << 16) | 1020u, (8u << 16) | 8u,
        };
        send(software, commands);
        send(hardware, commands);

        const auto expected = read_vram(software);
        const auto actual = read_vram(hardware);
        size_t mismatch_count = 0;
        size_t first_mismatch = expected.size();
        for (size_t i = 0; i < expected.size(); ++i) {
            if (expected[i] == actual[i]) continue;
            if (first_mismatch == expected.size()) first_mismatch = i;
            ++mismatch_count;
        }
        if (mismatch_count) {
            std::cerr << "PSX software/OpenGL VRAM mismatch: " << mismatch_count
                      << " pixels, first at (" << first_mismatch % kReadbackWidth
                      << ',' << first_mismatch / kReadbackWidth << "): software=0x"
                      << std::hex << expected[first_mismatch] << " hardware=0x"
                      << actual[first_mismatch] << std::dec << '\n';
            throw std::runtime_error("PSX GPU primitive parity check failed");
        }
        passed = true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return passed ? 0 : 1;
}
