#pragma once

#include <cstdint>

namespace multiarcaderecomp::chips {

// Intel 8255 PPI state and the mode-2 port-A handshake used by System 16A.
// Board code supplies address decoding and wires the port outputs to devices.
class I8255 {
public:
    struct WriteEffects {
        std::uint8_t register_index = 0;
        std::uint8_t value = 0;
        bool port_write = false;
        bool port_c_write = false;
        bool control_write = false;
        bool port_c_changed = false;
        bool mode2 = false;
    };

    std::uint8_t read(unsigned register_index) const;
    WriteEffects write(unsigned register_index, std::uint8_t value);
    void acknowledge_port_a();

    bool port_a_mode2() const { return m_port_a_mode2; }
    std::uint8_t port_c() const { return m_port_c; }

private:
    std::uint8_t m_port_a = 0xff;
    std::uint8_t m_port_b = 0;
    // PC7 is OBF (active low), PC6 is ACK (active low).
    std::uint8_t m_port_c = 0xc0;
    std::uint8_t m_control = 0x9b;
    bool m_port_a_mode2 = false;
};

} // namespace multiarcaderecomp::chips
