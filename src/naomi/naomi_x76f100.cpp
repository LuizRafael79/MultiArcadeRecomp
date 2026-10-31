#include "naomi_x76f100.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace mamerecomp::naomi {
namespace {
unsigned g_trace_events = 0;

void traceX76(const char* event, unsigned a = 0, unsigned b = 0) {
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_X76") && g_trace_events < 96) {
        ++g_trace_events;
        std::fprintf(stderr, "Naomi X76F100 %s a=%02x b=%02x\n", event, a, b);
    }
}
}

void NaomiX76F100::load(std::span<const std::uint8_t> image) {
    if (image.size() != kImageSize)
        throw std::invalid_argument("Naomi X76F100 image must be exactly 0x84 bytes");
    auto cursor = image.begin();
    std::copy_n(cursor, m_response.size(), m_response.begin()); cursor += m_response.size();
    std::copy_n(cursor, m_write_password.size(), m_write_password.begin()); cursor += m_write_password.size();
    std::copy_n(cursor, m_read_password.size(), m_read_password.begin()); cursor += m_read_password.size();
    std::copy_n(cursor, m_data.size(), m_data.begin());
    reset();
}

void NaomiX76F100::reset() {
    m_write_buffer.fill(0);
    m_cs = false;
    m_rst = false;
    m_scl = false;
    m_sdaw = false;
    m_sdar = false;
    m_password_accepted = false;
    m_state = State::stop;
    m_shift = m_bit = m_byte = m_command = m_password_retries = 0;
}

void NaomiX76F100::writeCS(bool value) {
    if (m_cs && !value) m_state = State::stop;
    else if (!m_cs && value) {
        m_state = State::stop;
        m_sdar = false;
    }
    m_cs = value;
}

void NaomiX76F100::writeRST(bool value) {
    if (!m_rst && value && !m_cs) {
        m_state = State::response_to_reset;
        m_bit = 0;
        m_byte = 0;
        traceX76("reset");
    }
    m_rst = value;
}

std::array<std::uint8_t, 8>& NaomiX76F100::password() {
    if ((m_command & 0xe1u) == static_cast<std::uint8_t>(Command::read))
        return m_read_password;
    return m_write_password;
}

void NaomiX76F100::acceptPassword() {
    m_password_retries = 0;
    if ((m_command & 0x81u) == static_cast<std::uint8_t>(Command::read))
        m_state = State::read_data;
    else if ((m_command & 0x81u) == static_cast<std::uint8_t>(Command::write))
        m_state = State::write_data;
}

int NaomiX76F100::dataOffset() const {
    const auto offset = static_cast<unsigned>((m_command >> 1) & 0x0fu) * 8u + m_byte;
    return offset < m_data.size() ? static_cast<int>(offset) : -1;
}

void NaomiX76F100::writeSCL(bool value) {
    if (!m_cs) {
        switch (m_state) {
        case State::stop:
            break;
        case State::response_to_reset:
            if (m_scl && !value) {
                if (m_bit == 0) m_shift = m_response[m_byte];
                m_sdar = (m_shift & 1u) != 0;
                m_shift >>= 1;
                if (++m_bit == 8) {
                    m_bit = 0;
                    m_byte = static_cast<std::uint8_t>((m_byte + 1u) % m_response.size());
                }
            }
            break;
        case State::load_command:
        case State::load_password:
        case State::verify_password:
        case State::write_data:
            if (!m_scl && value) {
                if (m_bit < 8) {
                    m_shift = static_cast<std::uint8_t>((m_shift << 1) | (m_sdaw ? 1u : 0u));
                    ++m_bit;
                } else {
                    m_sdar = false;
                    switch (m_state) {
                    case State::load_command:
                        m_command = m_shift;
                        traceX76("command", m_command);
                        m_state = State::load_password;
                        break;
                    case State::load_password:
                        m_write_buffer[m_byte++] = m_shift;
                        if (m_byte == m_write_buffer.size()) {
                            m_state = State::verify_password;
                            m_password_accepted = (password() == m_write_buffer);
                            traceX76("password", m_password_accepted ? 1u : 0u,
                                     m_command);
                            if (!m_password_accepted && ++m_password_retries >= 8) {
                                m_read_password.fill(0);
                                m_write_password.fill(0);
                                m_data.fill(0);
                                m_password_retries = 0;
                            }
                        }
                        break;
                    case State::verify_password:
                        if (m_shift == static_cast<std::uint8_t>(Command::ack_password)) {
                            traceX76("ack", m_password_accepted ? 1u : 0u,
                                     static_cast<unsigned>(m_command));
                            if (m_password_accepted) acceptPassword();
                            else m_sdar = true;
                        }
                        break;
                    case State::write_data:
                        m_write_buffer[m_byte++] = m_shift;
                        if (m_byte == m_write_buffer.size()) {
                            if (m_command == static_cast<std::uint8_t>(Command::change_write_password))
                                m_write_password = m_write_buffer;
                            else if (m_command == static_cast<std::uint8_t>(Command::change_read_password))
                                m_read_password = m_write_buffer;
                            else {
                                for (m_byte = 0; m_byte < m_write_buffer.size(); ++m_byte) {
                                    const int offset = dataOffset();
                                    if (offset < 0) break;
                                    m_data[static_cast<std::size_t>(offset)] = m_write_buffer[m_byte];
                                }
                            }
                            m_byte = 0;
                        }
                        break;
                    default:
                        break;
                    }
                    m_bit = 0;
                    m_shift = 0;
                }
            }
            break;
        case State::read_data:
            if (!m_scl && value) {
                if (m_bit < 8) {
                    if (m_bit == 0) {
                        const int offset = dataOffset();
                        m_shift = offset < 0 ? 0 : m_data[static_cast<std::size_t>(offset)];
                        traceX76("read-byte", offset < 0 ? 0xffu : static_cast<unsigned>(offset), m_shift);
                    }
                    m_sdar = (m_shift & 0x80u) != 0;
                    m_shift <<= 1;
                    ++m_bit;
                } else {
                    m_bit = 0;
                    m_sdar = false;
                    if (!m_sdaw) ++m_byte;
                }
            }
            break;
        }
    }
    m_scl = value;
}

void NaomiX76F100::writeSDA(bool value) {
    if (!m_cs && m_scl) {
        if (!m_sdaw && value) {
            m_state = State::stop;
            m_sdar = false;
        }
        if (m_sdaw && !value) {
            if (m_state == State::stop) m_state = State::load_command;
            // START during password/data phases is ignored by the chip.
            m_bit = 0;
            m_byte = 0;
            m_shift = 0;
            m_sdar = false;
        }
    }
    m_sdaw = value;
}

bool NaomiX76F100::readSDA() const {
    return m_cs ? true : m_sdar;
}

} // namespace mamerecomp::naomi
