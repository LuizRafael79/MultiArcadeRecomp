#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>
#include "native_z80.h"
#include "z80.h"
#include "m68k.h"
#include "native_registry.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef MAMERECOMP_CPS1_SDL
#include <SDL3/SDL.h>
#endif

extern "C" {
extern const ng_func_entry_t sf2ceng_recomp_table[];
extern const size_t sf2ceng_recomp_count;

ng_cpu_t ng_cpu{};
int64_t ng_cycles = 0;
int64_t ng_next_event = 0;
uint64_t ng_icount = 0;
ng_debug_instruction_hook_t ng_debug_instruction_hook = nullptr;
uint32_t g_current_guest_pc = 0;
uint8_t* ng_rmap[256]{};
uint8_t* ng_wmap[256]{};
int ng_protection_active = 0;
}

namespace {
constexpr uint32_t kAddressMask = 0x00ffffff;
// SF2CE's standard CPS1 configuration in MAME: 68000 at 12 MHz and Z80 /
// YM2151 at 3.579545 MHz. The OKI clock is 1 MHz (16 MHz / 4 / 4); OKI and
// YM2151 are not yet clocked as devices in this visual prototype.
constexpr int64_t kCps1M68kClock = 12'000'000;
constexpr uint64_t kCps1Z80Clock = 3'579'545;
constexpr int64_t kCps1FrameCycles = kCps1M68kClock * 512 * 262 / 8'000'000;
constexpr int64_t kCps1VblankCycle = kCps1M68kClock * 512 * 240 / 8'000'000;
std::array<uint8_t, 0x400000> g_program;
// The CPU exposes 0x30000 bytes, while CPS-A base registers select from a
// 0x40000-byte aperture (MAME masks the address with 0x3ffff).
std::array<uint8_t, 0x40000> g_gfxram{};
std::array<uint8_t, 0x10000> g_workram{};
std::array<uint16_t, 0x20> g_cps_a{};
std::array<uint16_t, 0x20> g_cps_b{};
std::array<uint16_t, 0xc00> g_palette_ram{};
// CPS1 main_map exposes the player port across $800000-$800007 and the
// system/DIP bytes at $800018-$80001f (returned in the high byte, low byte $ff).
uint16_t g_player_inputs = 0xffff;
std::array<uint8_t, 4> g_system_dips{0xff, 0xff, 0xfc, 0x9c}; // IN0, DSWA, DSWB, DSWC defaults from MAME's sf2 input ports
std::unordered_map<uint32_t, ng_func_t> g_native;
uint64_t g_fallback_instructions = 0;
uint64_t g_native_blocks = 0;
uint64_t g_reads = 0;
uint64_t g_writes = 0;
uint32_t g_last_fallback_pc = 0;
size_t g_program_size = 0;
std::unordered_set<uint32_t> g_dynamic_entries;
bool g_in_fallback_sequence = false;
int64_t g_next_vblank = kCps1VblankCycle;
bool g_irq1_pending = false;
std::vector<uint8_t> g_z80_fixed, g_z80_banked;
std::vector<uint8_t> g_gfx_rom;
std::array<uint8_t, 0x800> g_z80_ram{};
std::array<uint8_t, 0x800> g_buffered_obj{};
uint8_t g_sound_command = 0xff;
uint8_t g_sound_timer_command = 0xff;
uint8_t g_sound_bank = 0;
uint8_t g_oki_pin7 = 1;
uint8_t g_ym_address = 0;
std::array<uint8_t, 256> g_ym_registers{};
std::array<uint64_t, 0x8000> g_pc_profile{};
bool g_profile_pc = false;
bool g_trace_trap = false;
uint64_t g_trap_count = 0;
bool g_trace_vblank = false;
uint64_t g_vblank_count = 0;

void profile_guest_pc(uint32_t pc) {
    g_current_guest_pc = pc;
    if (g_profile_pc && pc < 0x10000)
        ++g_pc_profile[pc >> 1];
    if (g_trace_trap && pc == 0x0005c4u) {
        std::cerr << "[cps1] TRAP14 n=" << ++g_trap_count << " D0-D7=" << std::hex;
        for (unsigned i = 0; i < 8; ++i) std::cerr << (i ? "," : "") << ng_cpu.d[i];
        std::cerr << " A0-A7=";
        for (unsigned i = 0; i < 8; ++i) std::cerr << (i ? "," : "") << ng_cpu.a[i];
        std::cerr << std::dec << '\n';
    }
    if (g_trace_trap && (pc == 0x000aa6u || pc == 0x000ab2u || pc == 0x0000c0u || pc == 0x000232u)) {
        const uint32_t sp = ng_cpu.a[7];
        std::cerr << "[cps1] boot-flow PC=$" << std::hex << pc << " SR=$" << ng_get_sr()
                  << " SP=$" << sp << " stack=$" << ng_r16(sp) << ",$" << ng_r32(sp + 2)
                  << " D0=$" << ng_cpu.d[0] << " D2=$" << ng_cpu.d[2]
                  << " D3=$" << ng_cpu.d[3] << " D4=$" << ng_cpu.d[4]
                  << " A4=$" << ng_cpu.a[4] << " A5=$" << ng_cpu.a[5]
                  << std::dec << '\n';
    }
}
#ifdef MAMERECOMP_CPS1_SDL
SDL_Window* g_cps1_window = nullptr;
SDL_Renderer* g_cps1_renderer = nullptr;
SDL_Texture* g_cps1_texture = nullptr;
bool g_cps1_quit = false;
bool g_live_window_enabled = false;

void present_live_frame(const std::vector<uint32_t>& frame) {
    static uint64_t next_frame_ns = 0;
    if (!g_cps1_window) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return;
        g_cps1_window = SDL_CreateWindow("CPS-1 MameRecomp", 768, 448, SDL_WINDOW_RESIZABLE);
        if (!g_cps1_window) return;
        g_cps1_renderer = SDL_CreateRenderer(g_cps1_window, nullptr);
        g_cps1_texture = g_cps1_renderer ? SDL_CreateTexture(g_cps1_renderer, SDL_PIXELFORMAT_ARGB8888,
                                                               SDL_TEXTUREACCESS_STREAMING, 384, 224) : nullptr;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT) g_cps1_quit = true;
    if (g_cps1_quit || !g_cps1_renderer || !g_cps1_texture) return;
    SDL_UpdateTexture(g_cps1_texture, nullptr, frame.data(), 384 * sizeof(uint32_t));
    SDL_SetRenderDrawColor(g_cps1_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_cps1_renderer);
    SDL_FRect dst{0, 0, 768, 448};
    SDL_RenderTexture(g_cps1_renderer, g_cps1_texture, nullptr, &dst);
    SDL_RenderPresent(g_cps1_renderer);
    // CPS1 runs at the 8 MHz pixel clock with 512x262 total pixels. Keep the
    // interactive window at that hardware cadence instead of letting AOT code
    // sprint through emulated time as fast as the host can execute it.
    constexpr uint64_t frame_period_ns =
        static_cast<uint64_t>(512) * 262 * 1'000'000'000ULL / 8'000'000;
    const uint64_t now = SDL_GetTicksNS();
    if (next_frame_ns == 0) next_frame_ns = now;
    next_frame_ns += frame_period_ns;
    if (next_frame_ns > now) SDL_DelayNS(next_frame_ns - now);
    else next_frame_ns = now;
}
#endif

