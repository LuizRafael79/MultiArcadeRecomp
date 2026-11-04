#include "cps1_video.hpp"
#include "cps1_state.hpp"
#include "cps1_gfxram.hpp"
#include "cps1_inputs.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef MAMERECOMP_CPS1_SDL
#include <SDL3/SDL.h>
#endif

namespace mamerecomp::cps1::detail {
namespace {
#ifdef MAMERECOMP_CPS1_SDL
SDL_Window* g_cps1_window = nullptr;
SDL_Renderer* g_cps1_renderer = nullptr;
SDL_Texture* g_cps1_texture = nullptr;
bool g_cps1_quit = false;
bool g_cps1_window_failed = false;

void present_live_frame(const std::vector<uint32_t>& frame) {
    static uint64_t next_frame_ns = 0;
    static uint64_t pacing_window_start_ns = 0;
    static uint64_t pacing_frames = 0;
    static const bool trace_pacing = std::getenv("MAMERECOMP_CPS1_TRACE_PACING") != nullptr;
    if (!g_cps1_window) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { g_cps1_window_failed = true; return; }
        g_cps1_window = SDL_CreateWindow("CPS-1 MameRecomp", 768, 448, SDL_WINDOW_RESIZABLE);
        if (!g_cps1_window) { g_cps1_window_failed = true; return; }
        g_cps1_renderer = SDL_CreateRenderer(g_cps1_window, nullptr);
        g_cps1_texture = g_cps1_renderer ? SDL_CreateTexture(g_cps1_renderer, SDL_PIXELFORMAT_ARGB8888,
                                                               SDL_TEXTUREACCESS_STREAMING, 384, 224) : nullptr;
        if (!g_cps1_renderer || !g_cps1_texture) g_cps1_window_failed = true;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) g_cps1_quit = true;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) g_cps1_quit = true;
            if (event.key.scancode == SDL_SCANCODE_5) pulse_coin(0);
            if (event.key.scancode == SDL_SCANCODE_6) pulse_coin(1);
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto held = [&](SDL_Scancode key) { return keys && keys[key]; };
    auto player_bit = [&](std::uint16_t& mask, SDL_Scancode key, unsigned bit) {
        if (held(key)) mask |= static_cast<std::uint16_t>(1U << bit);
    };
    std::uint16_t players = 0;
    // P1: WASD + J/K/L and U/I/O (SF2 six-button order).
    player_bit(players, SDL_SCANCODE_D, 0); player_bit(players, SDL_SCANCODE_A, 1);
    player_bit(players, SDL_SCANCODE_S, 2); player_bit(players, SDL_SCANCODE_W, 3);
    player_bit(players, SDL_SCANCODE_J, 4); player_bit(players, SDL_SCANCODE_K, 5);
    player_bit(players, SDL_SCANCODE_L, 6);
    // P2: arrows + keypad 1-6.
    player_bit(players, SDL_SCANCODE_RIGHT, 8); player_bit(players, SDL_SCANCODE_LEFT, 9);
    player_bit(players, SDL_SCANCODE_DOWN, 10); player_bit(players, SDL_SCANCODE_UP, 11);
    player_bit(players, SDL_SCANCODE_KP_1, 12); player_bit(players, SDL_SCANCODE_KP_2, 13);
    player_bit(players, SDL_SCANCODE_KP_3, 14);
    std::uint8_t extra = 0;
    if (held(SDL_SCANCODE_U)) extra |= 0x01;
    if (held(SDL_SCANCODE_I)) extra |= 0x02;
    if (held(SDL_SCANCODE_O)) extra |= 0x04;
    if (held(SDL_SCANCODE_KP_4)) extra |= 0x10;
    if (held(SDL_SCANCODE_KP_5)) extra |= 0x20;
    if (held(SDL_SCANCODE_KP_6)) extra |= 0x40;
    // CPS-1 three-player games expose player 3 on the separate IN2 port.
    std::uint8_t player3 = 0;
    if (held(SDL_SCANCODE_KP_6)) player3 |= 0x01;
    if (held(SDL_SCANCODE_KP_4)) player3 |= 0x02;
    if (held(SDL_SCANCODE_KP_5)) player3 |= 0x04;
    if (held(SDL_SCANCODE_KP_8)) player3 |= 0x08;
    if (held(SDL_SCANCODE_KP_7)) player3 |= 0x10;
    if (held(SDL_SCANCODE_KP_9)) player3 |= 0x20;
    if (held(SDL_SCANCODE_KP_0)) player3 |= 0x40;
    if (held(SDL_SCANCODE_KP_ENTER)) player3 |= 0x80;
    const std::uint8_t system = static_cast<std::uint8_t>(
        (held(SDL_SCANCODE_5) ? 0x01 : 0) | (held(SDL_SCANCODE_6) ? 0x02 : 0)
        | (held(SDL_SCANCODE_F2) ? 0x04 : 0) | (held(SDL_SCANCODE_1) ? 0x10 : 0)
        | (held(SDL_SCANCODE_2) ? 0x20 : 0));
    if (std::getenv("MAMERECOMP_CPS1_TRACE_INPUT")) {
        static std::uint16_t last_players = 0xffff;
        static std::uint8_t last_system = 0xff, last_extra = 0xff;
        if (last_players != static_cast<std::uint16_t>(~players)
            || last_system != static_cast<std::uint8_t>(~system)
            || last_extra != static_cast<std::uint8_t>(~extra)) {
            std::fprintf(stderr, "[cps1-input] keys detected: players=$%04x IN0=$%02x extra=$%02x\n",
                static_cast<unsigned>(players), static_cast<unsigned>(static_cast<std::uint8_t>(~system)),
                static_cast<unsigned>(extra));
            last_players = static_cast<std::uint16_t>(~players);
            last_system = static_cast<std::uint8_t>(~system);
            last_extra = static_cast<std::uint8_t>(~extra);
        }
    }
    set_host_inputs(players, system, extra, player3);
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
    const uint64_t frame_period_ns = static_cast<uint64_t>(g_htotal) * g_vtotal
        * 1'000'000'000ULL / g_pixel_clock_hz;
    const uint64_t now = SDL_GetTicksNS();
    if (trace_pacing) {
        if (pacing_window_start_ns == 0) pacing_window_start_ns = now;
        if (++pacing_frames == 120) {
            const double elapsed = static_cast<double>(now - pacing_window_start_ns) / 1'000'000'000.0;
            std::fprintf(stderr, "[cps1] live-frame pacing: %.2f fps over %llu frames (target %.2f fps)\n",
                elapsed > 0.0 ? pacing_frames / elapsed : 0.0,
                static_cast<unsigned long long>(pacing_frames),
                static_cast<double>(g_pixel_clock_hz) / (g_htotal * g_vtotal));
            pacing_window_start_ns = now;
            pacing_frames = 0;
        }
    }
    if (next_frame_ns == 0) next_frame_ns = now;
    next_frame_ns += frame_period_ns;
    if (next_frame_ns > now) SDL_DelayNS(next_frame_ns - now);
    else next_frame_ns = now;
}
#endif

