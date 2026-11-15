#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cps1_driver.hpp"

namespace multiarcaderecomp::cps1::detail {

extern std::array<uint8_t, 0x400000> g_program;
extern std::array<uint8_t, 0x10000> g_workram;
extern std::array<uint16_t, 0x20> g_cps_a;
extern std::array<uint16_t, 0x20> g_cps_b;
extern std::array<uint16_t, 0xc00> g_palette_ram;
extern std::vector<uint8_t> g_gfx_rom;
extern std::array<uint8_t, 0x800> g_buffered_obj;
extern size_t g_program_size;
extern bool g_irq1_pending;
extern int64_t g_next_vblank;
extern bool g_trace_vblank;
extern uint64_t g_vblank_count;
extern std::uint32_t g_main_clock_hz;
extern std::uint32_t g_sound_clock_hz;
extern std::uint32_t g_pixel_clock_hz;
extern std::uint16_t g_htotal;
extern std::uint16_t g_vtotal;
extern std::uint16_t g_vblank_start;
extern const Cps1CpsBProfile* g_active_cps_b_profile;
extern bool g_gfxram_is_executable;
extern bool g_reverse_sprite_order;

void configure_game(const Cps1GameDriver& driver);
std::int64_t cps1_frame_cycles();
std::int64_t cps1_vblank_cycles();

} // namespace multiarcaderecomp::cps1::detail