class Cps1SoundBus final : public mamerecomp::z80::Z80Bus {
public:
    uint8_t Read(uint16_t address) override {
        if (address < 0x8000) return address < g_z80_fixed.size() ? g_z80_fixed[address] : 0xff;
        if (address < 0xc000) {
            const size_t offset = static_cast<size_t>(g_sound_bank) * 0x4000 + (address - 0x8000);
            return offset < g_z80_banked.size() ? g_z80_banked[offset] : 0xff;
        }
        if (address >= 0xd000 && address <= 0xd7ff) return g_z80_ram[address - 0xd000];
        if (address == 0xf000 || address == 0xf001) return 0; // YM2151 status is wired in the next audio step
        if (address == 0xf002) return 0; // OKI busy status
        if (address == 0xf008) return g_sound_command;
        if (address == 0xf00a) return g_sound_timer_command;
        return 0xff;
    }
    void Write(uint16_t address, uint8_t value) override {
        if (address >= 0xd000 && address <= 0xd7ff) g_z80_ram[address - 0xd000] = value;
        else if (address == 0xf000) g_ym_address = value;
        else if (address == 0xf001) g_ym_registers[g_ym_address] = value;
        else if (address == 0xf002) {} // OKI MSM6295 commands are connected in the audio step
        else if (address == 0xf004) g_sound_bank = value & 1;
        else if (address == 0xf006) g_oki_pin7 = value & 1;
    }
    bool ChangesMapping(uint16_t address) override { return address == 0xf004; }
    uint32_t Mapping() override { return g_sound_bank; }
    void SetMapping(uint32_t mapping) override { g_sound_bank = static_cast<uint8_t>(mapping & 1); }
};
Cps1SoundBus g_sound_bus;
mamerecomp::z80::Z80 g_sound_cpu(g_sound_bus);
bool g_sound_cpu_active = false;

void sync_z80_to_main_clock() {
    if (!g_sound_cpu_active || ng_cycles <= 0) return;
    const uint64_t target = static_cast<uint64_t>(ng_cycles) * kCps1Z80Clock
        / static_cast<uint64_t>(kCps1M68kClock);
    if (target > g_sound_cpu.cycles)
        g_sound_cpu.Run(target, mamerecomp::z80::Z80::Mode::Native);
}

uint16_t gfxram_word(size_t byte_offset) {
    byte_offset %= g_gfxram.size();
    return static_cast<uint16_t>((g_gfxram[byte_offset] << 8)
        | g_gfxram[(byte_offset + 1) % g_gfxram.size()]);
}

size_t video_base(unsigned register_index, size_t boundary);

void copy_cps1_palette() {
    const size_t base = video_base(5, 0x400);
    const uint16_t control = g_cps_b[0x18];
    size_t source_page = 0;
    g_palette_ram.fill(0);
    for (unsigned page = 0; page < 6; ++page) {
        if ((control & (1U << page)) == 0) {
            if (source_page != 0) ++source_page;
            continue;
        }
        for (unsigned entry = 0; entry < 0x200; ++entry)
            g_palette_ram[page * 0x200 + entry] = gfxram_word(base + source_page * 0x400 + entry * 2);
        ++source_page;
    }
}

size_t video_base(unsigned register_index, size_t boundary) {
    size_t base = static_cast<size_t>(g_cps_a[register_index]) * 256;
    base &= ~(boundary - 1);
    return base & 0x3ffff;
}

int map_gfx_code(unsigned type, uint32_t code) {
    const unsigned shift = type == 1 ? 0 : type == 3 ? 3 : 1;
    code <<= shift;
    unsigned bank = 0;
    bool valid = false;
    if (type == 0) {
        if (code <= 0x07fff) { bank = 0; valid = true; }
        else if (code <= 0x0ffff) { bank = 1; valid = true; }
        else if (code <= 0x11fff) { bank = 2; valid = true; }
    } else if (type == 1 && code >= 0x04000 && code <= 0x04fff) {
        bank = 2; valid = true;
    } else if (type == 2 && code >= 0x05000 && code <= 0x07fff) {
        bank = 2; valid = true;
    } else if (type == 3 && code >= 0x02000 && code <= 0x03fff) {
        bank = 2; valid = true;
    }
    return valid ? static_cast<int>((bank * 0x8000 + (code & 0x7fff)) >> shift) : -1;
}

