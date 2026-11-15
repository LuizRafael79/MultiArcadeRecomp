#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace mamerecomp::chips {

// Four-bit Intel 8243 I/O expander used with the MCS-48 P2/PROG interface.
class I8243 {
public:
    using PortWrite = std::function<void(unsigned port, std::uint8_t value)>;

    explicit I8243(PortWrite port_write = {});
    void set_port_write_callback(PortWrite callback);
    void write_p2(std::uint8_t value);
    std::uint8_t read_p2() const;
    void set_prog(bool high);
    std::uint8_t port_latch(unsigned port) const;

private:
    std::array<std::uint8_t, 4> m_ports{};
    PortWrite m_port_write;
    std::uint8_t m_p2 = 0x0f;
    std::uint8_t m_data = 0x0f;
    std::uint8_t m_command = 0;
    bool m_prog_high = true;
};

} // namespace mamerecomp::chips
