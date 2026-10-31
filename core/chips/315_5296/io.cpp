#include "io.hpp"

#include <utility>

namespace mamerecomp::chips {

Sega3155296::Sega3155296(Callbacks callbacks) : m_callbacks(std::move(callbacks)) {
    reset();
}

void Sega3155296::reset() {
    m_direction = 0;
    m_output_latch.fill(0);
    m_cnt = 0;
    if (m_callbacks.write_port)
        for (unsigned port = 0; port < m_output_latch.size(); ++port) m_callbacks.write_port(port, 0);
    if (m_callbacks.write_cnt)
        for (unsigned pin = 0; pin < 3; ++pin) m_callbacks.write_cnt(pin, false);
}

std::uint8_t Sega3155296::read(std::uint32_t offset) {
    offset &= 0x3fU;
    if (offset < 8U) {
        if ((m_direction & m_direction_override) & (1U << offset)) return m_output_latch[offset];
        return m_callbacks.read_port ? m_callbacks.read_port(offset) : 0xff;
    }
    switch (offset) {
    case 0x08: return 'S';
    case 0x09: return 'E';
    case 0x0a: return 'G';
    case 0x0b: return 'A';
    case 0x0c:
    case 0x0e: return m_cnt;
    case 0x0d:
    case 0x0f: return m_direction;
    default: return 0xff;
    }
}

void Sega3155296::write(std::uint32_t offset, std::uint8_t data) {
    offset &= 0x3fU;
    if (offset < 8U) {
        if ((m_direction & (1U << offset)) && m_callbacks.write_port)
            m_callbacks.write_port(offset, data);
        m_output_latch[offset] = data;
        return;
    }
    if (offset == 0x0eU) {
        for (unsigned pin = 0; pin < 3; ++pin)
            if (m_callbacks.write_cnt) m_callbacks.write_cnt(pin, (data & (1U << pin)) != 0);
        m_cnt = data;
    } else if (offset == 0x0fU) {
        for (unsigned port = 0; port < m_output_latch.size(); ++port) {
            const auto bit = static_cast<std::uint8_t>(1U << port);
            if (((m_direction ^ data) & bit) && m_callbacks.write_port)
                m_callbacks.write_port(port, (data & bit) ? m_output_latch[port] : 0);
        }
        m_direction = data;
    }
}

} // namespace mamerecomp::chips