std::unordered_map<uint32_t, std::vector<uint8_t>> g_decoded_tiles;
uint64_t g_video_tile_hits = 0;
uint64_t g_video_nonzero_pens = 0;

const std::vector<uint8_t>* decoded_tile(unsigned type, unsigned size, unsigned code, unsigned side = 0) {
    const uint32_t key = (type << 28) | (size << 24) | (side << 23) | code;
    auto found = g_decoded_tiles.find(key);
    if (found != g_decoded_tiles.end()) return &found->second;
    // These increments match MAME's CPS-1 gfx layouts: 8x8 characters occupy
    // 64-byte slots, 16x16 occupy 128 bytes, and 32x32 occupy 512 bytes.
    const size_t bytes_per_tile = size == 8 ? 64 : static_cast<size_t>(size) * size / 2;
    const size_t base = static_cast<size_t>(code) * bytes_per_tile;
    if (g_gfx_rom.empty() || base + bytes_per_tile > g_gfx_rom.size()) return nullptr;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size);
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
                const unsigned x_bits = (x / 8) * 32 + (x & 7)
                    + ((size == 8 && side) ? 32 : 0);
            unsigned value = 0;
            for (unsigned plane = 0; plane < 4; ++plane) {
                const unsigned row_bits = size == 8 ? 64 : size * 4;
                const unsigned bit = static_cast<unsigned>(base * 8) + y * row_bits + x_bits + 24 - plane * 8;
                value |= ((g_gfx_rom[bit / 8] >> (7 - (bit & 7))) & 1U) << (3 - plane);
            }
            pixels[y * size + x] = static_cast<uint8_t>(value);
        }
    }
    return &g_decoded_tiles.emplace(key, std::move(pixels)).first->second;
}

std::array<uint32_t, 0xc00> make_palette() {
    std::array<uint32_t, 0xc00> palette{};
    const size_t base = video_base(5, 0x400);
    const uint16_t control = g_cps_b[0x18];
    for (unsigned page = 0; page < 6; ++page) {
        if ((control & (1U << page)) == 0) continue;
        for (unsigned entry = 0; entry < 0x200; ++entry) {
            const uint16_t value = g_palette_ram[page * 0x200 + entry];
            const unsigned brightness = 0x0f + (((value >> 12) & 0x0f) << 1);
            const unsigned red = ((value >> 8) & 0x0f) * 0x11 * brightness / 0x2d;
            const unsigned green = ((value >> 4) & 0x0f) * 0x11 * brightness / 0x2d;
            const unsigned blue = (value & 0x0f) * 0x11 * brightness / 0x2d;
            palette[page * 0x200 + entry] = 0xff000000U | (red << 16) | (green << 8) | blue;
        }
    }
    return palette;
}

void render_tile_layer(std::vector<uint32_t>& frame, const std::array<uint32_t, 0xc00>& palette,
                       unsigned layer, std::vector<uint8_t>* sprite_block_mask = nullptr) {
    const unsigned size = layer == 1 ? 8 : layer == 2 ? 16 : 32;
    const unsigned type = layer;
    const size_t base = video_base(layer, 0x4000);
    const uint16_t video_control = g_cps_a[0x11];
    const size_t other_base = video_base(4, 0x800);
    const unsigned row_scroll_offset = g_cps_a[0x10];
    // MAME's SF2CE board profile has zero horizontal kludge offsets. The
    // driver's exceptional CPS1 conversions select per-game offsets; do not
    // bake the common bootleg -0x10 adjustment into the native renderer.
    const int scroll_x = static_cast<int16_t>(g_cps_a[4 + layer * 2]);
    const int scroll_y = static_cast<int16_t>(g_cps_a[5 + layer * 2]);
    const unsigned map_mask = size * 64 - 1;
    for (int y = 0; y < 224; ++y) {
        const unsigned screen_y = static_cast<unsigned>(y + 16);
        const unsigned world_y = (screen_y + scroll_y) & map_mask;
        int line_scroll_x = scroll_x;
        // CPS-A row-scroll RAM supplies one extra Scroll-2 horizontal offset
        // per raster line when video-control bit 0 is set. MAME indexes the
        // row by the physical 0..255 scanline and adds ROWSCROLL_OFFS to the
        // 1K-word OTHER RAM address.
        if (layer == 2 && (video_control & 1)) {
            const unsigned row = (screen_y + row_scroll_offset) & 0x3ff;
            line_scroll_x += gfxram_word(other_base + row * 2);
        }
        const unsigned row = world_y / size;
        for (int x = 0; x < 384; ++x) {
            const unsigned world_x = static_cast<unsigned>((x + 64 + line_scroll_x) & map_mask);
            const unsigned col = world_x / size;
            size_t tile_index;
            if (layer == 1) tile_index = (row & 0x1f) + ((col & 0x3f) << 5) + ((row & 0x20) << 6);
            else if (layer == 2) tile_index = (row & 0x0f) + ((col & 0x3f) << 4) + ((row & 0x30) << 6);
            else tile_index = (row & 0x07) + ((col & 0x3f) << 3) + ((row & 0x38) << 6);
            const uint32_t raw_code = gfxram_word(base + tile_index * 4);
            const uint16_t attr = gfxram_word(base + tile_index * 4 + 2);
            const int code = map_gfx_code(type, layer == 3 ? raw_code & 0x3fff : raw_code);
            if (code < 0) continue;
            const auto* tile = decoded_tile(type, size, static_cast<unsigned>(code),
                layer == 1 ? ((tile_index >> 5) & 1) : 0);
            if (!tile) continue;
            unsigned local_x = world_x % size;
            unsigned local_y = world_y % size;
            if (attr & 0x20) local_x = size - 1 - local_x;
            if (attr & 0x40) local_y = size - 1 - local_y;
            const uint8_t pen = (*tile)[local_y * size + local_x];
            // MAME configures CPS1 tilemaps with bgmask=$8000: pen 15 is
            // transparent in the normal layer pass.
            if (pen == 15) continue;
            if (sprite_block_mask) {
                // SF2CE's CPS_B_21_DEF uses priority registers $28/$2a/$2c/$2e.
                // Their set bits are the opaque high-priority pens; MAME
                // exposes those through TILEMAP_DRAW_LAYER0 to the sprite
                // priority bitmap (priority value 1, hence sprite mask $02).
                constexpr std::array<unsigned, 4> priority_registers{0x14, 0x15, 0x16, 0x17};
                const unsigned group = (attr >> 7) & 3;
                const bool high = (g_cps_b[priority_registers[group]] & (1U << pen)) != 0;
                if (high) (*sprite_block_mask)[y * 384 + x] = 1;
                continue;
            }
            ++g_video_nonzero_pens;
            const unsigned color = (attr & 0x1f) + layer * 0x20;
            // MAME's gfxdecode color base is zero for all three tilemaps.
            // Their palette pages are already encoded by color = attr +
            // 0x20/0x40/0x60, so adding a per-layer page here double-shifts it.
            const size_t palette_index = color * 16 + pen;
            frame[y * 384 + x] = palette[palette_index];
        }
    }
}

