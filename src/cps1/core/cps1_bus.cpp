#include "cps1_bus.hpp"
#include "cps1_state.hpp"
#include "cps1_gfxram.hpp"
#include "cps1_inputs.hpp"
#include "cps1_cpsa.hpp"
#include "cps1_cpsb.hpp"
#include "cps1_video.hpp"
#include "cps1_sound_ym_oki.hpp"
#include "cps1_sound_qsound.hpp"
#include "cps1_eeprom.hpp"

#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>
#include "cpus/m68000/runtime/cpu_runtime.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

extern "C" uint32_t g_current_guest_pc;
extern "C" {
uint8_t* ng_rmap[256]{};
uint8_t* ng_wmap[256]{};
int ng_protection_active = 0;
}

namespace mamerecomp::cps1::detail {
namespace {
uint64_t g_reads = 0;
uint64_t g_writes = 0;
}

uint16_t bus_read16(uint32_t address) {
    address &= 0x00fffffe;
    ++g_reads;
    // CPS-1 CPU-space autovector acknowledge. MAME clears IPL1/2 on reads in
    // the FC0-FC2 window; keep the same side effect for the native bus.
    if (address >= 0xfffff2 && address <= 0xfffffe) {
        g_irq1_pending = false;
        ng_cpu.ipl = 0;
        // MAME maps this as autovector(offset + 1): $fffff2 acknowledges
        // level 1 (vector 25), $fffff4 level 2 (vector 26), etc.
        return static_cast<uint16_t>(0x19 + ((address - 0xfffff2) >> 1));
    }
    if (address >= 0x800000 && address <= 0x800006) {
        const std::uint16_t value = read_player_inputs();
        if (std::getenv("MAMERECOMP_CPS1_TRACE_INPUT")) {
            static std::uint16_t previous = 0xffff;
            if (value != previous) {
                std::cerr << "[cps1-input] 68000 IN1 read=$" << std::hex << value << std::dec << '\n';
                previous = value;
            }
        }
        return value;
    }
    if (address >= 0x800018 && address <= 0x80001e) {
        const unsigned offset = (address - 0x800018) >> 1;
        const std::uint16_t value = static_cast<std::uint16_t>((read_system_dip(offset) << 8) | 0xff);
        if (offset == 0 && std::getenv("MAMERECOMP_CPS1_TRACE_INPUT")) {
            static std::uint16_t previous = 0xffff;
            if (value != previous) {
                std::cerr << "[cps1-input] 68000 IN0 read=$" << std::hex << value << std::dec << '\n';
                previous = value;
            }
        }
        return value;
    }
    if (address >= 0x800140 && address <= 0x80017e) {
        const unsigned offset = (address - 0x800140) >> 1;
        // CPS-B-21 (CPS_B_21_DEF in MAME): two factor registers feed a 32-bit
        // multiply result at offsets $04/$06. MAME returns $ffff for other
        // unimplemented read ports rather than the last value written there.
        return cpsb_read(offset);
    }
    // MAME maps CPS-A registers as write-only; reads return the unconnected
    // bus value rather than echoing the register state.
    if (address >= 0x800100 && address <= 0x80013e) return 0xffff;
    if (address >= 0x900000 && address <= 0x92fffe) {
        const size_t offset = address - 0x900000;
        return read_gfxram_word(offset);
    }
    if (qsound_is_active() && ((address >= 0xf18000 && address <= 0xf19ffe)
            || (address >= 0xf1e000 && address <= 0xf1fffe))) {
        // QSound's Z80 runs independently of sound-command writes. Advance it
        // while the 68000 polls the shared Q RAM, as MAME's scheduler does.
        sync_sound_to_main_clock();
        return qsound_main_read(address);
    }
    if (qsound_is_active() && address == 0xf1c000) return read_input2();
    if (qsound_is_active() && address == 0xf1c002) return read_input3();
    if (qsound_is_active() && address == 0xf1c006)
        return static_cast<std::uint16_t>(cps1_eeprom_read_do() ? 1 : 0);
    if (address >= 0xff0000 && address <= 0xfffffe) {
        const size_t offset = address - 0xff0000;
        return static_cast<uint16_t>((g_workram[offset] << 8) | g_workram[offset + 1]);
    }
    if (address < g_program.size())
        return static_cast<uint16_t>((g_program[address] << 8) | g_program[address + 1]);
    return 0xffff;
}

void bus_write16(uint32_t address, uint16_t value) {
    address &= 0x00fffffe;
    ++g_writes;
    if (qsound_is_active() && ((address >= 0xf18000 && address <= 0xf19ffe)
            || (address >= 0xf1e000 && address <= 0xf1fffe))) {
        sync_sound_to_main_clock();
        qsound_main_write(address, value);
        return;
    }
    if (qsound_is_active() && address >= 0x800030 && address <= 0x800036) {
        cps1_coin_counter_write(0, (value & 0x0100) != 0);
        cps1_coin_counter_write(1, (value & 0x0200) != 0);
        cps1_coin_lockout_write(0, (value & 0x0400) == 0);
        cps1_coin_lockout_write(1, (value & 0x0800) == 0);
        return;
    }
    if (qsound_is_active() && address == 0xf1c004) {
        cps1_coin_counter_write(2, (value & 0x01) != 0);
        cps1_coin_lockout_write(2, (value & 0x02) == 0);
        cps1_coin_counter_write(3, (value & 0x04) != 0);
        cps1_coin_lockout_write(3, (value & 0x08) == 0);
        return;
    }
    if (qsound_is_active() && address == 0xf1c006) {
        cps1_eeprom_write_lines(value & 0xff);
        return;
    }
    if (address >= 0x800100 && address <= 0x80013e) {
        const unsigned offset = (address - 0x800100) >> 1;
        cpsa_write(offset, value);
        return;
    }
    if (address >= 0x800140 && address <= 0x80017e) {
        const unsigned offset = (address - 0x800140) >> 1;
        cpsb_write(offset, value);
        return;
    }
    if (address >= 0x800180 && address <= 0x800186) {
        sync_sound_to_main_clock();
        write_sound_command(static_cast<uint8_t>(value));
        return;
    }
    if (address >= 0x800188 && address <= 0x80018e) {
        sync_sound_to_main_clock();
        write_sound_timer_command(static_cast<uint8_t>(value));
        return;
    }
    if (address >= 0x900000 && address <= 0x92fffe) {
        const size_t offset = address - 0x900000;
        write_gfxram_word(offset, value);
        return;
    }
    if (address >= 0xff0000 && address <= 0xfffffe) {
        const size_t offset = address - 0xff0000;
        if (address == 0xff804e && std::getenv("MAMERECOMP_CPS1_TRACE_LAYER_CONTROL"))
            std::cerr << "[cps1] layer-state cycle=" << ng_cycles << " PC=$" << std::hex
                      << g_current_guest_pc << " workram=$" << value << std::dec << '\n';
        g_workram[offset] = static_cast<uint8_t>(value >> 8);
        g_workram[offset + 1] = static_cast<uint8_t>(value);
    }
}

void initialize_bus(std::span<const uint8_t> image) {
    mame_recomp_m68k_set_exception_bus(ng_r16_slow, ng_w16_slow);
    g_program_size = image.size();
    g_program.fill(0xff);
    std::copy_n(image.begin(), std::min(image.size(), g_program.size()), g_program.begin());
    for (unsigned page = 0; page < 0x40; ++page) ng_rmap[page] = g_program.data() + page * 0x10000;
    for (unsigned page = 0x90; page <= 0x92; ++page) {
        const size_t offset = (page - 0x90) * 0x10000;
        ng_rmap[page] = g_gfxram.data() + offset;
        ng_wmap[page] = g_gfxram_is_executable ? nullptr : g_gfxram.data() + offset;
    }
    ng_rmap[0xff] = g_workram.data();
    ng_wmap[0xff] = std::getenv("MAMERECOMP_CPS1_TRACE_LAYER_CONTROL")
        ? nullptr : g_workram.data();
    cpsa_reset();
    cpsb_reset();
    cps1_eeprom_reset();
}

uint64_t bus_reads() { return g_reads; }
uint64_t bus_writes() { return g_writes; }

} // namespace mamerecomp::cps1::detail

using namespace mamerecomp::cps1::detail;

extern "C" {
uint8_t ng_r8_slow(uint32_t address) {
    const uint16_t word = bus_read16(address);
    return (address & 1) ? static_cast<uint8_t>(word) : static_cast<uint8_t>(word >> 8);
}
uint16_t ng_r16_slow(uint32_t address) { return bus_read16(address); }
void ng_w8_slow(uint32_t address, uint8_t value) {
    const uint16_t old = bus_read16(address & ~1U);
    bus_write16(address & ~1U, (address & 1) ? static_cast<uint16_t>((old & 0xff00) | value)
                                              : static_cast<uint16_t>((old & 0x00ff) | (value << 8)));
}
void ng_w16_slow(uint32_t address, uint16_t value) { bus_write16(address, value); }

} // extern "C"
