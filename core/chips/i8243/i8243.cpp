#include "i8243.hpp"

#include <utility>

namespace mamerecomp::chips {

I8243::I8243(PortWrite port_write) : m_port_write(std::move(port_write)) {}

void I8243::set_port_write_callback(PortWrite callback) {
    m_port_write = std::move(callback);
}

void I8243::write_p2(std::uint8_t value) {
    m_p2 = value & 0x0fU;
}

std::uint8_t I8243::read_p2() const {
    return m_data;
}

void I8243::set_prog(bool high) {
    if (m_prog_high && !high) {
        m_command = m_p2;
        const auto operation = m_command >> 2;
        const auto port = m_command & 3U;
        m_data = operation == 0 ? m_ports[port] : 0x0fU;
    } else if (!m_prog_high && high) {
        const auto operation = m_command >> 2;
        const auto port = m_command & 3U;
        const auto data = m_p2 & 0x0fU;
        if (operation == 1) m_ports[port] = data;
        else if (operation == 2) m_ports[port] = (m_ports[port] | data) & 0x0fU;
        else if (operation == 3) m_ports[port] = (m_ports[port] & data) & 0x0fU;
        if (operation != 0) {
            m_data = 0x0fU;
            if (m_port_write) m_port_write(port, m_ports[port]);
        } else {
            m_data = 0x0fU;
        }
    }
    m_prog_high = high;
}

std::uint8_t I8243::port_latch(unsigned port) const {
    return m_ports[port & 3U];
}

} // namespace mamerecomp::chips