bool g_live_window_enabled = false;
uint64_t g_video_nonzero_pens = 0;
}

void set_live_window_enabled(bool enabled) { g_live_window_enabled = enabled; }
bool live_window_quit_requested() {
#ifdef MAMERECOMP_CPS1_SDL
    return g_cps1_quit || (g_live_window_enabled && g_cps1_window_failed);
#else
    return false;
#endif
}
uint64_t video_nonzero_pens() { return g_video_nonzero_pens; }

uint16_t gfxram_word(size_t byte_offset) {
    byte_offset %= g_gfxram.size();
    return static_cast<uint16_t>((g_gfxram[byte_offset] << 8)
        | g_gfxram[(byte_offset + 1) % g_gfxram.size()]);
}

void copy_cps1_palette() {
    const size_t base = video_base(5, 0x400);
    const uint16_t control = g_cps_b[g_active_cps_b_profile
        ? g_active_cps_b_profile->palette_control_register : 0x18];
    size_t source_page = 0;
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
    const auto* profile = g_active_cps_b_profile;
    if (profile && (profile->graphics_mapper == "mapper_CD63B"
            || profile->graphics_mapper == "mapper_TK263B"
            || profile->graphics_mapper == "mapper_PS63B")) {
        // MAME cps1_v.cpp: the CD63B/TK263B/PS63B PALs expose two 0x8000
        // banks to sprites and all three scroll layers. The type-specific
        // shift happens before the PAL range lookup and is undone afterward.
        static constexpr unsigned kTypeMask[] = {0x01, 0x02, 0x04, 0x08};
        static constexpr unsigned kShift[] = {1, 0, 1, 3};
        if (type >= std::size(kTypeMask)) return -1;
        const unsigned shift = kShift[type];
        const std::uint32_t mapped_address = code << shift;
        unsigned bank = 0;
        if (mapped_address <= 0x07fff) bank = 0;
        else if (mapped_address >= 0x08000 && mapped_address <= 0x0ffff) bank = 1;
        else return -1;
        return static_cast<int>(((bank * 0x8000) + (mapped_address & 0x7fff)) >> shift);
    }

    // SF2CE's mapper_S9263B has a different per-type PAL table. Keep its
    // existing mapping until that table is represented by data as well.
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
    for (unsigned page = 0; page < 6; ++page) {
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

void flip_frame_180(std::vector<uint32_t>& frame) {
    for (size_t i = 0, opposite = frame.size() - 1; i < opposite; ++i, --opposite)
        std::swap(frame[i], frame[opposite]);
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
        const unsigned row = world_y / size;
        const unsigned local_y_unflipped = world_y % size;
        int line_scroll_x = scroll_x;
        // CPS-A row-scroll RAM supplies one extra Scroll-2 horizontal offset
        // per raster line when video-control bit 0 is set. MAME stores the
        // scroll at tilemap row (i + scroll_y), but sources it from
        // OTHER[i + ROWSCROLL_OFFS]. Since this is output scanline i, the
        // scroll-Y offset selects the tilemap row only; it is not subtracted
        // again from the OTHER-RAM address.
        if (layer == 2 && (video_control & 1)) {
            const unsigned row = (screen_y + row_scroll_offset) & 0x3ff;
            line_scroll_x += gfxram_word(other_base + row * 2);
        }
        unsigned cached_col = std::numeric_limits<unsigned>::max();
        const std::vector<uint8_t>* cached_tile = nullptr;
        uint16_t cached_attr = 0;
        unsigned cached_color = 0;
        unsigned cached_priority_group = 0;
        for (int x = 0; x < 384; ++x) {
            const unsigned world_x = static_cast<unsigned>((x + 64 + line_scroll_x) & map_mask);
            const unsigned col = world_x / size;
            if (col != cached_col) {
                size_t tile_index;
                if (layer == 1) tile_index = (row & 0x1f) + ((col & 0x3f) << 5) + ((row & 0x20) << 6);
                else if (layer == 2) tile_index = (row & 0x0f) + ((col & 0x3f) << 4) + ((row & 0x30) << 6);
                else tile_index = (row & 0x07) + ((col & 0x3f) << 3) + ((row & 0x38) << 6);
                const uint32_t raw_code = gfxram_word(base + tile_index * 4);
                cached_attr = gfxram_word(base + tile_index * 4 + 2);
                const int code = map_gfx_code(type, layer == 3 ? raw_code & 0x3fff : raw_code);
                cached_tile = code < 0 ? nullptr : decoded_tile(type, size, static_cast<unsigned>(code),
                    layer == 1 ? ((tile_index >> 5) & 1) : 0);
                cached_color = (cached_attr & 0x1f) + layer * 0x20;
                cached_priority_group = (cached_attr >> 7) & 3;
                cached_col = col;
            }
            if (!cached_tile) continue;
            unsigned local_x = world_x % size;
            unsigned local_y = local_y_unflipped;
            if (cached_attr & 0x20) local_x = size - 1 - local_x;
            if (cached_attr & 0x40) local_y = size - 1 - local_y;
            const uint8_t pen = (*cached_tile)[local_y * size + local_x];
            // MAME configures CPS1 tilemaps with bgmask=$8000: pen 15 is
            // transparent in the normal layer pass.
            if (pen == 15) continue;
            if (sprite_block_mask) {
                // SF2CE's CPS_B_21_DEF uses priority registers $28/$2a/$2c/$2e.
                // MAME's cps1_update_transmasks passes `priority_reg ^ 0xffff`
                // as a transparent-pen mask to set_transmask. TILEMAP_DRAW_LAYER0
                // draws the opaque complement of that mask, so the CPS-B
                // register's set bits are the pens that cover following sprites.
                const unsigned register_index = g_active_cps_b_profile
                    ? g_active_cps_b_profile->priority_registers[cached_priority_group]
                    : 0x14 + cached_priority_group;
                const bool high = (g_cps_b[register_index] & (1U << pen)) != 0;
                if (high) (*sprite_block_mask)[y * 384 + x] = 1;
                continue;
            }
            ++g_video_nonzero_pens;
            // MAME's gfxdecode color base is zero for all three tilemaps.
            // Their palette pages are already encoded by color = attr +
            // 0x20/0x40/0x60, so adding a per-layer page here double-shifts it.
            const size_t palette_index = cached_color * 16 + pen;
            frame[y * 384 + x] = palette[palette_index];
        }
    }
}

void draw_sprite_tile(std::vector<uint32_t>& frame, const std::array<uint32_t, 0xc00>& palette,
                      std::vector<uint8_t>& sprite_block_mask, int code, unsigned color,
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
            if (pen != 15) {
                ++g_video_nonzero_pens;
                frame[y * 384 + x] = palette[color * 16 + pen];
                // MAME prio_transpen sets the priority bitmap to 31 after
                // drawing an opaque sprite pixel. Its implicit mask bit 31
                // then prevents later objects from replacing this pixel.
                // Preserve that first-object-wins rule as well as the
                // foreground tile mask accumulated before sprite rendering.
                sprite_block_mask[y * 384 + x] = 1;
            }
        }
    }
}

