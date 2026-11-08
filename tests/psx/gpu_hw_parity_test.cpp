#include "gpu.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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
            // E1 dithering on: a constant Gouraud triangle verifies both
            // backend parity and a known 4x4-matrix quantization result.
            0xe1000200u,
            0x30808080u, 300u | (200u << 16), 0x00808080u,
            308u | (200u << 16), 0x00808080u,
            300u | (208u << 16),
            // Lines use the compute rasterizer and follow E1 dithering, while
            // solid rectangles remain undithered even with E1 bit 9 enabled.
            0x40808080u, 300u | (210u << 16), 308u | (210u << 16),
            0x60808080u, 320u | (210u << 16), (1u << 16) | 1u,
            // Textured triangle with UVs and the CLUT encoded in the packet.
            0x25000000u, 100u | (100u << 16), 0x00040000u,
            108u | (100u << 16), 0u,
            100u | (108u << 16), 3u | (1u << 8),
            // A textured quad whose first triangle is rejected by the PSX
            // coordinate-wrap cull rule; the surviving half must not walk
            // through vertex 0 while interpolating its scanlines.
            0x2d000000u, 0x00140400u, 0u,
            150u | (20u << 16), 0x01000000u,
            0u, 0u,
            10u | (20u << 16), 1u,
            // Exercise all four semi-transparency equations with mask off.
            0xe6000000u,
            0xe1000000u, 0x600000f8u, 200u | (120u << 16), (1u << 16) | 1u,
            0x62f80000u, 200u | (120u << 16), (1u << 16) | 1u,
            0xe1000020u, 0x600000f8u, 202u | (120u << 16), (1u << 16) | 1u,
            0x62f80000u, 202u | (120u << 16), (1u << 16) | 1u,
            0xe1000040u, 0x600000f8u, 204u | (120u << 16), (1u << 16) | 1u,
            0x62f80000u, 204u | (120u << 16), (1u << 16) | 1u,
            0xe1000060u, 0x600000f8u, 206u | (120u << 16), (1u << 16) | 1u,
            0x62f80000u, 206u | (120u << 16), (1u << 16) | 1u,
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
        constexpr size_t dither_pixel = 202u * kReadbackWidth + 302u;
        if (expected[dither_pixel] != 0x3defu)
            throw std::runtime_error("PSX dithering fixture did not match its expected RGB555 value");
        constexpr size_t dither_line_pixel = 210u * kReadbackWidth + 300u;
        if (expected[dither_line_pixel] != 0x3defu)
            throw std::runtime_error("PSX line dithering fixture did not match its expected RGB555 value");
        constexpr size_t solid_rect_pixel = 210u * kReadbackWidth + 320u;
        if (expected[solid_rect_pixel] != 0x4210u)
            throw std::runtime_error("PSX solid rectangle was unexpectedly dithered");
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

        const auto compare_scanout = [&](const char* name, uint32_t mode,
                                         uint32_t start_x, uint32_t start_y,
                                         uint32_t horizontal_start,
                                         uint32_t horizontal_end,
                                         uint32_t vertical_start,
                                         uint32_t vertical_end) {
            const std::array<uint32_t, 5> gp1{
                0x03000000u,
                0x05000000u | start_x | (start_y << 10),
                0x06000000u | horizontal_start | (horizontal_end << 12),
                0x07000000u | vertical_start | (vertical_end << 10),
                0x08000000u | mode,
            };
            for (const uint32_t command : gp1) {
                software.write_status(command);
                hardware.write_status(command);
            }
            const auto expected_frame = software.framebuffer_rgb();
            const auto actual_frame = hardware.framebuffer_rgb();
            if (expected_frame.width != actual_frame.width ||
                expected_frame.height != actual_frame.height ||
                expected_frame.rgb.size() != actual_frame.rgb.size())
                throw std::runtime_error(std::string("PSX scanout dimensions differ in ") + name);
            size_t different_pixels = 0;
            const size_t total_pixels = size_t(expected_frame.width) * expected_frame.height;
            size_t first_pixel = total_pixels;
            for (size_t pixel = 0; pixel < total_pixels; ++pixel) {
                const size_t offset = pixel * 3;
                if (expected_frame.rgb[offset] == actual_frame.rgb[offset] &&
                    expected_frame.rgb[offset + 1] == actual_frame.rgb[offset + 1] &&
                    expected_frame.rgb[offset + 2] == actual_frame.rgb[offset + 2])
                    continue;
                if (first_pixel == total_pixels) first_pixel = pixel;
                ++different_pixels;
            }
            if (different_pixels) {
                std::cerr << "PSX software/OpenGL scanout mismatch in " << name
                          << ": " << different_pixels << " pixels, first at ("
                          << first_pixel % expected_frame.width << ','
                          << first_pixel / expected_frame.width << ")\n";
                throw std::runtime_error("PSX GPU scanout parity check failed");
            }
        };
        compare_scanout("256x240 RGB15", 0x00u, 0, 0,
                        0x260, 0xc60, 0x10, 0x100);
        compare_scanout("320x240 RGB24 with partial beam window", 0x11u, 1000, 500,
                        0x2a0, 0x6a0, 0x20, 0xf0);
        compare_scanout("384x240 RGB15", 0x41u, 1000, 500,
                        0x260, 0xc60, 0x10, 0x100);
        compare_scanout("512x480 interlaced RGB15", 0x26u, 1000, 500,
                        0x260, 0xc60, 0x10, 0x100);
        compare_scanout("320x256 PAL RGB15", 0x09u, 0, 0,
                        0x260, 0xc60, 0x23, 0x123);

        // The original PSX-style GPU (type 0) stores 8bpp texture pages in
        // interleaved VRAM rows. Exercise that distinct addressing path too.
        std::vector<uint8_t> psx_software_vram(software_vram.size());
        std::vector<uint8_t> psx_hardware_vram(psx_software_vram.size());
        Gpu psx_software(psx_software_vram, 0);
        Gpu psx_hardware(psx_hardware_vram, 0);
        psx_software.set_frame_capture_enabled(false);
        psx_hardware.set_frame_capture_enabled(false);
        psx_hardware.enable_hardware_renderer(window, context);
        const std::vector<uint32_t> interleaved_8bpp{
            0xa0000000u, (1u << 16) | 16u, (1u << 16) | 1u, 0x0201u,
            0xa0000000u, 64u, (1u << 16) | 3u,
            0x001f0000u, 0x000003e0u,
            0xe1002200u,
            0x65000000u, (100u << 16) | 100u, 0x00040020u,
            (1u << 16) | 2u,
        };
        send(psx_software, interleaved_8bpp);
        send(psx_hardware, interleaved_8bpp);
        const auto read_texture_pair = [](Gpu& gpu) {
            gpu.write_data(0xc0000000u);
            gpu.write_data((100u << 16) | 100u);
            gpu.write_data((1u << 16) | 2u);
            return gpu.read_data();
        };
        const uint32_t psx_software_pair = read_texture_pair(psx_software);
        const uint32_t psx_hardware_pair = read_texture_pair(psx_hardware);
        if (psx_software_pair != psx_hardware_pair)
            throw std::runtime_error("PSX type-0 interleaved 8bpp texture parity failed");

        std::vector<uint8_t> gpu1_software_vram(software_vram.size());
        std::vector<uint8_t> gpu1_hardware_vram(gpu1_software_vram.size());
        Gpu gpu1_software(gpu1_software_vram, 1);
        Gpu gpu1_hardware(gpu1_hardware_vram, 1);
        gpu1_software.set_frame_capture_enabled(false);
        gpu1_hardware.set_frame_capture_enabled(false);
        gpu1_hardware.enable_hardware_renderer(window, context);
        const std::vector<uint32_t> flipped_wrapped_sprite{
            0xa0000000u, 0u, (1u << 16) | 1u, 0x001fu,
            0xa0000000u, 255u, (1u << 16) | 1u, 0x03e0u,
            0xe1001400u,
            0x65000000u, (100u << 16) | 100u, 0u,
            (1u << 16) | 2u,
        };
        send(gpu1_software, flipped_wrapped_sprite);
        send(gpu1_hardware, flipped_wrapped_sprite);
        const uint32_t gpu1_software_pair = read_texture_pair(gpu1_software);
        const uint32_t gpu1_hardware_pair = read_texture_pair(gpu1_hardware);
        if (gpu1_software_pair != 0x03e0001fu)
            throw std::runtime_error("PSX GPU type-1 flipped UV fixture is invalid");
        if (gpu1_software_pair != gpu1_hardware_pair) {
            std::cerr << "PSX GPU type-1 flipped UV wrap mismatch: software=0x"
                      << std::hex << gpu1_software_pair << " hardware=0x"
                      << gpu1_hardware_pair << std::dec << '\n';
            throw std::runtime_error("PSX GPU type-1 flipped texture parity failed");
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
