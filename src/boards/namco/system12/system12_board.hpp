#pragma once

#include "rom/rom_regions.hpp"
#include "psx_aot_abi.h"
#include "chips/c352.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mamerecomp::namco::system12 {

// Namco System 12 COH700 main-board mappings from MAME's namcos12.cpp.
// The PSX SoC is shared with ZN, while ROM banking, shared RAM and DMA ROM
// source selection remain board-specific.
class NamcoSystem12Board {
public:
    explicit NamcoSystem12Board(const mamerecomp::rom::RomRegions& roms);
    PsxBus make_bus();
    std::vector<uint32_t> dma_rom_read(size_t words);
    bool sub_read(uint32_t address, unsigned size, uint32_t *value);
    bool sub_write(uint32_t address, unsigned size, uint32_t value);
    uint16_t c352_read(uint16_t word_offset) const { return c352_.read(word_offset); }
    void c352_write(uint16_t word_offset, uint16_t data, uint16_t mem_mask = 0xffff) {
        c352_.write(word_offset, data, mem_mask);
    }
    std::vector<int16_t> render_audio(size_t frames) { return c352_.render(frames); }
    uint32_t audio_sample_rate() const { return c352_.sample_rate(); }
    unsigned active_audio_voices() const { return c352_.active_voices(); }
    uint64_t c352_writes() const { return c352_writes_; }
    uint16_t bank() const { return bank_; }
private:
    uint8_t h8_read8(uint32_t address);
    void h8_write8(uint32_t address, uint8_t value);
    uint8_t h8_port_read(unsigned port) const;
    uint8_t h8_port_input(unsigned port) const;
    static bool read_cb(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool write_cb(void *, uint32_t, unsigned, uint32_t, uint32_t);
    bool read(uint32_t, unsigned, uint32_t *);
    bool write(uint32_t, unsigned, uint32_t);
    const mamerecomp::rom::RomRegions* roms_;
    mamerecomp::chips::C352 c352_;
    uint16_t bank_ = 0;
    uint32_t dma_offset_ = 0;
    std::vector<uint8_t> shared_ram_ = std::vector<uint8_t>(0x10000);
    std::array<uint8_t, 0x200> h8_internal_ram_{};
    std::array<uint8_t, 16> h8_port_ddr_ = [] {
        std::array<uint8_t, 16> value{};
        value[6] = 0x80; // H8/3002 port 6 reset DDR from MAME configuration.
        value[8] = 0xf0; // Port 8 reset DDR from MAME configuration.
        return value;
    }();
    std::array<uint8_t, 16> h8_port_dr_{};
    uint8_t h8_port8_latch_ = 0;
    uint8_t h8_porta_latch_ = 0;
    uint8_t h8_portb_latch_ = 0x50;
    uint8_t h8_port4_pcr_ = 0;
    uint8_t h8_syscr_ = 0x09;
    uint64_t c352_writes_ = 0;
};


} // namespace mamerecomp::namco::system12