void render_sprites(std::vector<uint32_t>& frame, const std::array<uint32_t, 0xc00>& palette,
                    std::vector<uint8_t>& sprite_block_mask) {
    const size_t base = video_base(0, 0x800);
    std::array<uint16_t, 0x400> words{};
    for (size_t i = 0; i < words.size(); ++i) {
        if (std::any_of(g_buffered_obj.begin(), g_buffered_obj.end(), [](uint8_t b) { return b != 0; }))
            words[i] = static_cast<uint16_t>((g_buffered_obj[i * 2] << 8) | g_buffered_obj[i * 2 + 1]);
        else words[i] = gfxram_word(base + i * 2);
    }
    int last = 0x3fc;
    bool has_sprites = true;
    for (int entry = 0; entry < static_cast<int>(words.size()); entry += 4) {
        if ((words[entry + 3] & 0xff00) == 0xff00) {
            if (entry == 0) has_sprites = false;
            else last = entry - 4;
            break;
        }
    }
    if (std::getenv("MAMERECOMP_CPS1_TRACE_OBJECTS")) {
        const unsigned layer_control_index = g_active_cps_b_profile
            ? g_active_cps_b_profile->layer_control_register : 0x13;
        if (g_cps_b[layer_control_index] == 0x06d6) {
            std::uint64_t hash = 1469598103934665603ULL;
            for (uint16_t value : words) {
                hash ^= value;
                hash *= 1099511628211ULL;
            }
            static std::uint64_t previous_hash = 0;
            static unsigned captures = 0;
            if (hash != previous_hash && captures < 8) {
                std::fprintf(stderr, "[cps1-objects] control=$06d6 base=$%05zx last=$%03x hash=$%016llx\n",
                    base, last, static_cast<unsigned long long>(hash));
                for (int entry = 0; has_sprites && entry <= last; entry += 4) {
                    const uint16_t attr = words[entry + 3];
                    std::fprintf(stderr,
                        "[cps1-objects] %03x x=%03x y=%03x code=%04x attr=%04x size=%dx%d\n",
                        entry / 4, words[entry] & 0x1ff, words[entry + 1] & 0x1ff,
                        words[entry + 2], attr, ((attr >> 8) & 0x0f) + 1,
                        ((attr >> 12) & 0x0f) + 1);
                }
                previous_hash = hash;
                ++captures;
            }
        }
    }
    // MAME counts down to find the last entry, but its normal data pointer
    // starts at entry 0 and advances. A small set of bootlegs (bootleg_kludge
    // bit 6) starts at the last entry and moves backward instead.
    const int first = g_reverse_sprite_order ? last : 0;
    const int step = g_reverse_sprite_order ? -4 : 4;
    for (int i = first; has_sprites && i >= 0 && i <= last; i += step) {
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
    if (g_gfx_rom.empty()) return false;
    g_video_nonzero_pens = 0;
    const auto palette = make_palette();
    // MAME clears CPS1 output with palette pen $bff before drawing layers.
    std::vector<uint32_t> frame(384 * 224, palette[0xbff]);
    const unsigned layer_control_register = g_active_cps_b_profile
        ? g_active_cps_b_profile->layer_control_register : 0x13;
    const uint16_t control = g_cps_b[layer_control_register];
    const unsigned order[] = {unsigned((control >> 6) & 3), unsigned((control >> 8) & 3),
                              unsigned((control >> 10) & 3), unsigned((control >> 12) & 3)};
    const uint16_t layer_enable = control;
    const uint16_t video_control = g_cps_a[0x11];
    // CPS-B-21 masks apply to tilemaps 1/2/3; sprite layer 0 is always
    // addressable when selected by the priority order.
    const std::array<unsigned, 5> enable_mask = g_active_cps_b_profile
        ? std::array<unsigned, 5>{g_active_cps_b_profile->layer_enable_masks[0],
            g_active_cps_b_profile->layer_enable_masks[1], g_active_cps_b_profile->layer_enable_masks[2],
            g_active_cps_b_profile->layer_enable_masks[3], g_active_cps_b_profile->layer_enable_masks[4]}
        : std::array<unsigned, 5>{0x02, 0x04, 0x08, 0x30, 0x30};
    if (std::getenv("MAMERECOMP_CPS1_TRACE_COMPOSITION")) {
        static std::array<uint16_t, 6> previous{};
        const std::array<uint16_t, 6> current{
            control, g_cps_a[0x11],
            g_cps_b[g_active_cps_b_profile ? g_active_cps_b_profile->priority_registers[0] : 0x14],
            g_cps_b[g_active_cps_b_profile ? g_active_cps_b_profile->priority_registers[1] : 0x15],
            g_cps_b[g_active_cps_b_profile ? g_active_cps_b_profile->priority_registers[2] : 0x16],
            g_cps_b[g_active_cps_b_profile ? g_active_cps_b_profile->priority_registers[3] : 0x17],
        };
        static bool have_previous = false;
        if (!have_previous || current != previous) {
            std::fprintf(stderr,
                "[cps1-compose] control=$%04x order=%u,%u,%u,%u video=$%04x "
                "priority=%04x,%04x,%04x,%04x enables=%u,%u,%u\n",
                control, order[0], order[1], order[2], order[3], g_cps_a[0x11],
                current[2], current[3], current[4], current[5],
                (control & enable_mask[0]) != 0,
                (control & enable_mask[1]) != 0,
                (control & enable_mask[2]) != 0);
            previous = current;
            have_previous = true;
        }
    }
    std::vector<uint8_t> sprite_block_mask(384 * 224, 0);
    for (unsigned index = 0; index < std::size(order); ++index) {
        const unsigned layer = order[index];
        const bool enabled = layer != 0 && (layer_enable & enable_mask[layer - 1])
            && (layer == 1 || (video_control & (1U << layer)));
        if (index != 0 && layer == 0) {
            const unsigned previous_layer = order[index - 1];
            if (previous_layer != 0 && (layer_enable & enable_mask[previous_layer - 1])
                && (previous_layer == 1 || (video_control & (1U << previous_layer))))
                render_tile_layer(frame, palette, previous_layer, &sprite_block_mask);
        }
        if (layer == 0) {
            render_sprites(frame, palette, sprite_block_mask);
        } else if (enabled) {
            render_tile_layer(frame, palette, layer);
        }
    }
    if (g_cps_a[0x11] & 0x8000) flip_frame_180(frame);
    if (const char* prefix = std::getenv("MAMERECOMP_CPS1_DUMP_LAYERS")) {
        std::vector<uint8_t> no_sprite_mask(384 * 224, 0);
        for (unsigned layer = 0; layer <= 3; ++layer) {
            auto layer_frame = std::vector<uint32_t>(384 * 224, palette[0xbff]);
            if (layer == 0) render_sprites(layer_frame, palette, no_sprite_mask);
            else render_tile_layer(layer_frame, palette, layer);
            if (g_cps_a[0x11] & 0x8000) flip_frame_180(layer_frame);
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


} // namespace mamerecomp::cps1::detail