void draw_sprite_tile(std::vector<uint32_t>& frame, const std::array<uint32_t, 0xc00>& palette,
                      const std::vector<uint8_t>& sprite_block_mask, int code, unsigned color,
                      bool flip_x, bool flip_y, int origin_x, int origin_y) {
    if (code < 0) return;
    const auto* tile = decoded_tile(0, 16, static_cast<unsigned>(code));
    if (!tile) return;
    for (unsigned ty = 0; ty < 16; ++ty) {
        const int y = origin_y + static_cast<int>(ty);
        if (y < 0 || y >= 224) continue;
        for (unsigned tx = 0; tx < 16; ++tx) {
            const int x = origin_x + static_cast<int>(tx);
            if (x < 0 || x >= 384) continue;
            if (sprite_block_mask[y * 384 + x]) continue;
            const unsigned sx = flip_x ? 15 - tx : tx;
            const unsigned sy = flip_y ? 15 - ty : ty;
            const uint8_t pen = (*tile)[sy * 16 + sx];
            if (pen != 15) frame[y * 384 + x] = palette[color * 16 + pen];
        }
    }
}

void render_sprites(std::vector<uint32_t>& frame, const std::array<uint32_t, 0xc00>& palette,
                    const std::vector<uint8_t>& sprite_block_mask) {
    const size_t base = video_base(0, 0x800);
    std::array<uint16_t, 0x400> words{};
    for (size_t i = 0; i < words.size(); ++i) {
        if (std::any_of(g_buffered_obj.begin(), g_buffered_obj.end(), [](uint8_t b) { return b != 0; }))
            words[i] = static_cast<uint16_t>((g_buffered_obj[i * 2] << 8) | g_buffered_obj[i * 2 + 1]);
        else words[i] = gfxram_word(base + i * 2);
    }
    size_t last = 0x3fc;
    for (size_t i = 3; i < words.size(); i += 4) {
        if ((words[i] & 0xff00) == 0xff00) { last = i >= 4 ? i - 4 : 0; break; }
    }
    // MAME walks the buffered object table from its first entry toward the
    // end marker (the priority order is significant for overlapping sprites).
    for (size_t i = 0; i <= last; i += 4) {
        const int x = words[i] & 0x1ff;
        const int y = words[i + 1] & 0x1ff;
        const uint32_t raw_code = words[i + 2];
        // The CPS1 PAL maps the sprite's base code once. MAME then expands
        // nx/ny block tiles in that mapped bank; mapping each child code
        // separately can jump banks partway through a multi-tile fighter.
        const int mapped_code = map_gfx_code(0, raw_code);
        if (mapped_code < 0) continue;
        const uint16_t attr = words[i + 3];
        const int nx = ((attr >> 8) & 0x0f) + 1;
        const int ny = ((attr >> 12) & 0x0f) + 1;
        const bool flip_x = (attr & 0x20) != 0, flip_y = (attr & 0x40) != 0;
        const unsigned color = attr & 0x1f;
        for (int sy = 0; sy < ny; ++sy) {
            for (int sx = 0; sx < nx; ++sx) {
                const uint32_t code = (static_cast<uint32_t>(mapped_code) & ~0x0fU)
                    + ((static_cast<uint32_t>(mapped_code)
                        + (flip_x ? nx - 1 - sx : sx)) & 0x0fU)
                    + 0x10U * (flip_y ? ny - 1 - sy : sy);
                const int sprite_x = (x + sx * 16) & 0x1ff;
                const int sprite_y = (y + sy * 16) & 0x1ff;
                draw_sprite_tile(frame, palette, sprite_block_mask, static_cast<int>(code), color, flip_x, flip_y,
                                 sprite_x - 64, sprite_y - 16);
            }
        }
    }
}

