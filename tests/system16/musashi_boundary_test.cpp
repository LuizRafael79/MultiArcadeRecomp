#include "cpus/m68000/runtime/musashi_fallback.h"
#include "cpus/m68000/runtime/recomp_bus.h"
#include <neogeorecomp/cpu.h>

#include <array>
#include <cassert>
#include <cstdint>

extern "C" { int ng_protection_active = 0; }

namespace {
std::array<std::uint8_t, 256> memory{};
bool shorten_event = false;
bool invalidate_on_fetch = false;
bool aot_valid = true;

bool read8(void*, std::uint32_t address, std::uint8_t* value) {
    if (address >= memory.size()) return false;
    *value = memory[address];
    return true;
}
bool read16(void*, std::uint32_t address, std::uint16_t* value) {
    if (address + 1 >= memory.size()) return false;
    *value = static_cast<std::uint16_t>((memory[address] << 8) | memory[address + 1]);
    if (address == 0 && shorten_event) ng_next_event = 4;
    if (address == 0 && invalidate_on_fetch) aot_valid = false;
    return true;
}
bool write8(void*, std::uint32_t address, std::uint8_t value) {
    if (address >= memory.size()) return false;
    memory[address] = value;
    return true;
}
bool write16(void*, std::uint32_t address, std::uint16_t value) {
    if (address + 1 >= memory.size()) return false;
    memory[address] = static_cast<std::uint8_t>(value >> 8);
    memory[address + 1] = static_cast<std::uint8_t>(value);
    return true;
}
void reset_cpu() {
    ng_cpu = {};
    ng_cpu.s = 1;
    ng_cpu.a[7] = 0x80;
    ng_cycles = 0;
    ng_next_event = 100;
    aot_valid = true;
    shorten_event = false;
    invalidate_on_fetch = false;
}
}

int main() {
    // NOP followed by MOVEQ #1,D0, which must remain pending at the boundary.
    memory[0] = 0x4e; memory[1] = 0x71;
    memory[2] = 0x70; memory[3] = 0x01;
    M68kBusCallbacks bus{nullptr, read8, read16, write8, write16};
    mamerecomp_m68k_bus_bind(&bus);

    const std::uint32_t translated_pc = 2;
    std::uint64_t instructions = 0;
    reset_cpu();
    int cycles = mamerecomp_m68k_musashi_run_until(&translated_pc, 1, &aot_valid, &instructions);
    assert(cycles == 4 && instructions == 1 && ng_cpu.pc == 2 && ng_cpu.d[0] == 0);
    assert(mamerecomp_m68k_musashi_step() == 4 && ng_cpu.pc == 4 && ng_cpu.d[0] == 1);

    reset_cpu();
    shorten_event = true;
    cycles = mamerecomp_m68k_musashi_run_until(nullptr, 0, nullptr, &instructions);
    assert(cycles == 4 && instructions == 1 && ng_cpu.pc == 2 && ng_cpu.d[0] == 0);

    reset_cpu();
    invalidate_on_fetch = true;
    cycles = mamerecomp_m68k_musashi_run_until(nullptr, 0, &aot_valid, &instructions);
    assert(cycles == 4 && instructions == 1 && ng_cpu.pc == 2 && ng_cpu.d[0] == 0);
}
