#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace multiarcaderecomp::chips {

// Behavioral model of Sega's 315-5296 eight-port I/O device.
class Sega3155296 {
public:
    struct Callbacks {
        std::function<std::uint8_t(unsigned)> read_port;
        std::function<void(unsigned, std::uint8_t)> write_port;
        std::function<void(unsigned, bool)> write_cnt;
    };

    explicit Sega3155296(Callbacks callbacks = {});
    void reset();
    std::uint8_t read(std::uint32_t offset);
    void write(std::uint32_t offset, std::uint8_t data);
    void set_direction_override(std::uint8_t mask) { m_direction_override = mask; }
    std::uint8_t debug_output(std::uint32_t port) const { return m_output_latch[port & 7U]; }
    std::uint8_t direction() const { return m_direction; }
    std::uint8_t counter_latch() const { return m_cnt; }

private:
    Callbacks m_callbacks;
    std::array<std::uint8_t, 8> m_output_latch{};
    std::uint8_t m_cnt = 0;
    std::uint8_t m_direction = 0;
    std::uint8_t m_direction_override = 0xff;
};

} // namespace multiarcaderecomp::chips
