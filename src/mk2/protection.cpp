#include "mk2/protection.hpp"

namespace mamerecomp::mk2 {

bool Protection::read(std::uint32_t address, std::uint16_t& value) const noexcept {
    if ((address >= 0x01a190e0u && address < 0x01a19100u) ||
        (address >= 0x01a3d0c0u && address < 0x01a3d100u)) {
        value = m_data;
        return true;
    }
    if (address >= 0x01a191c0u && address < 0x01a191e0u) {
        value = m_data >> 1;
        return true;
    }
    if ((address >= 0x01d9d1e0u && address < 0x01d9d200u) ||
        (address >= 0x01def920u && address < 0x01def940u)) {
        value = 2;
        return true;
    }
    return false;
}

bool Protection::write(std::uint32_t address, std::uint16_t value) noexcept {
    if ((address >= 0x00f20c60u && address < 0x00f20c80u) ||
        (address >= 0x00f42820u && address < 0x00f42840u)) {
        m_data = value;
        return true;
    }
    return false;
}

} // namespace mamerecomp::mk2
