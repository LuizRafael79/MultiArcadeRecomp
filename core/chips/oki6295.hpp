#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mamerecomp::chips {

// Clocked OKIM6295 command/status and four-voice ADPCM device. The board owns
// ROM banking and supplies the chip's current sample-ROM address view.
class Oki6295 {
public:
    using RomRead = std::uint8_t (*)(void*, std::uint32_t);

    Oki6295(std::span<const std::uint8_t> rom, RomRead read = nullptr,
            void* context = nullptr);
    void reset();
    void write(std::uint8_t value);
    std::uint8_t read() const;
    void clock_nibble();
    std::int16_t output() const;
    unsigned active_voices() const;

private:
    struct Voice {
        bool playing = false;
        std::uint32_t start = 0;
        std::uint32_t nibble = 0;
        std::uint32_t nibble_count = 0;
        int signal = 0;
        int step = 0;
        std::uint8_t volume = 0;
    };

    std::uint8_t rom_read(std::uint32_t address) const;
    std::uint32_t sample_address(std::uint32_t offset) const;

    std::span<const std::uint8_t> rom_;
    RomRead read_;
    void* context_;
    std::array<Voice, 4> voices_{};
    int pending_sample_ = -1;
};

} // namespace mamerecomp::chips
