#include "mapper.hpp"

#include <algorithm>

namespace multiarcaderecomp::chips {

Mapper3155195::Mapper3155195(std::array<Wiring, 8> wiring, Callbacks callbacks)
    : m_wiring(std::move(wiring)), m_callbacks(std::move(callbacks)) {
    rebuild_mappings();
}

void Mapper3155195::rebuild_mappings() {
    static constexpr std::array<std::uint32_t, 4> size_masks{0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff};
    m_mappings.clear();
    for (unsigned index = 0; index < m_wiring.size(); ++index) {
        const auto& wire = m_wiring[index];
        if (wire.length == 0) continue;
        const std::uint32_t size_mask = size_masks[m_regs[0x10 + index * 2] & 3U];
        const std::uint32_t base = (static_cast<std::uint32_t>(m_regs[0x11 + index * 2]) << 16) & ~size_mask;
        const std::uint32_t mirror = wire.mirror & size_mask;
        const std::uint32_t start = base + (wire.target_offset & size_mask);
        const std::uint32_t end = start + std::min(wire.length - 1, size_mask);
        m_mappings.push_back({index, wire.target, start, end, mirror, wire.target_offset, size_mask});
    }
    // Hardware maps regions in descending index order. Keep that ordering for
    // board bus decoders that must resolve overlapping regions.
    std::stable_sort(m_mappings.begin(), m_mappings.end(), [](const Mapping& a, const Mapping& b) {
        return a.region > b.region;
    });
}

void Mapper3155195::configure_explicit(std::span<const std::uint8_t, 16> config) {
    std::copy(config.begin(), config.end(), m_regs.begin() + 0x10);
    rebuild_mappings();
}

std::uint8_t Mapper3155195::read(std::uint8_t offset) {
    offset &= 0x1fU;
    switch (offset) {
    case 0x00:
    case 0x01: return m_regs[offset];
    case 0x02: return (m_regs[0x02] & 3U) == 3U ? 0x00 : 0x0f;
    case 0x03:
        if (m_callbacks.mcu_irq) m_callbacks.mcu_irq(false);
        return m_sound_reply;
    default: return 0xff;
    }
}

void Mapper3155195::set_sound_reply(std::uint8_t data) {
    m_sound_reply = data;
    if (m_callbacks.mcu_irq) m_callbacks.mcu_irq(true);
}

std::uint16_t Mapper3155195::read_mapped(std::uint32_t address) {
    return m_callbacks.read16 ? m_callbacks.read16(address & 0x00fffffeU) : 0xffff;
}

void Mapper3155195::write_mapped(std::uint32_t address, std::uint16_t data) {
    if (m_callbacks.write16) m_callbacks.write16(address & 0x00fffffeU, data);
}

void Mapper3155195::write(std::uint8_t offset, std::uint8_t data) {
    offset &= 0x1fU;
    const std::uint8_t old = m_regs[offset];
    m_regs[offset] = data;
    switch (offset) {
    case 0x02:
        if (((old ^ data) & 3U) != 0 && m_callbacks.maincpu_reset)
            m_callbacks.maincpu_reset((data & 3U) == 3U);
        break;
    case 0x03:
        if (m_callbacks.to_sound) m_callbacks.to_sound(data);
        break;
    case 0x04:
        // Negative logic. HOLD_LINE is asserted for the selected 68000 IRQ,
        // while $07 clears every IRQ line.
        if (m_callbacks.irq) m_callbacks.irq((data & 7U) == 7U ? 0U : ((~data) & 7U));
        break;
    case 0x05:
        if (data == 0x01) {
            const std::uint32_t address = (static_cast<std::uint32_t>(m_regs[0x0a]) << 17)
                | (static_cast<std::uint32_t>(m_regs[0x0b]) << 9)
                | (static_cast<std::uint32_t>(m_regs[0x0c]) << 1);
            write_mapped(address, static_cast<std::uint16_t>((m_regs[0] << 8) | m_regs[1]));
        } else if (data == 0x02) {
            const std::uint32_t address = (static_cast<std::uint32_t>(m_regs[0x07]) << 17)
                | (static_cast<std::uint32_t>(m_regs[0x08]) << 9)
                | (static_cast<std::uint32_t>(m_regs[0x09]) << 1);
            const auto result = read_mapped(address);
            m_regs[0] = static_cast<std::uint8_t>(result >> 8);
            m_regs[1] = static_cast<std::uint8_t>(result);
        }
        break;
    default:
        if (offset >= 0x10 && old != data) rebuild_mappings();
        break;
    }
}

std::uint8_t Mapper3155195::sound_latch_read() {
    return m_regs[0x03];
}

} // namespace multiarcaderecomp::chips
