#include "chips/i8255/i8255.hpp"

namespace multiarcaderecomp::chips {

std::uint8_t I8255::read(unsigned register_index) const {
    switch (register_index & 3U) {
    case 0: return m_port_a;
    case 1: return m_port_b;
    case 2: return m_port_c;
    default: return m_control;
    }
}

I8255::WriteEffects I8255::write(unsigned register_index, std::uint8_t value) {
    WriteEffects effects;
    effects.register_index = static_cast<std::uint8_t>(register_index & 3U);
    effects.value = value;
    effects.mode2 = m_port_a_mode2;
    switch (effects.register_index) {
    case 0:
        m_port_a = value;
        effects.port_write = true;
        if (m_port_a_mode2) m_port_c &= static_cast<std::uint8_t>(~0x80U); // OBF low
        break;
    case 1:
        m_port_b = value;
        effects.port_write = true;
        break;
    case 2: {
        const auto previous = m_port_c;
        m_port_c = m_port_a_mode2
            ? static_cast<std::uint8_t>((value & 0x3fU) | (m_port_c & 0xc0U))
            : value;
        effects.port_write = true;
        effects.port_c_write = true;
        effects.port_c_changed = previous != m_port_c;
        effects.value = m_port_c;
        break;
    }
    default:
        effects.control_write = true;
        if (value & 0x80U) {
            m_control = value;
            const bool previous_mode2 = m_port_a_mode2;
            m_port_a_mode2 = ((value >> 5) & 0x03U) == 0x02U;
            if (m_port_a_mode2) m_port_c |= 0xc0U; // OBF and ACK idle high
            effects.port_c_changed = previous_mode2 != m_port_a_mode2;
            effects.value = m_port_c;
        } else {
            const unsigned bit = (value >> 1) & 7U;
            if (!(m_port_a_mode2 && (bit == 6 || bit == 7))) {
                const auto previous = m_port_c;
                const auto mask = static_cast<std::uint8_t>(1U << bit);
                if (value & 1U) m_port_c |= mask;
                else m_port_c &= static_cast<std::uint8_t>(~mask);
                effects.port_c_changed = previous != m_port_c;
                effects.value = m_port_c;
            }
        }
        break;
    }
    effects.mode2 = m_port_a_mode2;
    return effects;
}

void I8255::acknowledge_port_a() {
    m_port_c &= static_cast<std::uint8_t>(~0x40U); // ACK low
    if (m_port_a_mode2) m_port_c |= 0x80U; // ACK releases OBF high
}

} // namespace multiarcaderecomp::chips