bool write_frame(const std::string& path) {
    if (g_gfx_rom.size() != 0x600000) return false;
    g_video_nonzero_pens = 0;
    const auto palette = make_palette();
    // MAME clears CPS1 output with palette pen $bff before drawing layers.
    std::vector<uint32_t> frame(384 * 224, palette[0xbff]);
    const uint16_t control = g_cps_b[0x13];
    const unsigned order[] = {unsigned((control >> 6) & 3), unsigned((control >> 8) & 3),
                              unsigned((control >> 10) & 3), unsigned((control >> 12) & 3)};
    const uint16_t layer_enable = g_cps_b[0x13];
    const uint16_t video_control = g_cps_a[0x11];
    // CPS-B-21 masks apply to tilemaps 1/2/3; sprite layer 0 is always
    // addressable when selected by the priority order.
    const unsigned enable_mask[] = {0x00, 0x02, 0x04, 0x08};
    std::vector<uint8_t> sprite_block_mask(384 * 224, 0);
    for (unsigned index = 0; index < std::size(order); ++index) {
        const unsigned layer = order[index];
        const bool enabled = layer != 0 && (layer_enable & enable_mask[layer])
            && (layer == 1 || (video_control & (1U << layer)));
        if (index != 0 && layer == 0) {
            const unsigned previous_layer = order[index - 1];
            if (previous_layer != 0 && (layer_enable & enable_mask[previous_layer])
                && (previous_layer == 1 || (video_control & (1U << previous_layer))))
                render_tile_layer(frame, palette, previous_layer, &sprite_block_mask);
        }
        if (layer == 0) {
            render_sprites(frame, palette, sprite_block_mask);
        } else if (enabled) {
            render_tile_layer(frame, palette, layer);
        }
    }
    if (const char* prefix = std::getenv("MAMERECOMP_CPS1_DUMP_LAYERS")) {
        const std::vector<uint8_t> no_sprite_mask(384 * 224, 0);
        for (unsigned layer = 0; layer <= 3; ++layer) {
            auto layer_frame = std::vector<uint32_t>(384 * 224, palette[0xbff]);
            if (layer == 0) render_sprites(layer_frame, palette, no_sprite_mask);
            else render_tile_layer(layer_frame, palette, layer);
            std::ofstream dump(std::string(prefix) + "-layer" + std::to_string(layer) + ".ppm",
                               std::ios::binary);
            if (!dump) continue;
            dump << "P6\n384 224\n255\n";
            for (uint32_t pixel : layer_frame) {
                const char rgb[] = {static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8),
                                    static_cast<char>(pixel)};
                dump.write(rgb, sizeof(rgb));
            }
        }
    }
#ifdef MAMERECOMP_CPS1_SDL
    if (g_live_window_enabled) present_live_frame(frame);
#endif
    if (path.empty()) return true;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n384 224\n255\n";
    for (uint32_t pixel : frame) {
        const char rgb[] = {static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8), static_cast<char>(pixel)};
        out.write(rgb, sizeof(rgb));
    }
    return static_cast<bool>(out);
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
    if (address >= 0x800000 && address <= 0x800006) return g_player_inputs;
    if (address >= 0x800018 && address <= 0x80001e) {
        const unsigned offset = (address - 0x800018) >> 1;
        return static_cast<uint16_t>((g_system_dips[offset] << 8) | 0xff);
    }
    if (address >= 0x800140 && address <= 0x80017e) {
        const unsigned offset = (address - 0x800140) >> 1;
        // CPS-B-21 (CPS_B_21_DEF in MAME): two factor registers feed a 32-bit
        // multiply result at offsets $04/$06. MAME returns $ffff for other
        // unimplemented read ports rather than the last value written there.
        if (offset == 2 || offset == 3) {
            const uint32_t product = static_cast<uint32_t>(g_cps_b[0]) * g_cps_b[1];
            return offset == 2 ? static_cast<uint16_t>(product)
                               : static_cast<uint16_t>(product >> 16);
        }
        // CPS-B-21, sf2ce's board signature is -1 at register offset 0x32.
        if (offset == 0x19) return 0xffff;
        return 0xffff;
    }
    if (address >= 0x800100 && address <= 0x80013e)
        return g_cps_a[(address - 0x800100) >> 1];
    if (address >= 0x900000 && address <= 0x92fffe) {
        const size_t offset = address - 0x900000;
        return static_cast<uint16_t>((g_gfxram[offset] << 8) | g_gfxram[offset + 1]);
    }
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
    if (address >= 0x800100 && address <= 0x80013e) {
        const unsigned offset = (address - 0x800100) >> 1;
        g_cps_a[offset] = value;
        if (offset == 5) copy_cps1_palette();
        return;
    }
    if (address >= 0x800140 && address <= 0x80017e) {
        const unsigned offset = (address - 0x800140) >> 1;
        g_cps_b[offset] = value;
        static uint16_t previous_layer_control = 0xffff;
        if (address == 0x800166 && value != previous_layer_control
            && std::getenv("MAMERECOMP_CPS1_TRACE_LAYER_CONTROL")) {
            std::cerr << "[cps1] layer-control cycle=" << ng_cycles << " PC=$" << std::hex
                      << g_current_guest_pc << " value=$" << value << " A5=$" << ng_cpu.a[5]
                      << " source=$" << ng_r16(ng_cpu.a[5] + 0x4e) << std::dec << '\n';
            previous_layer_control = value;
        }
        return;
    }
    if (address >= 0x800180 && address <= 0x800186) {
        sync_z80_to_main_clock();
        g_sound_command = static_cast<uint8_t>(value);
        return;
    }
    if (address >= 0x800188 && address <= 0x80018e) {
        sync_z80_to_main_clock();
        g_sound_timer_command = static_cast<uint8_t>(value);
        return;
    }
    if (address >= 0x900000 && address <= 0x92fffe) {
        const size_t offset = address - 0x900000;
        g_gfxram[offset] = static_cast<uint8_t>(value >> 8);
        g_gfxram[offset + 1] = static_cast<uint8_t>(value);
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

void copy_to_musashi() {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; ++i) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0 + i), ng_cpu.d[i]);
    for (int i = 0; i < 7; ++i) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_A0 + i), ng_cpu.a[i]);
    m68k_set_reg(M68K_REG_A7, ng_cpu.a[7]);
    m68k_set_reg(ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP, ng_cpu.osp);
    m68k_set_reg(M68K_REG_PC, ng_cpu.pc);
}

