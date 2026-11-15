#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace multiarcaderecomp::runtime {

// Board-facing address bus. CPU ABIs may extend this with ISA-specific
// services, while machine runtimes use the same read/write contract to
// connect CPUs to board decoders.
struct AddressBus {
    using Read = bool (*)(void *, std::uint32_t address, unsigned size,
                          std::uint32_t pc, std::uint32_t *value);
    using Write = bool (*)(void *, std::uint32_t address, unsigned size,
                           std::uint32_t pc, std::uint32_t value);

    void *context = nullptr;
    Read read = nullptr;
    Write write = nullptr;
};

// Platform-independent arcade controls. Board runtimes translate these
// semantic buttons into the active-low ports and wiring defined by MAME.
struct ArcadeInput {
    static constexpr std::uint8_t left = 1U << 0;
    static constexpr std::uint8_t right = 1U << 1;
    static constexpr std::uint8_t up = 1U << 2;
    static constexpr std::uint8_t down = 1U << 3;
    static constexpr std::uint8_t button1 = 1U << 4;
    static constexpr std::uint8_t button2 = 1U << 5;
    static constexpr std::uint8_t button3 = 1U << 6;
    static constexpr std::uint8_t start = 1U << 7;
    static constexpr std::uint16_t button4 = 1U << 8;
    static constexpr std::uint16_t button5 = 1U << 9;
    static constexpr std::uint16_t button6 = 1U << 10;

    std::uint8_t coins_pressed = 0;
    std::array<std::uint16_t, 4> players_pressed{};
    bool service_pressed = false;
    std::uint8_t service_switches = 0;
};

struct Rgb24FrameView {
    unsigned width = 0;
    unsigned height = 0;
    std::size_t pitch = 0;
    std::span<const std::uint8_t> pixels;
};

struct Pcm16AudioView {
    unsigned sample_rate = 0;
    unsigned channels = 0;
    std::span<const std::int16_t> samples;
};

} // namespace multiarcaderecomp::runtime
