#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mamerecomp::naomi {

// Xicor X76F100 secure serial EEPROM used as the Naomi ROM-board ID.
// Protocol and reset behavior follow MAME's x76f100_device and Flycast's
// NaomiBoardID/NaomiGameID wiring.
class NaomiX76F100 {
public:
    static constexpr std::size_t kImageSize = 0x84;

    void load(std::span<const std::uint8_t> image);
    void reset();
    void writeCS(bool value);
    void writeRST(bool value);
    void writeSCL(bool value);
    void writeSDA(bool value);
    [[nodiscard]] bool readSDA() const;

private:
    enum class State : std::uint8_t {
        stop, response_to_reset, load_command, load_password,
        verify_password, read_data, write_data
    };
    enum class Command : std::uint8_t {
        write = 0x80, read = 0x81,
        change_write_password = 0xfc, change_read_password = 0xfe,
        ack_password = 0x55
    };

    void acceptPassword();
    [[nodiscard]] int dataOffset() const;

    std::array<std::uint8_t, 4> m_response{0x19, 0x00, 0xaa, 0x55};
    std::array<std::uint8_t, 8> m_write_password{};
    std::array<std::uint8_t, 8> m_read_password{};
    std::array<std::uint8_t, 112> m_data{};
    std::array<std::uint8_t, 8> m_write_buffer{};
    bool m_cs = false;
    bool m_rst = false;
    bool m_scl = false;
    bool m_scl_changed = false;
    bool m_sdaw = false;
    bool m_sdar = false;
    State m_state = State::stop;
    std::uint8_t m_shift = 0;
    std::uint8_t m_bit = 0;
    std::uint8_t m_byte = 0;
    std::uint8_t m_command = 0;
};

} // namespace mamerecomp::naomi
