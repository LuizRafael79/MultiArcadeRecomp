#pragma once

#include <array>
#include <cstdint>

namespace multiarcaderecomp::chips {

// RF5C68 eight-channel PCM generator. The host supplies chip clock ticks and
// routes the stereo DAC output into its board's mixer.
class Rf5c68 {
public:
    void write_register(std::uint8_t offset, std::uint8_t value) noexcept;
    std::uint8_t read_sample_ram(std::uint16_t offset) const noexcept;
    void write_sample_ram(std::uint16_t offset, std::uint8_t value) noexcept;
    void advance_clocks(std::uint32_t clocks) noexcept;

    std::array<std::int16_t, 2> output() const noexcept { return m_output; }
    std::uint8_t enabled_channels() const noexcept { return m_enabled_channels; }
    std::uint64_t sample_ram_writes() const noexcept { return m_sample_ram_writes; }
    std::uint16_t peak() const noexcept { return m_peak; }

private:
    struct Channel {
        std::uint8_t env = 0;
        std::uint8_t pan = 0;
        std::uint8_t start = 0;
        std::uint32_t address = 0;
        std::uint16_t step = 0;
        std::uint16_t loop = 0;
        bool enabled = false;
    };

    void generate_sample() noexcept;
    void update_enabled_channels() noexcept;

    std::array<std::uint8_t, 0x10000> m_ram{};
    std::array<Channel, 8> m_channels{};
    std::array<std::int16_t, 2> m_output{};
    std::uint16_t m_write_bank = 0;
    std::uint8_t m_channel_bank = 0;
    std::uint8_t m_enabled_channels = 0;
    std::uint32_t m_clock_fraction = 0;
    std::uint64_t m_sample_ram_writes = 0;
    std::uint16_t m_peak = 0;
    bool m_enabled = false;
};

} // namespace multiarcaderecomp::chips
