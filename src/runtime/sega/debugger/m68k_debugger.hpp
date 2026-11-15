#pragma once

#include "boards/sega/common/sega_address_space.hpp"

#include <cstdint>
#include <filesystem>

namespace multiarcaderecomp {

void configure_m68k_debugger(bool enabled, SegaAddressSpace* bus, const std::uint64_t* vblank_count,
    const std::filesystem::path* trace_path = nullptr, std::uint64_t trace_start = 0,
    std::uint64_t trace_end = 0);
bool m68k_debugger_quit_requested();
void report_m68k_debug_snapshot(std::uint64_t vblank, std::int64_t cycles);
void m68k_debugger_note_write(std::uint32_t address, std::uint8_t width, std::uint32_t value);
void m68k_debugger_note_read(std::uint32_t address, std::uint8_t width, std::uint32_t value);

} // namespace multiarcaderecomp