void copy_from_musashi() {
    const uint16_t sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    ng_cpu.s = (sr >> 13) & 1;
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; ++i) ng_cpu.d[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
    for (int i = 0; i < 8; ++i) ng_cpu.a[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
    ng_cpu.osp = m68k_get_reg(nullptr, ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    ng_cpu.pc = m68k_get_reg(nullptr, M68K_REG_PC) & kAddressMask;
}

void run_fallback() {
    g_last_fallback_pc = ng_cpu.pc & kAddressMask;
    if (g_trace_trap && (g_last_fallback_pc == 0x0005c4u || g_last_fallback_pc == 0x000aa6u || g_last_fallback_pc == 0x0000c0u)) {
        std::cerr << "[cps1] fallback PC=$" << std::hex << g_last_fallback_pc
                  << " SR=$" << ng_get_sr() << " SP=$" << ng_cpu.a[7]
                  << " D0=$" << ng_cpu.d[0] << " D2=$" << ng_cpu.d[2]
                  << " D3=$" << ng_cpu.d[3] << " D4=$" << ng_cpu.d[4]
                  << " A4=$" << ng_cpu.a[4] << " A5=$" << ng_cpu.a[5]
                  << std::dec << '\n';
    }
    if (!g_in_fallback_sequence && g_last_fallback_pc < g_program_size) {
        const auto [_, inserted] = g_dynamic_entries.insert(g_last_fallback_pc);
        if (inserted && g_dynamic_entries.size() <= 32)
            std::cerr << "[cps1] uncovered ROM entry PC=$" << std::hex << g_last_fallback_pc << std::dec << '\n';
    }
    char disassembly[128]{};
    const unsigned length = std::max(2U, m68k_disassemble(disassembly, g_last_fallback_pc, M68K_CPU_TYPE_68000));
    copy_to_musashi();
    const int consumed = m68k_execute(1);
    copy_from_musashi();
    ng_cycles += std::max(consumed, 1);
    ++g_fallback_instructions;
    const uint32_t next_pc = ng_cpu.pc & kAddressMask;
    g_in_fallback_sequence = next_pc == ((g_last_fallback_pc + length) & kAddressMask)
        && g_native.find(next_pc) == g_native.end();
}

void initialize_bus(std::span<const uint8_t> image) {
    g_program_size = image.size();
    g_program.fill(0xff);
    std::copy_n(image.begin(), std::min(image.size(), g_program.size()), g_program.begin());
    for (unsigned page = 0; page < 0x40; ++page) ng_rmap[page] = g_program.data() + page * 0x10000;
    for (unsigned page = 0x90; page <= 0x92; ++page) {
        const size_t offset = (page - 0x90) * 0x10000;
        ng_rmap[page] = g_gfxram.data() + offset;
        ng_wmap[page] = g_gfxram.data() + offset;
    }
    ng_rmap[0xff] = g_workram.data();
    ng_wmap[0xff] = std::getenv("MAMERECOMP_CPS1_TRACE_LAYER_CONTROL")
        ? nullptr : g_workram.data();
    g_cps_a.fill(0);
    g_cps_b.fill(0);
}

bool load_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out.assign(std::istreambuf_iterator<char>(file), {});
    return !out.empty();
}
}

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
void ng_bad_dispatch(uint32_t pc) {
    std::cerr << "[cps1] generated routine received mismatched PC=$" << std::hex << pc << std::dec << '\n';
    std::abort();
}
void ng_set_sr(uint16_t sr) {
    const uint8_t new_supervisor = (sr >> 13) & 1;
    if (new_supervisor != ng_cpu.s) {
        const uint32_t active_sp = ng_cpu.a[7];
        ng_cpu.a[7] = ng_cpu.osp;
        ng_cpu.osp = active_sp;
        ng_cpu.s = new_supervisor;
    }
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
}
void ng_exception(int vector, uint32_t ret_pc) {
    const uint16_t sr = ng_get_sr();
    ng_set_sr(static_cast<uint16_t>((sr | 0x2000) & ~0x8000));
    ng_push32(ret_pc);
    ng_push16(sr);
    ng_cpu.pc = ng_r32(static_cast<uint32_t>(vector) * 4) & kAddressMask;
    ng_cycles += 34;
}

