#include "system12_board.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <utility>

namespace mamerecomp::namco::system12 {

NamcoSystem12Board::NamcoSystem12Board(const mamerecomp::rom::RomRegions& roms)
    : roms_(&roms), c352_(roms.at("c352")) {}

PsxBus NamcoSystem12Board::make_bus() {
    PsxBus bus{};
    bus.opaque = this;
    bus.read = read_cb;
    bus.write = write_cb;
    return bus;
}

bool NamcoSystem12Board::sub_read(uint32_t address, unsigned size, uint32_t *value) {
    if (!value || (size != 1 && size != 2 && size != 4)) return false;
    if (address >= 0xff0000 && address + size <= 0x1000000) {
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result = (result << 8) | h8_read8(address + i);
        *value = result;
        return true;
    }
    if (address + size <= 0x80000) {
        const auto &rom = roms_->at("audio_cpu");
        if (address + size > rom.size()) return false;
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result = (result << 8) | rom[address + i];
        *value = result;
        return true;
    }
    if (address >= 0x80000 && address + size <= 0x90000) {
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result = (result << 8) | shared_ram_[address - 0x80000 + i];
        *value = result;
        return true;
    }
    if (address >= 0x280000 && address + size <= 0x288000) {
        const uint16_t word = c352_.read(static_cast<uint16_t>((address - 0x280000) >> 1));
        if (size == 2) *value = word;
        else if (size == 1) *value = (address & 1) ? (word & 0xff) : (word >> 8);
        else {
            const uint16_t next = c352_.read(static_cast<uint16_t>(((address - 0x280000) >> 1) + 1));
            *value = (uint32_t(word) << 16) | next;
        }
        return true;
    }
    if (address >= 0x300000 && address + size <= 0x300004) {
        *value = size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;
        return true;
    }
    if ((address >= 0x300010 && address + size <= 0x300012) ||
        (address >= 0x300030 && address + size <= 0x300032)) {
        *value = size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;
        return true;
    }
    return false;
}

bool NamcoSystem12Board::sub_write(uint32_t address, unsigned size, uint32_t value) {
    if (size != 1 && size != 2 && size != 4) return false;
    if (address >= 0xff0000 && address + size <= 0x1000000) {
        for (unsigned i = 0; i < size; ++i)
            h8_write8(address + i, static_cast<uint8_t>(value >> ((size - 1 - i) * 8)));
        return true;
    }
    if (address >= 0x80000 && address + size <= 0x90000) {
        for (unsigned i = 0; i < size; ++i)
            shared_ram_[address - 0x80000 + i] = static_cast<uint8_t>(value >> ((size - 1 - i) * 8));
        return true;
    }
    if (address >= 0x280000 && address + size <= 0x288000) {
        const uint16_t offset = static_cast<uint16_t>((address - 0x280000) >> 1);
        if (std::getenv("MAMERECOMP_TRACE_SYSTEM12_C352")) {
            static unsigned traced = 0;
            if (traced++ < 256)
                std::cerr << "System 12 C352 write offset=0x" << std::hex << offset
                          << " size=" << std::dec << size << " data=0x" << std::hex
                          << value << std::dec << '\n';
        }
        if (size == 2) c352_.write(offset, static_cast<uint16_t>(value));
        else if (size == 1) {
            const uint16_t mask = (address & 1) ? 0x00ff : 0xff00;
            const uint16_t data = (address & 1) ? uint16_t(value) : uint16_t(value << 8);
            c352_.write(offset, data, mask);
        } else {
            c352_.write(offset, static_cast<uint16_t>(value >> 16));
            c352_.write(static_cast<uint16_t>(offset + 1), static_cast<uint16_t>(value));
        }
        c352_writes_ += size == 4 ? 2 : 1;
        return true;
    }
    if ((address >= 0x300010 && address + size <= 0x300012) ||
        (address >= 0x300030 && address + size <= 0x300032)) return true;
    return false;
}

uint8_t NamcoSystem12Board::h8_port_input(unsigned port) const {
    switch (port) {
    case 6: return 0xff; // MAME sub_port6_r with JVS not initialized yet.
    case 7: return 0xff; // default System 12 DIP switch state.
    case 8: return 0xff; // MAME sub_port8_r before a JVS node is initialized.
    case 10: return 0xff; // sub_porta_r
    case 11: return h8_portb_latch_ | 0x7f; // sub_portb_r
    default: return 0xff;
    }
}

uint8_t NamcoSystem12Board::h8_port_read(unsigned port) const {
    const uint8_t mask = port == 6 ? 0x80 : port == 8 ? 0xe0 :
                         port == 9 ? 0xc0 : 0x00;
    return mask | (h8_port_dr_[port] & ~mask & h8_port_ddr_[port]) |
           (h8_port_input(port) & ~h8_port_ddr_[port]);
}

uint8_t NamcoSystem12Board::h8_read8(uint32_t address) {
    if (address >= 0xfffd10 && address <= 0xffff0f)
        return h8_internal_ram_[address - 0xfffd10];
    switch (address) {
    case 0xffc5: case 0xffc9: case 0xffcd: case 0xffd0:
    case 0xffd1: case 0xffd4: return 0xff; // H8 port DDR readback is open bus.
    case 0xffc7: return h8_port_read(4);
    case 0xffcb: return h8_port_read(6);
    case 0xffce: return h8_port_input(7);
    case 0xffcf: return h8_port_read(8);
    case 0xffd2: return h8_port_read(9);
    case 0xffd3: return h8_port_read(10);
    case 0xffd6: return h8_port_read(11);
    case 0xffda: return h8_port4_pcr_;
    case 0xfff2: return h8_syscr_;
    default: return 0xff;
    }
}

void NamcoSystem12Board::h8_write8(uint32_t address, uint8_t value) {
    if (address >= 0xfffd10 && address <= 0xffff0f) {
        h8_internal_ram_[address - 0xfffd10] = value;
        return;
    }
    switch (address) {
    case 0xffc5: h8_port_ddr_[4] = value; break;
    case 0xffc7: h8_port_dr_[4] = value; break;
    case 0xffc9: h8_port_ddr_[6] = value; break;
    case 0xffcb: h8_port_dr_[6] = value; break;
    case 0xffcd: h8_port_ddr_[8] = value; break;
    case 0xffcf: h8_port8_latch_ = value; h8_port_dr_[8] = value; break;
    case 0xffd0: h8_port_ddr_[9] = value; break;
    case 0xffd2: h8_port_dr_[9] = value; break;
    case 0xffd1: h8_port_ddr_[10] = value; break;
    case 0xffd3: h8_porta_latch_ = value; h8_port_dr_[10] = value; break;
    case 0xffd4: h8_port_ddr_[11] = value; break;
    case 0xffd6:
        h8_portb_latch_ = (h8_portb_latch_ & 0x80) | (value & 0x7f);
        h8_port_dr_[11] = value;
        break;
    case 0xffda: h8_port4_pcr_ = value; break;
    case 0xfff2: h8_syscr_ = value; break;
    default: break; // Other H83002 internal peripherals are still pending.
    }
}

bool NamcoSystem12Board::read_cb(void *opaque, uint32_t address, unsigned size,
                                uint32_t, uint32_t *value) {
    return static_cast<NamcoSystem12Board *>(opaque)->read(address, size, value);
}

bool NamcoSystem12Board::write_cb(void *opaque, uint32_t address, unsigned size,
                                 uint32_t value, uint32_t) {
    return static_cast<NamcoSystem12Board *>(opaque)->write(address, size, value);
}

bool NamcoSystem12Board::read(uint32_t address, unsigned size, uint32_t *value) {
    if (!value || (size != 1 && size != 2 && size != 4)) return false;
    const uint32_t p = address & 0x1fffffff;
    if (p >= 0x1fa00000 && p + size <= 0x1fc00000) {
        const auto &rom = roms_->at("program");
        const size_t offset = size_t(bank_) * 0x200000 + (p - 0x1fa00000);
        if (offset + size > rom.size()) return false;
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result |= uint32_t(rom[offset + i]) << (i * 8);
        *value = result;
        return true;
    }
    if (p >= 0x1f080000 && p + size <= 0x1f084000) {
        const size_t offset = p - 0x1f080000;
        uint32_t result = 0;
        for (unsigned i = 0; i < size; ++i)
            result |= uint32_t(shared_ram_[offset + i]) << (i * 8);
        *value = result;
        return true;
    }
    if (p >= 0x1f140000 && p + size <= 0x1f141000) {
        // MAME maps an AT28C16 here on the low byte lane. No NVRAM image is
        // supplied by this ROM set, so the initial device contents are erased.
        *value = size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;
        return true;
    }
    if (p >= 0x1f000000 && p <= 0x1f000003) {
        *value = 0xffffffff;
        return true;
    }
    if ((p >= 0x1f008000 && p <= 0x1f008003) ||
        (p >= 0x1f010000 && p <= 0x1f010003) ||
        (p >= 0x1f018000 && p <= 0x1f018003) ||
        (p >= 0x1f1bff08 && p <= 0x1f1bff0f) ||
        (p >= 0x1f700000 && p <= 0x1f70ffff)) {
        *value = 0xffffffff;
        return true;
    }
    return false;
}

bool NamcoSystem12Board::write(uint32_t address, unsigned size, uint32_t value) {
    if (size != 1 && size != 2 && size != 4) return false;
    const uint32_t p = address & 0x1fffffff;
    if (p >= 0x1f000000 && p <= 0x1f000003) {
        bank_ = static_cast<uint16_t>(value);
        return true;
    }
    if (p >= 0x1f080000 && p + size <= 0x1f084000) {
        const size_t offset = p - 0x1f080000;
        for (unsigned i = 0; i < size; ++i)
            shared_ram_[offset + i] = static_cast<uint8_t>(value >> (i * 8));
        return true;
    }
    if (p >= 0x1f140000 && p + size <= 0x1f141000) return true;
    if (p >= 0x1f700000 && p <= 0x1f70ffff) {
        const uint32_t word_offset = (p - 0x1f700000) & 0xffff;
        dma_offset_ = word_offset | ((value & 0xffff) << 16);
        return true;
    }
    if ((p >= 0x1f008000 && p <= 0x1f008003) ||
        (p >= 0x1f010000 && p <= 0x1f010003) ||
        (p >= 0x1f018000 && p <= 0x1f018003) ||
        (p >= 0x1f1bff08 && p <= 0x1f1bff0f) ||
        (p >= 0x1fa00000 && p <= 0x1fbfffff)) return true;
    return false;
}

std::vector<uint32_t> NamcoSystem12Board::dma_rom_read(size_t words) {
    const bool main_rom = (dma_offset_ & 0x80000000u) != 0;
    const auto &rom = roms_->at(main_rom ? "bios" : "program");
    size_t offset = main_rom ? size_t(dma_offset_ & 0x00ffffffu)
                             : size_t(dma_offset_ & 0x7fffffffu) + size_t(bank_) * 0x200000;
    std::vector<uint32_t> result;
    result.reserve(words);
    for (size_t i = 0; i < words; ++i, offset += 4) {
        if (offset + 4 > rom.size()) break;
        result.push_back(uint32_t(rom[offset]) |
                         (uint32_t(rom[offset + 1]) << 8) |
                         (uint32_t(rom[offset + 2]) << 16) |
                         (uint32_t(rom[offset + 3]) << 24));
    }
    // MAME's namcos12_rom_read uses the programmed DMA source offset as a
    // stable base for each transfer callback; the DMA engine advances its
    // destination address, while this register is changed only by a write.
    return result;
}

} // namespace mamerecomp::namco::system12
