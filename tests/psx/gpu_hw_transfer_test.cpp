#include "machine_memory.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t kSkip = 77;
constexpr uint32_t kDma2Base = 0x1f8010a0;
constexpr uint32_t kDmaDpcr = 0x1f8010f0;
constexpr uint32_t kDma2Enable = 1u << 11;
constexpr uint32_t kDma2LinkedList = 0x01000401;
constexpr uint32_t kDma2DeviceToRam = 0x11000000;

bool bus_write(const PsxBus& bus, uint32_t address, uint32_t value)
{
    return bus.write(bus.opaque, address, 4, value, 0);
}

uint32_t bus_read(const PsxBus& bus, uint32_t address)
{
    uint32_t value = 0;
    if (!bus.read(bus.opaque, address, 4, 0, &value))
        throw std::runtime_error("PSX bus rejected DMA result read");
    return value;
}

void dma_ram_to_gpu(mamerecomp::psx::MachineMemory& memory,
                    const PsxBus& bus, uint32_t list_address,
                    const std::vector<uint32_t>& packet)
{
    if (packet.empty() || packet.size() > 255)
        throw std::runtime_error("invalid test GPU packet size");
    if (!bus_write(bus, list_address,
                   (static_cast<uint32_t>(packet.size()) << 24) | 0x00ffffffu))
        throw std::runtime_error("failed to write DMA linked-list header");
    for (size_t i = 0; i < packet.size(); ++i) {
        if (!bus_write(bus, list_address + 4 + static_cast<uint32_t>(i * 4), packet[i]))
            throw std::runtime_error("failed to write DMA linked-list payload");
    }
    if (!bus_write(bus, kDmaDpcr, kDma2Enable) ||
        !bus_write(bus, kDma2Base, list_address) ||
        !bus_write(bus, kDma2Base + 4, 0) ||
        !bus_write(bus, kDma2Base + 8, kDma2LinkedList))
        throw std::runtime_error("failed to start GPU linked-list DMA");
    memory.dma().advance_to(1000, mamerecomp::psx::kZn2MemoryConfig.cpu_cycle_hz);
}

std::vector<uint32_t> dma_gpu_to_ram(mamerecomp::psx::MachineMemory& memory,
                                     const PsxBus& bus, uint32_t list_address,
                                     uint32_t result_address,
                                     unsigned x, unsigned y,
                                     unsigned width, unsigned height)
{
    dma_ram_to_gpu(memory, bus, list_address,
        {0xc0000000u, (y << 16) | x, (height << 16) | width});
    const uint32_t pixels = width * height;
    const uint32_t words = (pixels + 1) / 2;
    if (!bus_write(bus, kDma2Base, result_address) ||
        !bus_write(bus, kDma2Base + 4, words) ||
        !bus_write(bus, kDma2Base + 8, kDma2DeviceToRam))
        throw std::runtime_error("failed to start GPU readback DMA");
    std::vector<uint32_t> result(words);
    for (uint32_t i = 0; i < words; ++i)
        result[i] = bus_read(bus, result_address + i * 4);
    return result;
}

bool check_packed_pixels(const std::vector<uint32_t>& words,
                         const std::vector<uint16_t>& expected)
{
    if (words.size() != (expected.size() + 1) / 2) return false;
    for (size_t pixel = 0; pixel < expected.size(); ++pixel) {
        const uint16_t value = static_cast<uint16_t>(words[pixel / 2] >>
            ((pixel & 1u) * 16));
        if (value != expected[pixel]) {
            std::cerr << "DMA GPUREAD mismatch at pixel " << pixel << ": got 0x"
                      << std::hex << value << " expected 0x" << expected[pixel]
                      << std::dec << '\n';
            return false;
        }
    }
    if ((expected.size() & 1u) && (words.back() >> 16) != 0) {
        std::cerr << "DMA GPUREAD did not zero-fill the final high halfword\n";
        return false;
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
    if (!context || !SDL_GL_MakeCurrent(window, context)) {
        std::cerr << "Skipping OpenGL transfer test: " << SDL_GetError() << '\n';
        if (context) SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return kSkip;
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer) std::cout << "PSX GPU DMA transfer test renderer: " << renderer << '\n';

    bool passed = false;
    try {
        using namespace mamerecomp::psx;
        MachineMemory memory(kZn2MemoryConfig);
        const PsxBus bus = memory.make_bus();
        memory.gpu().enable_hardware_renderer(window, context);

        constexpr std::array<uint16_t, 9> uploaded{
            0x1234, 0x2345, 0x3456,
            0x4567, 0x5678, 0x6789,
            0x789a, 0x89ab, 0x9abc,
        };
        std::vector<uint32_t> upload_packet{
            0xa0000000u,
            (511u << 16) | 1022u,
            (3u << 16) | 3u,
        };
        for (size_t i = 0; i < uploaded.size(); i += 2) {
            upload_packet.push_back(uploaded[i] |
                (i + 1 < uploaded.size() ? uint32_t(uploaded[i + 1]) << 16 : 0));
        }
        dma_ram_to_gpu(memory, bus, 0x400, upload_packet);

        std::vector<uint32_t> copy_packet{
            0x80000000u,
            (511u << 16) | 1022u,
            (511u << 16) | 1008u,
            (3u << 16) | 3u,
        };
        dma_ram_to_gpu(memory, bus, 0x500, copy_packet);
        const auto copied = dma_gpu_to_ram(memory, bus, 0x600, 0x800,
                                           1008, 511, 3, 3);
        if (!check_packed_pixels(copied,
                std::vector<uint16_t>(uploaded.begin(), uploaded.end())))
            throw std::runtime_error("DMA A0/80/C0 wrapped transfer check failed");

        // A GPU fill crosses X=1023 and Y=511. A second C0 DMA must observe
        // the pending GPU write after synchronizing the wrapped rectangle.
        dma_ram_to_gpu(memory, bus, 0x700,
            {0x0200ff00u, (511u << 16) | 1008u, (2u << 16) | 32u});
        const auto filled = dma_gpu_to_ram(memory, bus, 0x900, 0xa00,
                                           1008, 511, 32, 2);
        if (!check_packed_pixels(filled, std::vector<uint16_t>(64, 0x03e0)))
            throw std::runtime_error("DMA GPU fill/readback wrap check failed");
        passed = true;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return passed ? 0 : 1;
}
