#include "cps1_state.hpp"

namespace multiarcaderecomp::cps1::detail {

std::array<uint8_t, 0x400000> g_program{};
std::array<uint8_t, 0x10000> g_workram{};
std::array<uint16_t, 0x20> g_cps_a{};
std::array<uint16_t, 0x20> g_cps_b{};
std::array<uint16_t, 0xc00> g_palette_ram{};
std::vector<uint8_t> g_gfx_rom;
std::array<uint8_t, 0x800> g_buffered_obj{};
size_t g_program_size = 0;
std::uint32_t g_main_clock_hz = 12'000'000;
std::uint32_t g_sound_clock_hz = 3'579'545;
std::uint32_t g_pixel_clock_hz = 8'000'000;
std::uint16_t g_htotal = 512;
std::uint16_t g_vtotal = 262;
std::uint16_t g_vblank_start = 240;
const Cps1CpsBProfile* g_active_cps_b_profile = nullptr;
bool g_gfxram_is_executable = false;
bool g_reverse_sprite_order = false;
int64_t g_next_vblank = static_cast<int64_t>(g_main_clock_hz) * g_htotal * g_vblank_start / g_pixel_clock_hz;
bool g_irq1_pending = false;
bool g_trace_vblank = false;
uint64_t g_vblank_count = 0;

} // namespace multiarcaderecomp::cps1::detail