unsigned int m68k_read_memory_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return ng_r32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { ng_w8(a, static_cast<uint8_t>(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { ng_w16(a, static_cast<uint16_t>(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { ng_w32(a, v); }
unsigned int m68k_read_immediate_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return ng_r32(a); }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cps1_boot <program.bin> [cycle-budget] [m68k-misses] [z80-fixed z80-banked [z80-misses z80-fallbacks [gfx.bin [frame.ppm [--window|--until-video]]]]]]\n";
        return 2;
    }
    std::vector<uint8_t> image;
    if (!load_file(argv[1], image) || image.size() < 8) {
        std::cerr << "cannot read 68000 image: " << argv[1] << '\n';
        return 1;
    }
    const uint64_t budget = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 12'000'000;
    const std::string miss_path = argc > 3 ? argv[3] : "m68k_dynamic_entries.txt";
    if (argc > 5) {
        if (!load_file(argv[4], g_z80_fixed) || !load_file(argv[5], g_z80_banked)
            || g_z80_fixed.size() != 0x8000 || g_z80_banked.size() != 0x8000) {
            std::cerr << "invalid CPS-1 Z80 ROM images\n";
            return 1;
        }
        mamerecomp::z80::SetZ80Code(mamerecomp_z80_aot_code());
        g_sound_cpu.Reset();
        g_sound_cpu.recording = true;
        g_sound_cpu_active = true;
    }
    std::string frame_path;
    if (argc > 8) {
        if (!load_file(argv[8], g_gfx_rom) || g_gfx_rom.size() != 0x600000) {
            std::cerr << "invalid CPS-1 graphics ROM image: " << argv[8] << '\n';
            return 1;
        }
    }
    if (argc > 9) frame_path = argv[9];
    const std::string mode = argc > 10 ? argv[10] : "";
    const bool until_video = mode == "--until-video";
 #ifdef MAMERECOMP_CPS1_SDL
    g_live_window_enabled = argc > 10 && std::string(argv[10]) == "--window";
 #endif
    bool video_found = false;
    const bool force_trap_fallback = std::getenv("MAMERECOMP_CPS1_FORCE_TRAP_FALLBACK") != nullptr;
    const bool force_all_fallback = std::getenv("MAMERECOMP_CPS1_FORCE_ALL_FALLBACK") != nullptr;
    g_trace_trap = std::getenv("MAMERECOMP_CPS1_TRACE_TRAP") != nullptr;
    g_trace_vblank = std::getenv("MAMERECOMP_CPS1_TRACE_VBLANK") != nullptr;
    initialize_bus(image);
    for (size_t i = 0; i < sf2ceng_recomp_count; ++i)
        g_native.emplace(sf2ceng_recomp_table[i].addr, sf2ceng_recomp_table[i].fn);
    ng_cpu.a[7] = ng_r32(0);
    ng_cpu.osp = ng_cpu.a[7];
    ng_cpu.pc = ng_r32(4) & kAddressMask;
    ng_cpu.s = 1;
    ng_set_sr(0x2700);
    g_profile_pc = std::getenv("MAMERECOMP_CPS1_PC_PROFILE") != nullptr;
    if (g_profile_pc || std::getenv("MAMERECOMP_CPS1_TRACE_LAYER_CONTROL"))
        ng_debug_instruction_hook = profile_guest_pc;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    while (static_cast<uint64_t>(ng_cycles) < budget) {
        sync_z80_to_main_clock();
        while (ng_cycles >= g_next_vblank) {
            ++g_vblank_count;
            if (g_trace_vblank && g_vblank_count <= 8)
                std::cerr << "[cps1] VBLANK n=" << g_vblank_count << " cycles=" << ng_cycles
                          << " edge=" << g_next_vblank << " PC=$" << std::hex << ng_cpu.pc
                          << " SR=$" << ng_get_sr() << std::dec
                          << " pending=" << g_irq1_pending << '\n';
            const size_t obj_base = video_base(0, 0x800);
            for (size_t i = 0; i < g_buffered_obj.size(); ++i)
                g_buffered_obj[i] = g_gfxram[(obj_base + i) & 0x3ffff];
            g_irq1_pending = true;
            g_next_vblank += kCps1FrameCycles;
            if (until_video && !frame_path.empty() && !video_found) {
                if (!write_frame("")) {
                    std::cerr << "could not probe CPS-1 framebuffer: " << frame_path << '\n';
                    return 1;
                }
                video_found = g_video_nonzero_pens != 0;
                if (video_found && !write_frame(frame_path)) {
                    std::cerr << "could not save CPS-1 framebuffer: " << frame_path << '\n';
                    return 1;
                }
            }
#ifdef MAMERECOMP_CPS1_SDL
            if (argc > 10 && std::string(argv[10]) == "--window") write_frame("");
#endif
        }
        const unsigned sr_mask = (ng_get_sr() >> 8) & 7;
        // CPS1 calls set_interrupt_mixer(false); MAME's IPL1 input is bit 1
        // of the three-line interrupt level, so VBlank is priority level 2.
        if (g_irq1_pending && sr_mask < 2) {
            ng_exception(26, ng_cpu.pc);
            ng_cpu.ipl = 2;
            ng_cpu.stopped = 0;
            g_irq1_pending = false;
        }
        if (force_trap_fallback && (ng_cpu.pc == 0x0005c4u || (ng_cpu.pc >= 0x000aa6u && ng_cpu.pc <= 0x000ab2u))) {
            run_fallback();
            continue;
        }
        if (force_all_fallback) {
            run_fallback();
            continue;
        }
        const auto found = g_native.find(ng_cpu.pc & 0x00fffffe);
        if (found == g_native.end()) {
            run_fallback();
            continue;
        }
        g_in_fallback_sequence = false;
        // Always stop at the next VBlank edge, even while the ROM masks
        // interrupts. Otherwise a batched block can carry an asserted IRQ
        // across the point where the ROM changes SR and deliver a stale IRQ
        // in the wrong phase of its POST.
        ng_next_event = g_next_vblank;
        ++g_native_blocks;
        found->second();
        if (ng_cpu.stopped) ng_cycles += 4;
    }
    if (!frame_path.empty() && (!until_video || video_found)) {
        if (!write_frame(frame_path)) {
            std::cerr << "could not render CPS-1 framebuffer: " << frame_path << '\n';
            return 1;
        }
        std::cout << "[cps1] framebuffer=" << frame_path << " (384x224 prototype)\n";
        std::cout << "[cps1] video CPS-A=";
        for (unsigned i = 0; i < g_cps_a.size(); ++i)
            if (g_cps_a[i]) std::cout << std::hex << i << ':' << g_cps_a[i] << ' ';
        std::cout << " CPS-B=";
        for (unsigned i = 0; i < g_cps_b.size(); ++i)
            if (g_cps_b[i] != 0xffff) std::cout << std::hex << i << ':' << g_cps_b[i] << ' ';
        std::cout << std::dec << " gfxram-nonzero="
                  << std::count_if(g_gfxram.begin(), g_gfxram.end(), [](uint8_t v) { return v != 0; })
                  << " sound-cmd=$" << std::hex << unsigned(g_sound_command)
                  << " YM-register-writes=" << std::dec
                  << std::count_if(g_ym_registers.begin(), g_ym_registers.end(), [](uint8_t v) { return v != 0; })
                  << '\n';
        const size_t palette_base = video_base(5, 0x400);
        const size_t scroll_base = video_base(1, 0x4000);
        const size_t object_base = video_base(0, 0x800);
        std::cout << "[cps1] video samples palette=" << std::hex << gfxram_word(palette_base)
                  << ',' << gfxram_word(palette_base + 2) << " scroll1="
                  << gfxram_word(scroll_base) << ',' << gfxram_word(scroll_base + 2)
                  << " object=" << gfxram_word(object_base) << ','
                  << gfxram_word(object_base + 2) << ',' << gfxram_word(object_base + 4)
                  << ',' << gfxram_word(object_base + 6) << std::dec << '\n';
        const size_t other_base = video_base(4, 0x800);
        std::cout << "[cps1] scroll x/y S1=" << std::hex << g_cps_a[6] << ',' << g_cps_a[7]
                  << " S2=" << g_cps_a[8] << ',' << g_cps_a[9]
                  << " S3=" << g_cps_a[10] << ',' << g_cps_a[11]
                  << " row-base=" << other_base << " row-offset=" << g_cps_a[0x10]
                  << " video-control=" << g_cps_a[0x11] << " rows=";
        for (unsigned y : {16U, 96U, 208U}) {
            const unsigned row = (y + g_cps_a[0x10]) & 0x3ff;
            std::cout << y << ':' << gfxram_word(other_base + row * 2) << ' ';
        }
        std::cout << std::dec << '\n';
    }
    std::cout << "[cps1] cycles=" << ng_cycles << " PC=$" << std::hex << ng_cpu.pc << std::dec
              << " native blocks=" << g_native_blocks
              << " native instructions=" << ng_icount
              << " Musashi instructions=" << g_fallback_instructions
              << " uncovered ROM entries=" << g_dynamic_entries.size()
              << " Z80 native blocks=" << g_sound_cpu.nativeBlocks
              << " Z80 interpreted instructions=" << g_sound_cpu.interpreted
              << " Z80 PC=$" << std::hex << g_sound_cpu.pc << std::dec
              << " SR=$" << std::hex << ng_get_sr() << std::dec
              << " bus reads=" << g_reads << " writes=" << g_writes
              << " video-pens=" << g_video_nonzero_pens << '\n';
    std::cout << "[cps1] final-regs A0=$" << std::hex << ng_cpu.a[0]
              << " A1=$" << ng_cpu.a[1] << " A3=$" << ng_cpu.a[3]
              << " A5=$" << ng_cpu.a[5] << " A6=$" << ng_cpu.a[6]
              << " A4=$" << ng_cpu.a[4]
              << " D0=$" << ng_cpu.d[0] << " D1=$" << ng_cpu.d[1] << std::dec << '\n';
    if (g_profile_pc) {
        std::vector<std::pair<uint64_t, uint32_t>> hot;
        uint64_t profiled = 0;
        for (uint32_t i = 0; i < g_pc_profile.size(); ++i)
            if (g_pc_profile[i]) { hot.emplace_back(g_pc_profile[i], i * 2); profiled += g_pc_profile[i]; }
        std::sort(hot.begin(), hot.end(), std::greater<>());
        std::cerr << "[cps1] PC profile total=" << profiled << " fail-loop $71c="
                  << g_pc_profile[0x71c >> 1] << " RAM test $6f8=" << g_pc_profile[0x6f8 >> 1]
                  << " reset $c0=" << g_pc_profile[0xc0 >> 1]
                  << " trap14 $5c4=" << g_pc_profile[0x5c4 >> 1]
                  << " trap-handler $aa6=" << g_pc_profile[0xaa6 >> 1]
                  << " hottest native PCs:";
        for (size_t i = 0; i < std::min<size_t>(hot.size(), 16); ++i)
            std::cerr << " $" << std::hex << hot[i].second << '=' << std::dec << hot[i].first;
        std::cerr << '\n';
    }
    if (until_video && !video_found)
        std::cout << "[cps1] video target not reached within cycle budget; no PPM written\n";
    std::vector<uint32_t> sorted_entries(g_dynamic_entries.begin(), g_dynamic_entries.end());
    std::sort(sorted_entries.begin(), sorted_entries.end());
    std::ofstream misses(miss_path);
    for (uint32_t pc : sorted_entries) misses << std::hex << std::uppercase << std::setw(6)
        << std::setfill('0') << pc << '\n';
    if (!misses) {
        std::cerr << "cannot write dynamic entry list: " << miss_path << '\n';
        return 1;
    }
    if (g_sound_cpu_active) {
        std::sort(g_sound_cpu.newEntries.begin(), g_sound_cpu.newEntries.end());
        g_sound_cpu.newEntries.erase(std::unique(g_sound_cpu.newEntries.begin(), g_sound_cpu.newEntries.end()),
                                     g_sound_cpu.newEntries.end());
        const std::string z80_entry_path = argc > 6 ? argv[6] : "z80_dynamic_entries.txt";
        std::ofstream z80_entries(z80_entry_path);
        for (uint16_t pc : g_sound_cpu.newEntries)
            z80_entries << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << pc << '\n';
        if (!z80_entries) {
            std::cerr << "cannot write dynamic Z80 entries: " << z80_entry_path << '\n';
            return 1;
        }
        mamerecomp::z80::z80native::ReportInterpreterProfile();
        const std::string fallback_path = argc > 7 ? argv[7] : "z80_fallbacks.log";
        mamerecomp::z80::z80native::PersistFallbackLog(fallback_path.c_str());
    }
    return 0;
}
