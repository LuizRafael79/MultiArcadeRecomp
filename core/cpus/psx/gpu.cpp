#include "gpu.h"
#include "gpu_hw.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace mamerecomp::psx {
namespace {
constexpr uint64_t kRefreshScale = 1'000'000;
constexpr uint64_t kRefreshNtsc240 = 59'826'098;
constexpr uint64_t kRefreshNtsc480 = 59'940'052;
constexpr uint64_t kRefreshPal = 50'000'000;

uint64_t refresh_rate_scaled(uint32_t status)
{
    if (status & (1u << 20)) return kRefreshPal;
    return (status & (1u << 19)) ? kRefreshNtsc480 : kRefreshNtsc240;
}

constexpr auto make_blend_table()
{
    std::array<std::array<std::array<uint8_t, 32>, 32>, 4> table{};
    for (unsigned mode = 0; mode < 4; ++mode) {
        for (unsigned back = 0; back < 32; ++back) {
            for (unsigned front = 0; front < 32; ++front) {
                unsigned value = 0;
                switch (mode) {
                case 0: value = (back + front) / 2; break;
                case 1: value = std::min(31U, back + front); break;
                case 2: value = back > front ? back - front : 0; break;
                default: value = std::min(31U, back + (front >> 2)); break;
                }
                table[mode][back][front] = static_cast<uint8_t>(value);
            }
        }
    }
    return table;
}

constexpr auto make_modulation_table()
{
    std::array<std::array<uint8_t, 256>, 32> table{};
    for (unsigned texel = 0; texel < 32; ++texel)
        for (unsigned shade = 0; shade < 256; ++shade)
            table[texel][shade] = static_cast<uint8_t>(
                std::min(31U, (texel * shade) / 128U));
    return table;
}

constexpr auto kBlendTable = make_blend_table();
constexpr auto kModulationTable = make_modulation_table();

uint16_t rgb24_to_15(uint32_t rgb)
{
    return uint16_t(((rgb >> 3) & 31) | (((rgb >> 11) & 31) << 5) |
                    (((rgb >> 19) & 31) << 10));
}
bool cull_vertex(int a, int b)
{
    const int delta = a - b;
    return delta < -1023 || delta > 1023;
}
bool cull_triangle(const int *x, const int *y, unsigned first)
{
    const unsigned a = first, b = first + 1, c = first + 2;
    return cull_vertex(y[a], y[b]) || cull_vertex(x[a], x[b]) ||
           cull_vertex(y[b], y[c]) || cull_vertex(x[b], x[c]) ||
           cull_vertex(y[c], y[a]) || cull_vertex(x[c], x[a]);
}
struct PolygonEdges {
    unsigned count;
    int left;
    const int *next;
    const int *prev;
    int max_y;
};
bool find_polygon_edges(const int *x, const int *y, unsigned count,
                        PolygonEdges &edges)
{
    static constexpr int next3[3]{1, 2, 0};
    static constexpr int prev3[3]{2, 0, 1};
    static constexpr int next4[4]{1, 3, 0, 2};
    static constexpr int prev4[4]{2, 0, 3, 1};
    static constexpr int next4b[4]{0, 3, 1, 2};
    static constexpr int prev4b[4]{0, 2, 3, 1};
    if (!x || !y || (count != 3 && count != 4)) return false;

    edges.count = count;
    edges.left = 0;
    unsigned first_candidate = 0;
    if (count == 4) {
        if (cull_triangle(x, y, 0)) {
            if (cull_triangle(x, y, 1)) return false;
            edges.next = next4b;
            edges.prev = prev4b;
            edges.left = 1;
            first_candidate = 1;
        } else if (cull_triangle(x, y, 1)) {
            edges.count = 3;
            edges.next = next3;
            edges.prev = prev3;
        } else {
            edges.next = next4;
            edges.prev = prev4;
        }
    } else {
        if (cull_triangle(x, y, 0)) return false;
        edges.next = next3;
        edges.prev = prev3;
    }

    // MAME's FINDTOPLEFT starts at vertex 1 when triangle 0 of a quad was
    // culled, and searches only the surviving chain. Including vertex 0 here
    // can select a point from the discarded half and corrupt the scan range.
    edges.max_y = y[first_candidate];
    for (unsigned i = first_candidate; i < edges.count; ++i) {
        edges.max_y = std::max(edges.max_y, y[i]);
        if (y[i] < y[edges.left] ||
            (y[i] == y[edges.left] && x[i] < x[edges.left]))
            edges.left = int(i);
    }
    return true;
}
uint16_t blend_rgb555(uint16_t back, uint16_t front, unsigned mode)
{
    const auto &table = kBlendTable[mode & 3];
    return uint16_t(table[back & 31][front & 31] |
                    (table[(back >> 5) & 31][(front >> 5) & 31] << 5) |
                    (table[(back >> 10) & 31][(front >> 10) & 31] << 10) |
                    (front & 0x8000));
}

unsigned gp0_packet_words(unsigned op)
{
    if (op == 0x80) return 4; // command, source, destination, dimensions
    if (op == 0x02 || op == 0xa0 || op == 0xc0) return 3;
    if ((op & 0xfc) == 0x40) return 3; // monochrome line
    if ((op & 0xfc) == 0x50) return 4; // Gouraud line
    if ((op & 0xe0) == 0x20) {
        const unsigned vertices = (op & 8) ? 4 : 3;
        return 1 + vertices * (1 + ((op & 4) ? 1 : 0)) + ((op & 0x10) ? vertices - 1 : 0);
    }
    if (op >= 0x60 && op <= 0x7f)
        return 2 + ((op & 4) ? 1 : 0) + ((op & 0x18) == 0 ? 1 : 0);
    return 1;
}
bool is_gp0_polyline(unsigned op)
{ return (op & 0xf8) == 0x48 || (op & 0xf8) == 0x58; }
bool is_gp0_polyline_terminator(uint32_t value)
{ return (value & 0xf000f000u) == 0x50005000u; }
}

Gpu::Gpu(std::vector<uint8_t> &vram, unsigned gpu_type)
    : vram_(vram), gpu_type_(gpu_type)
{
    profile_timing_ = std::getenv("MAMERECOMP_ZN_PROFILE_RUNTIME") != nullptr;
    vram_height_ = std::max<size_t>(1, vram_.size() / (1024 * 2));
    vram_height_power_of_two_ =
        (vram_height_ & (vram_height_ - 1)) == 0;
    vram_height_mask_ = vram_height_ - 1;
    vram_pixel_count_ = std::max<size_t>(1, vram_.size() / 2);
    vram_pixel_count_power_of_two_ =
        (vram_pixel_count_ & (vram_pixel_count_ - 1)) == 0;
    vram_pixel_mask_ = vram_pixel_count_ - 1;
}

Gpu::~Gpu() = default;

void Gpu::enable_hardware_renderer()
{
    if (hardware_renderer_) return;
#ifdef MAMERECOMP_PSX_GPU_OPENGL
    hardware_renderer_ = std::make_unique<GpuHardwareRenderer>(vram_);
    hardware_vram_shadow_valid_ = true;
    std::cout << "PSX GPU OpenGL rasterizer: "
              << hardware_renderer_->renderer_name() << '\n';
#else
    throw std::runtime_error("--gpu-hw requested, but this build has no SDL3/OpenGL PSX rasterizer; configure with -DMAMERECOMP_ENABLE_PSX_GPU_OPENGL=ON");
#endif
}

void Gpu::get_hardware_state(GpuHwState& state) const
{
    state.draw_x1 = draw_area_x1_;
    state.draw_y1 = draw_area_y1_;
    state.draw_x2 = draw_area_x2_;
    state.draw_y2 = draw_area_y2_;
    state.offset_x = draw_offset_x_;
    state.offset_y = draw_offset_y_;
    state.window_x = int(texture_window_x_);
    state.window_y = int(texture_window_y_);
    state.window_mask_x = int(texture_window_mask_x_);
    state.window_mask_y = int(texture_window_mask_y_);
    state.flip_x = texture_flip_x_;
    state.flip_y = texture_flip_y_;
    state.texture_page = texture_page_;
    state.gpu_type = gpu_type_;
    state.set_mask = draw_mask_;
    state.check_mask = check_mask_;
}

size_t Gpu::wrap_vram_y(size_t y) const
{
    return vram_height_power_of_two_ ? (y & vram_height_mask_)
                                     : (y % vram_height_);
}

size_t Gpu::wrap_vram_pixel(size_t pixel) const
{
    return vram_pixel_count_power_of_two_ ? (pixel & vram_pixel_mask_)
                                          : (pixel % vram_pixel_count_);
}

bool Gpu::advance_screen_timing(uint64_t cpu_cycles, uint64_t cpu_cycle_hz)
{
    // Match MAME's NTSC/PAL screen refresh derived from the GPU video clock,
    // with the same 1024-line phase used for GPUSTAT's live vpos bit.
    // MAME's screen starts with vblank_start_time == 0 and reports a rounded
    // vpos offset by visible_area.bottom()+1 (480). Accumulate phase so this
    // instruction-boundary callback does not divide by the CPU clock each time.
    if (!cpu_cycle_hz) {
        screen_vpos_odd_ = false;
        return false;
    }
    if (screen_cycle_hz_ != cpu_cycle_hz) {
        screen_cycle_hz_ = cpu_cycle_hz;
        screen_elapsed_cycles_ = 0;
        screen_frame_phase_ = 0;
        // vpos() rounds to the nearest scanline.
        screen_line_phase_ = cpu_cycle_hz * kRefreshScale / 2;
    }
    if (cpu_cycles <= screen_elapsed_cycles_) return false;
    const uint64_t delta = cpu_cycles - screen_elapsed_cycles_;
    screen_elapsed_cycles_ = cpu_cycles;

    constexpr uint64_t total_lines = 1024;
    const uint64_t refresh = refresh_rate_scaled(status_);
    const uint64_t phase_limit = cpu_cycle_hz * kRefreshScale;
    screen_frame_phase_ += delta * refresh;
    const uint64_t elapsed_frames = screen_frame_phase_ / phase_limit;
    screen_frame_phase_ %= phase_limit;
    const bool vblank_started = elapsed_frames != 0;
    if (vblank_started) {
        // MAME's default VIDEO_UPDATE_BEFORE_VBLANK updates the screen at
        // vblank_begin, before the GPU's registered vblank callback changes
        // status/IRQ and before the CPU can react with a page flip.
        if (frame_capture_enabled_)
            last_vblank_frame_ = framebuffer_rgb();
        if (status_ & (1u << 22)) {
            if (elapsed_frames & 1) status_ ^= 1u << 13;
        } else {
            status_ |= 1u << 13;
        }
    }
    screen_line_phase_ += delta * refresh * total_lines;
    const uint64_t elapsed_lines = screen_line_phase_ / phase_limit;
    screen_line_phase_ %= phase_limit;
    if (elapsed_lines & 1) screen_vpos_odd_ = !screen_vpos_odd_;
    return vblank_started;
}

uint64_t Gpu::cycles_until_screen_event(uint64_t cpu_cycle_hz) const
{
    if (!cpu_cycle_hz) return UINT64_MAX;
    const uint64_t refresh = refresh_rate_scaled(status_);
    const uint64_t phase_limit = cpu_cycle_hz * kRefreshScale;
    const uint64_t frame_phase_rate = refresh;
    const uint64_t frame_clocks = phase_limit - screen_frame_phase_;
    const uint64_t until_frame = (frame_clocks + frame_phase_rate - 1) / frame_phase_rate;
    // The beam position is queried from the current emulated time when
    // GPUSTAT is read (MAME uses screen().vpos() for this). Only vblank is a
    // scheduler event; waking the CPU for each of the 1024 scanlines adds
    // about 61,000 unnecessary scheduler callbacks per emulated second.
    return std::max<uint64_t>(1, until_frame);
}

int16_t Gpu::coord_x(uint32_t v)
{ return int16_t((v & 0x7ff) - ((v & 0x400) ? 0x800 : 0)); }
int16_t Gpu::coord_y(uint32_t v)
{ const uint32_t y = (v >> 16) & 0x7ff; return int16_t(y - ((y & 0x400) ? 0x800 : 0)); }

uint16_t Gpu::get_pixel(int x, int y) const
{
    if (x >= 0 && x < 1024 && y >= 0 && size_t(y) < vram_height_) {
        const size_t at = (size_t(y) * 1024 + size_t(x)) * 2;
        return uint16_t(vram_[at] | (uint16_t(vram_[at + 1]) << 8));
    }
    x &= 1023;
    y = int(wrap_vram_y(static_cast<size_t>(y)));
    const size_t at = (size_t(y) * 1024 + size_t(x)) * 2;
    if (at + 1 >= vram_.size()) return 0;
    return uint16_t(vram_[at] | (uint16_t(vram_[at + 1]) << 8));
}
uint16_t Gpu::get_pixel_linear(int x, int y) const
{
    // MAME's texture and CLUT readers start at a row pointer, then advance a
    // flat pointer. An X coordinate beyond 1023 continues into the next row;
    // it does not wrap back to column zero of the current row.
    const size_t pixel = wrap_vram_pixel(size_t(y) * 1024 + size_t(x));
    const size_t at = pixel * 2;
    return uint16_t(vram_[at] | (uint16_t(vram_[at + 1]) << 8));
}
void Gpu::put_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < 1024 && y >= 0 && size_t(y) < vram_height_) {
        const size_t at = (size_t(y) * 1024 + size_t(x)) * 2;
        vram_[at] = uint8_t(color);
        vram_[at + 1] = uint8_t(color >> 8);
        return;
    }
    x &= 1023;
    y = int(wrap_vram_y(static_cast<size_t>(y)));
    const size_t at = (size_t(y) * 1024 + size_t(x)) * 2;
    if (at + 1 < vram_.size()) { vram_[at] = uint8_t(color); vram_[at + 1] = uint8_t(color >> 8); }
}
void Gpu::write_pixel(int x, int y, uint16_t color)
{
    // MAME's WRITE_PIXEL only reads the destination when check-STP is active.
    // Avoid an otherwise unconditional VRAM read for every rasterized pixel.
    if (check_mask_ && (get_pixel(x, y) & 0x8000)) return;
    put_pixel(x, y, static_cast<uint16_t>(color | (draw_mask_ ? 0x8000 : 0)));
}
void Gpu::fill_rect(int x, int y, int w, int h, uint16_t color, bool honor_mask)
{
    w = std::clamp(w, 0, 1024);
    h = std::clamp(h, 0, int(std::max<size_t>(1, vram_.size() / (1024 * 2))));
    if (hardware_renderer_ && !honor_mask) {
        hardware_renderer_->fill_rect(x, y, w, h, color);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) {
        if (honor_mask) write_pixel(x + xx, y + yy, color);
        else put_pixel(x + xx, y + yy, color);
    }
}
void Gpu::draw_solid_rect(int x, int y, int w, int h, uint16_t color,
                          bool semi_transparent)
{
    if (hardware_renderer_) {
        GpuHwState state;
        get_hardware_state(state);
        hardware_renderer_->draw_rect(x, y, w, h, color, state, semi_transparent);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    const int vram_height = int(std::max<size_t>(1, vram_.size() / (1024 * 2)));
    const int left = x + draw_offset_x_;
    const int top = y + draw_offset_y_;
    const int x0 = std::max({0, draw_area_x1_, left});
    const int y0 = std::max({0, draw_area_y1_, top});
    const int x1 = std::min({1024, draw_area_x2_ + 1, left + std::max(w, 0)});
    const int y1 = std::min({vram_height, draw_area_y2_ + 1, top + std::max(h, 0)});
    const unsigned abr = gpu_type_ == 2 ? ((texture_page_ >> 5) & 3)
                                         : ((texture_page_ >> 7) & 3);
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const uint16_t out = semi_transparent
                ? blend_rgb555(get_pixel(px, py), color, abr) : color;
            write_pixel(px, py, out);
        }
    }
}
void Gpu::draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t color, bool semi_transparent)
{
    const int x[3]{x0, x1, x2}, y[3]{y0, y1, y2};
    draw_flat_polygon(x, y, 3, color, semi_transparent);
}

// MAME's MonochromeLine/GouraudLine walk a 16.16 DDA for max(|dx|,|dy|)
// pixels, apply draw offset and inclusive draw-area clipping, and omit the
// terminal endpoint so adjoining line segments do not double-draw it.
void Gpu::draw_line(int x0, int y0, int x1, int y1, uint32_t color0,
                    uint32_t color1, bool gouraud, bool semi_transparent)
{
    if (hardware_renderer_) {
        GpuHwState state;
        get_hardware_state(state);
        hardware_renderer_->draw_line({x0, y0, 0, 0, color0},
                                      {x1, y1, 0, 0, color1},
                                      state, gouraud, semi_transparent);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    const int dx_pixels = std::abs(x1 - x0);
    const int dy_pixels = std::abs(y1 - y0);
    const int steps = std::max(1, std::max(dx_pixels, dy_pixels));
    int64_t x = int64_t(x0) * 65536;
    int64_t y = int64_t(y0) * 65536;
    const int64_t dx = (int64_t(x1) * 65536 - x) / steps;
    const int64_t dy = (int64_t(y1) * 65536 - y) / steps;
    std::array<int64_t, 3> channel{};
    std::array<int64_t, 3> dchannel{};
    for (unsigned c = 0; c < 3; ++c) {
        const unsigned shift = c * 8;
        channel[c] = int64_t((color0 >> shift) & 0xff) << 16;
        dchannel[c] = ((int64_t((color1 >> shift) & 0xff) -
                        int64_t((color0 >> shift) & 0xff)) * 65536) / steps;
    }
    const unsigned abr = gpu_type_ == 2 ? ((texture_page_ >> 5) & 3)
                                         : ((texture_page_ >> 7) & 3);
    const int vram_height = int(std::max<size_t>(1, vram_.size() / (1024 * 2)));
    for (int i = 0; i < steps; ++i) {
        const int px = int(x >> 16) + draw_offset_x_;
        const int py = int(y >> 16) + draw_offset_y_;
        if (px >= std::max(0, draw_area_x1_) &&
            px <= std::min(1023, draw_area_x2_) &&
            py >= std::max(0, draw_area_y1_) &&
            py <= std::min(vram_height - 1, draw_area_y2_)) {
            uint16_t out = gouraud
                ? uint16_t(std::clamp<int64_t>(channel[0] >> 19, 0, 31) |
                           (std::clamp<int64_t>(channel[1] >> 19, 0, 31) << 5) |
                           (std::clamp<int64_t>(channel[2] >> 19, 0, 31) << 10))
                : uint16_t(((color0 >> 3) & 31) |
                           (((color0 >> 11) & 31) << 5) |
                           (((color0 >> 19) & 31) << 10));
            if (semi_transparent) out = blend_rgb555(get_pixel(px, py), out, abr);
            write_pixel(px, py, out);
        }
        x += dx;
        y += dy;
        if (gouraud)
            for (unsigned c = 0; c < 3; ++c) channel[c] += dchannel[c];
    }
}

// Port of MAME's psxgpu_device::FlatPolygon scan converter (BSD-3-Clause,
// copyright smf). It follows the GPU's left/right edge chains and fills each
// scanline [left,right), instead of testing pixel centers against triangles.
void Gpu::draw_flat_polygon(const int *x, const int *y, unsigned count,
                            uint16_t color, bool semi_transparent)
{
    if (hardware_renderer_) {
        std::array<GpuHwVertex, 4> vertices{};
        for (unsigned i = 0; i < count && i < vertices.size(); ++i)
            vertices[i] = {x[i], y[i], 0, 0, color};
        GpuHwState state;
        get_hardware_state(state);
        hardware_renderer_->draw_polygon(std::span(vertices.data(), std::min(count, 4u)),
                                         state, false, false, false,
                                         semi_transparent, 0);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    PolygonEdges polygon{};
    if (!find_polygon_edges(x, y, count, polygon)) return;
    count = polygon.count;
    const int *next = polygon.next;
    const int *prev = polygon.prev;
    int left = polygon.left;
    const int max_y = polygon.max_y;
    int right = left;
    // MAME begins both edge walkers at the selected top-left vertex. Using a
    // global min Y can advance the interpolators through vertices belonging
    // to a culled triangle before the surviving quad chain starts.
    int scan_y = y[left];
    int64_t left_x = int64_t(x[left]) << 16;
    int64_t right_x = left_x;
    int64_t left_dx = 0, right_dx = 0;
    const int vram_height = int(std::max<size_t>(1, vram_.size() / (1024 * 2)));
    const unsigned abr = gpu_type_ == 2 ? ((texture_page_ >> 5) & 3)
                                         : ((texture_page_ >> 7) & 3);
    while (scan_y < max_y) {
        if (scan_y == y[left]) {
            unsigned steps = 0;
            while (y[prev[left]] == scan_y && steps++ < count) left = prev[left];
            const int from = left;
            left_x = int64_t(x[from]) << 16;
            left = prev[left];
            const int dy = y[left] - scan_y;
            if (dy <= 0) break;
            left_dx = ((int64_t(x[left]) << 16) - left_x) / dy;
        }
        if (scan_y == y[right]) {
            unsigned steps = 0;
            while (y[next[right]] == scan_y && steps++ < count) right = next[right];
            const int from = right;
            right_x = int64_t(x[from]) << 16;
            right = next[right];
            const int dy = y[right] - scan_y;
            if (dy <= 0) break;
            right_dx = ((int64_t(x[right]) << 16) - right_x) / dy;
        }
        const int py = scan_y + draw_offset_y_;
        const int lx = int(left_x >> 16) + draw_offset_x_;
        const int rx = int(right_x >> 16) + draw_offset_x_;
        if (lx != rx && py >= draw_area_y1_ && py <= draw_area_y2_ &&
            py >= 0 && py < vram_height) {
            const int start = std::max(std::min(lx, rx), draw_area_x1_);
            const int end = std::min(std::max(lx, rx), draw_area_x2_ + 1);
            for (int px = std::max(0, start); px < std::min(1024, end); ++px) {
                const uint16_t out = semi_transparent
                    ? blend_rgb555(get_pixel(px, py), color, abr) : color;
                write_pixel(px, py, out);
            }
        }
        left_x += left_dx;
        right_x += right_dx;
        ++scan_y;
    }
}

void Gpu::draw_gouraud_polygon(const int *x, const int *y,
                               const uint32_t *color, unsigned count,
                               bool semi_transparent)
{
    if (hardware_renderer_) {
        std::array<GpuHwVertex, 4> vertices{};
        for (unsigned i = 0; i < count && i < vertices.size(); ++i)
            vertices[i] = {x[i], y[i], 0, 0, color[i]};
        GpuHwState state;
        get_hardware_state(state);
        hardware_renderer_->draw_polygon(std::span(vertices.data(), std::min(count, 4u)),
                                         state, false, true, false,
                                         semi_transparent, 0);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    PolygonEdges polygon{};
    if (!color || !find_polygon_edges(x, y, count, polygon)) return;
    count = polygon.count;
    const int *next = polygon.next;
    const int *prev = polygon.prev;
    int left = polygon.left;
    int right = left;
    int scan_y = y[left];
    int64_t lx = int64_t(x[left]) << 16;
    int64_t rx = lx;
    std::array<int64_t, 3> lc{}, rc{};
    auto set_color = [&](std::array<int64_t, 3> &out, unsigned vertex) {
        for (unsigned c = 0; c < 3; ++c)
            out[c] = int64_t((color[vertex] >> (c * 8)) & 255) << 16;
    };
    set_color(lc, left);
    rc = lc;
    int64_t ldx = 0, rdx = 0;
    std::array<int64_t, 3> ldc{}, rdc{};
    auto set_edge = [&](bool is_left, int from, int to, int dy) {
        const int64_t dx = ((int64_t(x[to]) - x[from]) * 65536) / dy;
        std::array<int64_t, 3> dc{};
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned shift = c * 8;
            dc[c] = ((int64_t((color[to] >> shift) & 255) -
                      int64_t((color[from] >> shift) & 255)) * 65536) / dy;
        }
        if (is_left) { ldx = dx; ldc = dc; }
        else { rdx = dx; rdc = dc; }
    };
    const int vram_height = int(std::max<size_t>(1, vram_.size() / (1024 * 2)));
    const unsigned abr = gpu_type_ == 2 ? ((texture_page_ >> 5) & 3)
                                         : ((texture_page_ >> 7) & 3);
    while (scan_y < polygon.max_y) {
        if (scan_y == y[left]) {
            unsigned steps = 0;
            while (y[prev[left]] == scan_y && steps++ < count) left = prev[left];
            const int from = left;
            lx = int64_t(x[from]) << 16;
            set_color(lc, from);
            left = prev[left];
            const int dy = y[left] - scan_y;
            if (dy <= 0) break;
            set_edge(true, from, left, dy);
        }
        if (scan_y == y[right]) {
            unsigned steps = 0;
            while (y[next[right]] == scan_y && steps++ < count) right = next[right];
            const int from = right;
            rx = int64_t(x[from]) << 16;
            set_color(rc, from);
            right = next[right];
            const int dy = y[right] - scan_y;
            if (dy <= 0) break;
            set_edge(false, from, right, dy);
        }
        const int py = scan_y + draw_offset_y_;
        if ((lx >> 16) != (rx >> 16) && py >= draw_area_y1_ &&
            py <= draw_area_y2_ && py >= 0 && py < vram_height) {
            const int left_x = int(lx >> 16) + draw_offset_x_;
            const int right_x = int(rx >> 16) + draw_offset_x_;
            const bool left_is_first = left_x < right_x;
            const int x_start = std::min(left_x, right_x);
            const int x_end = std::max(left_x, right_x);
            const int distance = x_end - x_start;
            if (distance > 0) {
                auto start = left_is_first ? lc : rc;
                const auto end = left_is_first ? rc : lc;
                std::array<int64_t, 3> delta{};
                for (unsigned c = 0; c < 3; ++c)
                    delta[c] = (end[c] - start[c]) / distance;
                const int clipped_start = std::max({x_start, draw_area_x1_, 0});
                const int clipped_end = std::min({x_end, draw_area_x2_ + 1, 1024});
                const int skip = clipped_start - x_start;
                for (unsigned c = 0; c < 3; ++c) start[c] += delta[c] * skip;
                for (int px = clipped_start; px < clipped_end; ++px) {
                    const uint16_t color15 = uint16_t(
                        (std::clamp(int(start[0] >> 16) / 8, 0, 31)) |
                        (std::clamp(int(start[1] >> 16) / 8, 0, 31) << 5) |
                        (std::clamp(int(start[2] >> 16) / 8, 0, 31) << 10));
                    const uint16_t out = semi_transparent
                        ? blend_rgb555(get_pixel(px, py), color15, abr) : color15;
                    write_pixel(px, py, out);
                    for (unsigned c = 0; c < 3; ++c) start[c] += delta[c];
                }
            }
        }
        lx += ldx;
        rx += rdx;
        for (unsigned c = 0; c < 3; ++c) { lc[c] += ldc[c]; rc[c] += rdc[c]; }
        ++scan_y;
    }
}

uint16_t Gpu::sample_texture(int u, int v, uint32_t clut, uint32_t tpage) const
{
    // Match psxgpu_device::decode_tpage: type 2 and type 1 encode the
    // texture page differently. The ZN-2 CXD8654Q uses ABR bits 5-6,
    // depth bits 7-8, Y-page bits 4/11, and flip bits 12/13.
    const unsigned depth = gpu_type_ == 2 ? ((tpage >> 7) & 3)
                                          : ((tpage >> 9) & 3);
    const int page_x = int(tpage & 0xf) * 64;
    const int page_y = gpu_type_ == 2
        ? int(((tpage & 0x10) << 4) | ((tpage & 0x800) >> 2))
        : int(tpage & 0x60) << 3;
    u &= int(texture_window_mask_x_);
    v &= int(texture_window_mask_y_);
    const bool interleaved = gpu_type_ != 2 && (tpage & 0x2000) != 0;
    if (depth == 3) return 0;
    const unsigned per_word = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    const int base_x = page_x + int(texture_window_x_) / int(per_word);
    const int base_y = page_y + int(texture_window_y_);
    if (depth == 2) return get_pixel_linear(base_x + u, base_y + v);
    const int clut_x = int(clut & 0x3f) * 16;
    const int clut_y = int((clut >> 6) & 0x3ff);
    int tex_x = u / int(per_word);
    int tex_y = v;
    if (interleaved && depth == 0) {
        tex_x = ((u >> 2) & ~0x3c) + ((v << 2) & 0x3c);
        tex_y = (v & ~0xf) + ((u >> 4) & 0xf);
    } else if (interleaved && depth == 1) {
        tex_x = ((u >> 1) & ~0x78) + ((u << 2) & 0x40) + ((v << 3) & 0x38);
        tex_y = (v & ~0x7) + ((u >> 5) & 0x7);
    }
    const uint16_t packed = get_pixel_linear(base_x + tex_x, base_y + tex_y);
    const unsigned index = depth == 0 ? (packed >> ((u & 3) * 4)) & 0xf
                                      : (packed >> ((u & 1) * 8)) & 0xff;
    return get_pixel_linear(clut_x + int(index), clut_y);
}

void Gpu::decode_tpage(uint32_t tpage)
{
    if (gpu_type_ == 2) {
        texture_page_ = tpage & 0x39ff;
        status_ = (status_ & 0xffff7800u) | (tpage & 0x7ffu) |
                  ((tpage & 0x800u) << 4);
    } else {
        texture_page_ = tpage & 0x27ef;
        status_ = (status_ & 0xffffe000u) | (tpage & 0x1fffu);
    }
}

void Gpu::draw_textured_polygon(const int *x, const int *y, const int *u, const int *v,
                                const uint32_t *color, unsigned count,
                                uint32_t clut, uint32_t tpage, bool raw_texture,
                                bool gouraud, bool semi_transparent)
{
    if (!u || !v || !color) return;
    if (hardware_renderer_) {
        std::array<GpuHwVertex, 4> vertices{};
        for (unsigned i = 0; i < count && i < vertices.size(); ++i)
            vertices[i] = {x[i], y[i], u[i], v[i], color[i]};
        GpuHwState state;
        get_hardware_state(state);
        state.texture_page = tpage;
        hardware_renderer_->draw_polygon(std::span(vertices.data(), std::min(count, 4u)),
                                         state, true, gouraud, raw_texture,
                                         semi_transparent, clut);
        hardware_vram_shadow_valid_ = false;
        return;
    }
    PolygonEdges polygon{};
    if (!find_polygon_edges(x, y, count, polygon)) return;
    count = polygon.count;
    const int *next = polygon.next;
    const int *prev = polygon.prev;
    int left = polygon.left;
    const int max_y = polygon.max_y;
    int right = left;
    int scan_y = y[left];
    int64_t lx = int64_t(x[left]) * 65536, rx = lx;
    int64_t lu = int64_t(u[left]) << 16, ru = lu;
    int64_t lv = int64_t(v[left]) << 16, rv = lv;
    std::array<int64_t, 3> lc{}, rc{};
    auto set_color = [&](std::array<int64_t, 3> &out, unsigned vertex) {
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned shift = c == 0 ? 0 : c == 1 ? 8 : 16;
            out[c] = int64_t((color[vertex] >> shift) & 255) << 16;
        }
    };
    set_color(lc, left); rc = lc;
    int64_t ldx = 0, rdx = 0, ldu = 0, rdu = 0, ldv = 0, rdv = 0;
    std::array<int64_t, 3> ldc{}, rdc{};
    const int vram_height = int(std::max<size_t>(1, vram_.size() / (1024 * 2)));
    const unsigned abr = gpu_type_ == 2 ? ((tpage >> 5) & 3) : ((tpage >> 7) & 3);
    const unsigned texture_depth = gpu_type_ == 2 ? ((tpage >> 7) & 3)
                                                  : ((tpage >> 9) & 3);
    const unsigned pixels_per_word = texture_depth == 0 ? 4 :
                                     texture_depth == 1 ? 2 : 1;
    const int texture_page_x = int(tpage & 0xf) * 64;
    const int texture_page_y = gpu_type_ == 2
        ? int(((tpage & 0x10) << 4) | ((tpage & 0x800) >> 2))
        : int(tpage & 0x60) << 3;
    const int texture_base_x = texture_page_x +
        int(texture_window_x_) / int(pixels_per_word);
    const int texture_base_y = texture_page_y + int(texture_window_y_);
    const int clut_x = int(clut & 0x3f) * 16;
    const int clut_y = int((clut >> 6) & 0x3ff);
    const bool interleaved = gpu_type_ != 2 && (tpage & 0x2000) != 0;
    const auto sample_texel = [&](int u, int v) -> uint16_t {
        u &= int(texture_window_mask_x_);
        v &= int(texture_window_mask_y_);
        if (texture_depth == 3) return 0;
        if (texture_depth == 2)
            return get_pixel_linear(texture_base_x + u, texture_base_y + v);
        int tex_x = u / int(pixels_per_word);
        int tex_y = v;
        if (interleaved && texture_depth == 0) {
            tex_x = ((u >> 2) & ~0x3c) + ((v << 2) & 0x3c);
            tex_y = (v & ~0xf) + ((u >> 4) & 0xf);
        } else if (interleaved && texture_depth == 1) {
            tex_x = ((u >> 1) & ~0x78) + ((u << 2) & 0x40) + ((v << 3) & 0x38);
            tex_y = (v & ~0x7) + ((u >> 5) & 0x7);
        }
        const uint16_t packed = get_pixel_linear(texture_base_x + tex_x,
                                                  texture_base_y + tex_y);
        const unsigned index = texture_depth == 0
            ? (packed >> ((u & 3) * 4)) & 0xf
            : (packed >> ((u & 1) * 8)) & 0xff;
        return get_pixel_linear(clut_x + int(index), clut_y);
    };
    auto set_edge = [&](bool is_left, int from, int to, int dy) {
        const int64_t dx = ((int64_t(x[to]) - x[from]) * 65536) / dy;
        const int64_t du = ((int64_t(u[to]) - u[from]) * 65536) / dy;
        const int64_t dv = ((int64_t(v[to]) - v[from]) * 65536) / dy;
        std::array<int64_t, 3> dc{};
        for (unsigned c = 0; c < 3; ++c) {
            const unsigned shift = c == 0 ? 0 : c == 1 ? 8 : 16;
            dc[c] = ((int64_t((color[to] >> shift) & 255) -
                      int64_t((color[from] >> shift) & 255)) * 65536) / dy;
        }
        if (is_left) { ldx = dx; ldu = du; ldv = dv; ldc = dc; }
        else { rdx = dx; rdu = du; rdv = dv; rdc = dc; }
    };
    while (scan_y < max_y) {
        if (scan_y == y[left]) {
            unsigned steps = 0;
            while (y[prev[left]] == scan_y && steps++ < count) left = prev[left];
            const int from = left;
            lx = int64_t(x[from]) * 65536; lu = int64_t(u[from]) << 16;
            lv = int64_t(v[from]) << 16; set_color(lc, from);
            left = prev[left];
            const int dy = y[left] - scan_y;
            if (dy <= 0) break;
            set_edge(true, from, left, dy);
        }
        if (scan_y == y[right]) {
            unsigned steps = 0;
            while (y[next[right]] == scan_y && steps++ < count) right = next[right];
            const int from = right;
            rx = int64_t(x[from]) * 65536; ru = int64_t(u[from]) << 16;
            rv = int64_t(v[from]) << 16; set_color(rc, from);
            right = next[right];
            const int dy = y[right] - scan_y;
            if (dy <= 0) break;
            set_edge(false, from, right, dy);
        }
        const int py = scan_y + draw_offset_y_;
        if ((lx >> 16) != (rx >> 16) && py >= draw_area_y1_ &&
            py <= draw_area_y2_ && py >= 0 && py < vram_height) {
            const int left_x = int(lx >> 16) + draw_offset_x_;
            const int right_x = int(rx >> 16) + draw_offset_x_;
            const bool left_is_first = left_x < right_x;
            int x_start = std::min(left_x, right_x);
            const int x_end = std::max(left_x, right_x);
            int64_t start_u = left_is_first ? lu : ru;
            int64_t start_v = left_is_first ? lv : rv;
            const int64_t end_u = left_is_first ? ru : lu;
            const int64_t end_v = left_is_first ? rv : lv;
            std::array<int64_t, 3> start_c = left_is_first ? lc : rc;
            const std::array<int64_t, 3> end_c = left_is_first ? rc : lc;
            const int distance = x_end - x_start;
            if (distance > 0) {
                const int64_t du = (end_u - start_u) / distance;
                const int64_t dv = (end_v - start_v) / distance;
                std::array<int64_t, 3> dc{};
                for (unsigned c = 0; c < 3; ++c)
                    dc[c] = (end_c[c] - start_c[c]) / distance;
                const int clipped_start = std::max({x_start, draw_area_x1_, 0});
                const int clipped_end = std::min({x_end, draw_area_x2_ + 1, 1024});
                const int skip = clipped_start - x_start;
                const size_t destination_row = size_t(py) * 1024;
                start_u += du * skip; start_v += dv * skip;
                for (unsigned c = 0; c < 3; ++c) start_c[c] += dc[c] * skip;
                for (int px = clipped_start; px < clipped_end; ++px) {
                    const uint16_t texel = sample_texel(int(start_u >> 16),
                                                        int(start_v >> 16));
                    if (texel) {
                        uint16_t out = texel;
                        if (!raw_texture) {
                            const int cr = gouraud ? int(start_c[0] >> 16) : int(color[0] & 255);
                            const int cg = gouraud ? int(start_c[1] >> 16) : int((color[0] >> 8) & 255);
                            const int cb = gouraud ? int(start_c[2] >> 16) : int((color[0] >> 16) & 255);
                            out = uint16_t(std::clamp(cr * int(texel & 31) / 128, 0, 31) |
                                (std::clamp(cg * int((texel >> 5) & 31) / 128, 0, 31) << 5) |
                                (std::clamp(cb * int((texel >> 10) & 31) / 128, 0, 31) << 10) |
                                (texel & 0x8000));
                        }
                        const size_t destination = (destination_row + size_t(px)) * 2;
                        bool destination_loaded = false;
                        uint16_t previous = 0;
                        if (semi_transparent && (texel & 0x8000)) {
                            previous = uint16_t(vram_[destination] |
                                (uint16_t(vram_[destination + 1]) << 8));
                            destination_loaded = true;
                            out = blend_rgb555(previous, out, abr);
                        }
                        const bool blocked = check_mask_ &&
                            ((destination_loaded ? previous : uint16_t(
                                vram_[destination] |
                                (uint16_t(vram_[destination + 1]) << 8))) & 0x8000);
                        if (!blocked) {
                            out = static_cast<uint16_t>(out | (draw_mask_ ? 0x8000 : 0));
                            vram_[destination] = uint8_t(out);
                            vram_[destination + 1] = uint8_t(out >> 8);
                        }
                    }
                    start_u += du; start_v += dv;
                    for (unsigned c = 0; c < 3; ++c) start_c[c] += dc[c];
                }
            }
        }
        lx += ldx; rx += rdx; lu += ldu; ru += rdu; lv += ldv; rv += rdv;
        for (unsigned c = 0; c < 3; ++c) { lc[c] += ldc[c]; rc[c] += rdc[c]; }
        ++scan_y;
    }
}

void Gpu::gp0(uint32_t value)
{
    if (transfer_to_vram_) {
        const uint32_t total = transfer_w_ * transfer_h_;
        for (unsigned half = 0; half < 2 && transfer_pos_ < total; ++half, ++transfer_pos_)
            write_pixel(int(transfer_x_ + transfer_pos_ % transfer_w_),
                      int(transfer_y_ + transfer_pos_ / transfer_w_),
                      uint16_t(value >> (half * 16)));
        if (transfer_pos_ >= total) {
            transfer_to_vram_ = false;
            if (hardware_renderer_) {
                hardware_renderer_->sync_from_cpu(vram_);
                hardware_vram_shadow_valid_ = true;
            }
        }
        return;
    }
    if (packet_.empty()) {
        const unsigned op = value >> 24;
        const unsigned words = gp0_packet_words(op);
        ++gp0_command_counts_[op];
        static const bool trace_gp0 = std::getenv("MAMERECOMP_ZN_TRACE_GP0") != nullptr;
        if (trace_gp0 &&
            (op == 0x02 || (op >= 0x20 && op <= 0x7f) || op == 0x80 ||
             op == 0xa0 || op == 0xc0 || op >= 0xe1)) {
            static unsigned traced = 0;
            if (traced++ < 256)
                std::cerr << "ZN GP0 op=0x" << std::hex << op << " word=0x" << value
                          << " words=" << std::dec << words << '\n';
        }
        packet_.reserve(words); packet_.push_back(value);
        if (is_gp0_polyline(op)) return;
        if (words == 1) execute_packet();
    } else {
        const unsigned op = packet_[0] >> 24;
        if (is_gp0_polyline(op)) {
            if (is_gp0_polyline_terminator(value)) {
                execute_packet();
                return;
            }
            packet_.push_back(value);
            return;
        }
        const unsigned words = gp0_packet_words(op);
        packet_.push_back(value);
        if (packet_.size() == words) execute_packet();
    }
}

void Gpu::execute_packet()
{
    const auto profile_start = profile_timing_
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const uint32_t opword = packet_[0];
    const unsigned op = opword >> 24;
    const uint16_t color = rgb24_to_15(opword);
    static const bool trace_gp0_packets =
        std::getenv("MAMERECOMP_ZN_TRACE_GP0_PACKETS") != nullptr;
    if (op == 0x65 && trace_gp0_packets) {
        static unsigned traced_sprite_packets = 0;
        if (traced_sprite_packets++ < 16) {
            std::cerr << "ZN GP0 sprite65";
            for (uint32_t word : packet_)
                std::cerr << " 0x" << std::hex << word;
            std::cerr << " tpage=0x" << texture_page_
                      << " area=(" << draw_area_x1_ << ',' << draw_area_y1_
                      << "-" << draw_area_x2_ << ',' << draw_area_y2_ << ')'
                      << " offset=(" << std::dec << draw_offset_x_ << ',' << draw_offset_y_ << ")\n";
        }
    }
    if (op == 0x2d && trace_gp0_packets) {
        static unsigned traced_quad_packets = 0;
        if (traced_quad_packets++ < 20) {
            std::cerr << "ZN GP0 quad2d";
            for (uint32_t word : packet_)
                std::cerr << " 0x" << std::hex << word;
            std::cerr << " tpage=0x" << texture_page_
                      << " area=(" << draw_area_x1_ << ',' << draw_area_y1_
                      << "-" << draw_area_x2_ << ',' << draw_area_y2_
                      << ") offset=(" << std::dec << draw_offset_x_ << ',' << draw_offset_y_ << ")\n";
        }
    }
    if (op == 0xe1) {
        decode_tpage(opword);
        texture_flip_x_ = (gpu_type_ == 1 || gpu_type_ == 2) && (opword & 0x1000) ? -1 : 1;
        texture_flip_y_ = (gpu_type_ == 1 || gpu_type_ == 2) && (opword & 0x2000) ? -1 : 1;
    } else if (op == 0xe6) {
        draw_mask_ = (opword & 1) != 0;
        check_mask_ = (opword & 2) != 0;
        status_ = (status_ & ~(3u << 11)) | ((opword & 3) << 11);
    } else if (op == 0xe2) {
        texture_window_x_ = ((opword >> 10) & 0x1f) << 3;
        texture_window_y_ = ((opword >> 15) & 0x1f) << 3;
        texture_window_mask_x_ = 255 - (((opword >> 5) & 0x1f) << 3);
        texture_window_mask_y_ = 255 - ((opword & 0x1f) << 3);
    } else if (op == 0xe3) {
        draw_area_x1_ = opword & 0x3ff;
        draw_area_y1_ = (opword >> (gpu_type_ == 2 ? 10 : 12)) & 0x3ff;
    } else if (op == 0xe4) {
        draw_area_x2_ = opword & 0x3ff;
        draw_area_y2_ = (opword >> (gpu_type_ == 2 ? 10 : 12)) & 0x3ff;
    } else if (op == 0xe5) {
        auto sign11 = [](uint32_t v) {
            return int((v & 0x400) ? (v | 0xfffff800) : (v & 0x7ff));
        };
        draw_offset_x_ = sign11(opword);
        draw_offset_y_ = sign11(opword >> 11);
    } else if (op == 0x02) {
        const int x = int(packet_[1] & 0x3f0), y = int((packet_[1] >> 16) & 0x3ff);
        const int w = int(packet_[2] & 0x3ff), h = int((packet_[2] >> 16) & 0x3ff);
        fill_rect(x, y, w ? w : 1024, h ? h : 512, color);
    } else if ((op & 0xe0) == 0x20) {
        const unsigned vertices = (op & 8) ? 4 : 3;
        const bool textured = (op & 4) != 0, gouraud = (op & 0x10) != 0;
        int x[4]{}, y[4]{}, u[4]{}, v[4]{};
        uint32_t colors[4]{};
        unsigned cursor = 1;
        uint32_t clut = 0, tpage = 0;
        colors[0] = opword;
        for (unsigned i = 0; i < vertices; ++i) {
            if (i && gouraud) colors[i] = packet_[cursor++];
            else if (!gouraud) colors[i] = opword;
            const uint32_t pos = packet_[cursor++];
            x[i] = coord_x(pos); y[i] = coord_y(pos);
            if (textured) {
                const uint32_t uv = packet_[cursor++];
                u[i] = uv & 255; v[i] = (uv >> 8) & 255;
                if (i == 0) clut = uv >> 16;
                if (i == 1) tpage = uv >> 16;
            }
        }
        if (!textured) {
            if (gouraud) draw_gouraud_polygon(x, y, colors, vertices, (op & 2) != 0);
            else draw_flat_polygon(x, y, vertices, color, (op & 2) != 0);
        } else {
            // MAME's FlatTexturedPolygon/GouraudTexturedPolygon call
            // decode_tpage() on the packet's second vertex. Besides using it
            // for this polygon, that updates the GPU's persistent draw mode
            // used by following sprite/rectangle commands.
            decode_tpage(tpage);
            texture_flip_x_ = (gpu_type_ == 1 || gpu_type_ == 2) && (tpage & 0x1000) ? -1 : 1;
            texture_flip_y_ = (gpu_type_ == 1 || gpu_type_ == 2) && (tpage & 0x2000) ? -1 : 1;
            draw_textured_polygon(x, y, u, v, colors, vertices, clut, tpage,
                                  (op & 1) != 0, gouraud, (op & 2) != 0);
        }
    } else if ((op & 0xf8) == 0x48) {
        if (packet_.size() >= 3) {
            uint32_t previous = packet_[1];
            for (size_t i = 2; i < packet_.size(); ++i) {
                const uint32_t next = packet_[i];
                draw_line(coord_x(previous), coord_y(previous), coord_x(next), coord_y(next),
                          opword, opword, false, (op & 2) != 0);
                previous = next;
            }
        }
    } else if ((op & 0xf8) == 0x58) {
        if (packet_.size() >= 4) {
            uint32_t previous_position = packet_[1];
            uint32_t previous_color = opword;
            for (size_t i = 2; i + 1 < packet_.size(); i += 2) {
                const uint32_t next_color = packet_[i];
                const uint32_t next_position = packet_[i + 1];
                draw_line(coord_x(previous_position), coord_y(previous_position),
                          coord_x(next_position), coord_y(next_position),
                          previous_color, next_color, true, (op & 2) != 0);
                previous_color = next_color;
                previous_position = next_position;
            }
        }
    } else if ((op & 0xfc) == 0x40) {
        const uint32_t p0 = packet_[1], p1 = packet_[2];
        draw_line(coord_x(p0), coord_y(p0), coord_x(p1), coord_y(p1),
                  opword, opword, false, (op & 2) != 0);
    } else if ((op & 0xfc) == 0x50) {
        const uint32_t p0 = packet_[1], p1 = packet_[3];
        draw_line(coord_x(p0), coord_y(p0), coord_x(p1), coord_y(p1),
                  opword, packet_[2], true, (op & 2) != 0);
    } else if (op >= 0x60 && op <= 0x7f) {
        const int x = coord_x(packet_[1]), y = coord_y(packet_[1]);
        int w = (op & 0x18) == 0x08 ? 1 : (op & 0x18) == 0x10 ? 8 :
                (op & 0x18) == 0x18 ? 16 : int(packet_[2] & 0x3ff);
        int h = (op & 0x18) == 0x08 ? 1 : (op & 0x18) == 0x10 ? 8 :
                (op & 0x18) == 0x18 ? 16 : int((packet_[2] >> 16) & 0x1ff);
        if (!(op & 4)) {
            draw_solid_rect(x, y, w, h, color, (op & 2) != 0);
        } else {
            const uint32_t uvword = packet_[2];
            const uint32_t clut = uvword >> 16;
            const uint32_t tpage = texture_page_;
            if ((op & 0x18) == 0) {
                w = int(packet_[3] & 0x3ff);
                h = int((packet_[3] >> 16) & 0x1ff);
            }
            const int u0 = int(uvword & 255), v0 = int((uvword >> 8) & 255);
            const int origin_x = x + draw_offset_x_;
            const int origin_y = y + draw_offset_y_;
            const bool raw = (op & 1) != 0;
            const bool semi = (op & 2) != 0;
            if (hardware_renderer_) {
                GpuHwState state;
                get_hardware_state(state);
                state.texture_page = tpage;
                hardware_renderer_->draw_textured_rect(x, y, w, h, u0, v0,
                    opword, clut, state, raw, semi);
                hardware_vram_shadow_valid_ = false;
            } else {
              const unsigned abr = gpu_type_ == 2 ? ((tpage >> 5) & 3)
                                                   : ((tpage >> 7) & 3);
              for (int dy = 0; dy < h; ++dy) {
                const int py = origin_y + dy;
                if (py < draw_area_y1_ || py > draw_area_y2_ || py < 0 || py > 1023) continue;
                const int tv = (v0 + dy * texture_flip_y_) & 255;
                for (int dx = 0; dx < w; ++dx) {
                    const int px = origin_x + dx;
                    if (px < draw_area_x1_ || px > draw_area_x2_ || px < 0 || px > 1023) continue;
                    const int tu = (u0 + dx * texture_flip_x_) & 255;
                    const uint16_t texel = sample_texture(tu, tv, clut, tpage);
                    if (texel == 0) continue;
                    uint16_t out = texel;
                    if (!raw) {
                        out = uint16_t(kModulationTable[texel & 31][opword & 0xff] |
                            (kModulationTable[(texel >> 5) & 31][(opword >> 8) & 0xff] << 5) |
                            (kModulationTable[(texel >> 10) & 31][(opword >> 16) & 0xff] << 10) |
                            (texel & 0x8000));
                    }
                    if (semi && (texel & 0x8000)) {
                        // MAME's TRANSPARENTPIXEL preserves STP on a blended
                        // texel. Keeping bit 15 matters when later polygons
                        // test this pixel for semi-transparency.
                        out = blend_rgb555(get_pixel(px, py), out, abr);
                    }
                    write_pixel(px, py, out);
                }
              }
            }
        }
    } else if (op == 0x80) {
        uint32_t sx = packet_[1] & 0x3ff, sy = (packet_[1] >> 16) & 0x3ff;
        uint32_t dx = packet_[2] & 0x3ff, dy = (packet_[2] >> 16) & 0x3ff;
        uint32_t w = packet_[3] & 0x3ff, h = (packet_[3] >> 16) & 0x3ff;
        if (!w) w = 1024; if (!h) h = 512;
        if (hardware_renderer_) {
            GpuHwState state;
            get_hardware_state(state);
            hardware_renderer_->copy_rect(int(sx), int(sy), int(dx), int(dy),
                                          int(w), int(h), state);
            hardware_vram_shadow_valid_ = false;
        } else {
            std::vector<uint16_t> tmp(size_t(w) * h);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                    tmp[size_t(y) * w + x] = get_pixel(int(sx + x), int(sy + y));
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                    write_pixel(int(dx + x), int(dy + y), tmp[size_t(y) * w + x]);
        }
    } else if (op == 0xa0 || op == 0xc0) {
        // CPU-to-VRAM transfers update the host mirror a word at a time. Merge
        // prior GPU draws first so the full upload at transfer completion does
        // not overwrite pixels that existed only in device VRAM.
        if (op == 0xa0 && hardware_renderer_ && !hardware_vram_shadow_valid_) {
            hardware_renderer_->sync_to_cpu(vram_);
            hardware_vram_shadow_valid_ = true;
        }
        transfer_x_ = packet_[1] & 0x3ff; transfer_y_ = (packet_[1] >> 16) & 0x3ff;
        transfer_w_ = packet_[2] & 0x3ff; transfer_h_ = (packet_[2] >> 16) & 0x3ff;
        if (!transfer_w_) transfer_w_ = 1024; if (!transfer_h_) transfer_h_ = 512;
        transfer_pos_ = 0;
        if (op == 0xc0) {
            transfer_from_vram_ = true;
            transfer_to_vram_ = false;
            // MAME leaves the C0 packet active and reads two pixels lazily
            // for each GPUREAD/DMA word. Do not snapshot the entire rectangle:
            // the CPU can observe VRAM changes between successive reads.
            status_ |= 1u << 27;
        } else {
            transfer_from_vram_ = false;
            transfer_to_vram_ = true;
        }
    }
    packet_.clear();
    if (profile_timing_) {
        profile_execute_packet_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - profile_start).count());
        ++profile_execute_packet_calls_;
    }
}

uint32_t Gpu::read_data()
{
    if (!(status_ & (1u << 27)) || !transfer_from_vram_) return gpu_info_;
    if (hardware_renderer_ && !hardware_vram_shadow_valid_) {
        hardware_renderer_->sync_to_cpu(vram_);
        hardware_vram_shadow_valid_ = true;
    }
    const uint32_t total = transfer_w_ * transfer_h_;
    if (transfer_pos_ >= total) {
        transfer_pos_ = 0;
        transfer_from_vram_ = false;
        status_ &= ~(1u << 27);
        return gpu_info_;
    }
    uint32_t result = 0;
    for (unsigned half = 0; half < 2; ++half) {
        if (transfer_pos_ >= total) {
            // MAME zero-fills the unused high halfword when the image has an
            // odd pixel count and ends halfway through a GPUREAD word.
            transfer_pos_ = 0;
            transfer_from_vram_ = false;
            status_ &= ~(1u << 27);
            break;
        }
        const uint32_t x = (transfer_x_ + transfer_pos_ % transfer_w_) & 0x3ff;
        const uint32_t y = (transfer_y_ + transfer_pos_ / transfer_w_) & 0x3ff;
        result |= uint32_t(get_pixel(int(x), int(y))) << (half * 16);
        ++transfer_pos_;
        if (transfer_pos_ == total) {
            transfer_pos_ = 0;
            transfer_from_vram_ = false;
            status_ &= ~(1u << 27);
            break;
        }
    }
    return result;
}
uint32_t Gpu::read_status() const
{
    uint32_t value = status_;
    // MAME adds GPUSTAT bit 31 dynamically from the current screen vpos for
    // non-interlaced modes; in interlace it reports the field bit updated at
    // each vblank edge in advance_screen_timing().
    if (((status_ & (1u << 22)) && (status_ & (1u << 13))) ||
        (!(status_ & (1u << 22)) && screen_vpos_odd_))
        value |= 1u << 31;
    return value;
}
void Gpu::write_data(uint32_t value) { gp0(value); }
void Gpu::write_status(uint32_t value)
{
    const unsigned op = value >> 24;
    static const bool trace_commands =
        std::getenv("MAMERECOMP_ZN_TRACE_GPU_COMMANDS") != nullptr;
    static unsigned traced_commands = 0;
    auto trace_command = [&] {
        if (trace_commands && traced_commands++ < 512)
            std::cerr << "ZN GP1 command=0x" << std::hex << value
                      << " status=0x" << status_
                      << " display=" << ((status_ & (1u << 23)) ? "disabled" : "enabled")
                      << std::dec << '\n';
    };
    if (op == 0) {
        status_ = 0x14802000;
        display_start_x_ = display_start_y_ = 0;
        horizontal_start_ = 0x260; horizontal_end_ = 0xc60;
        vertical_start_ = 0x10; vertical_end_ = 0x100;
        texture_page_ = 0;
        draw_mask_ = false; check_mask_ = false;
        texture_window_x_ = texture_window_y_ = 0;
        texture_window_mask_x_ = texture_window_mask_y_ = 255;
        draw_area_x1_ = draw_area_y1_ = 0;
        draw_area_x2_ = draw_area_y2_ = 1023;
        draw_offset_x_ = draw_offset_y_ = 0;
        texture_flip_x_ = texture_flip_y_ = 1;
        packet_.clear(); transfer_to_vram_ = false; transfer_from_vram_ = false;
        trace_command();
    }
    else if (op == 1) { packet_.clear(); transfer_to_vram_ = false; trace_command(); }
    else if (op == 3) { status_ = (status_ & ~(1u << 23)) | ((value & 1) << 23); trace_command(); }
    else if (op == 4) {
        status_ = (status_ & ~(3u << 29)) | ((value & 3) << 29);
        status_ &= ~(1u << 25);
        if ((value & 3) == 1 || (value & 3) == 2) status_ |= 1u << 25;
        trace_command();
    }
    else if (op == 5) {
        display_start_x_ = value & 0x3ff;
        display_start_y_ = (value >> (gpu_type_ == 2 ? 10 : 12)) & 0x3ff;
        trace_command();
    }
    else if (op == 6) {
        horizontal_start_ = value & 0xfff;
        horizontal_end_ = (value >> 12) & 0xfff;
        trace_command();
    }
    else if (op == 7) {
        vertical_start_ = value & 0x3ff;
        vertical_end_ = (value >> 10) & 0x7ff;
        trace_command();
    }
    else if (op == 8) {
        status_ = (status_ & ~(0x7fu << 16)) | ((value & 0x3f) << 17) |
                  (((value >> 6) & 1) << 16);
        trace_command();
    }
    else if (op == 0x10) {
        // MAME's GP1(10h) readback register is returned by GPUREAD whenever
        // no GP0 C0 VRAM-to-CPU transfer is active.
        switch (value & 0xff) {
        case 0x03:
            gpu_info_ = gpu_type_ == 2
                ? uint32_t(draw_area_x1_) | (uint32_t(draw_area_y1_) << 10)
                : uint32_t(draw_area_x1_) | (uint32_t(draw_area_y1_) << 12);
            break;
        case 0x04:
            gpu_info_ = gpu_type_ == 2
                ? uint32_t(draw_area_x2_) | (uint32_t(draw_area_y2_) << 10)
                : uint32_t(draw_area_x2_) | (uint32_t(draw_area_y2_) << 12);
            break;
        case 0x05:
            gpu_info_ = gpu_type_ == 2
                ? (uint32_t(draw_offset_x_) & 0x7ff) |
                    ((uint32_t(draw_offset_y_) & 0x7ff) << 11)
                : (uint32_t(draw_offset_x_) & 0x7ff) |
                    ((uint32_t(draw_offset_y_) & 0x7ff) << 12);
            break;
        case 0x07: gpu_info_ = gpu_type_; break;
        case 0x08: gpu_info_ = 0; break; // lightgun coordinates are not present on ZN.
        default: gpu_info_ = 0; break;
        }
        trace_command();
    }
}
void Gpu::dma_write(std::span<const uint32_t> words) { for (uint32_t v : words) gp0(v); }
std::vector<uint32_t> Gpu::dma_read(size_t words)
{
    std::vector<uint32_t> out; out.reserve(words);
    while (words--) out.push_back(read_data());
    return out;
}

GpuFrame Gpu::framebuffer_rgb() const
{
    GpuFrame frame;
    const unsigned width_code = (status_ >> 17) & 3;
    const unsigned width_high = (status_ >> 16) & 1;
    switch (width_code) {
    case 0: frame.width = width_high ? 368 : 256; break;
    case 1: frame.width = width_high ? 384 : 320; break;
    case 2: frame.width = 512; break;
    default: frame.width = 640; break;
    }
    const bool pal = (status_ & (1u << 20)) != 0;
    const bool high_resolution = (status_ & (1u << 19)) != 0;
    frame.height = pal ? (high_resolution ? 512 : 256)
                       : (high_resolution ? 480 : 240);
    frame.display_enabled = (status_ & (1u << 23)) == 0;
    static unsigned summary_frame = 0;
    static const bool summary_enabled =
        std::getenv("MAMERECOMP_ZN_TRACE_GP0_SUMMARY") != nullptr;
    static const bool summary_all =
        std::getenv("MAMERECOMP_ZN_TRACE_GP0_SUMMARY_ALL") != nullptr;
    const bool report_summary = summary_enabled &&
        (summary_all || ((summary_frame++ % 60) == 0));
    // Diagnostic summaries inspect the host VRAM mirror. Keep that opt-in
    // path correct without making normal hardware scanout read the full VRAM.
    if (report_summary && hardware_renderer_ && !hardware_vram_shadow_valid_) {
        hardware_renderer_->sync_to_cpu(vram_);
        hardware_vram_shadow_valid_ = true;
    }
    auto report_gpu_summary = [&] {
        if (report_summary) {
            if (const char *dump_path = std::getenv("MAMERECOMP_ZN_DUMP_VRAM")) {
                std::ofstream dump(dump_path, std::ios::binary | std::ios::trunc);
                if (dump) {
                    constexpr unsigned dump_width = 1024, dump_height = 512;
                    dump << "P6\n" << dump_width << ' ' << dump_height << "\n255\n";
                    for (unsigned y = 0; y < dump_height; ++y) {
                        for (unsigned x = 0; x < dump_width; ++x) {
                            const uint16_t pixel = get_pixel(int(x), int(y));
                            const char rgb[3]{
                                char(((pixel & 31) << 3) | ((pixel & 31) >> 2)),
                                char(((((pixel >> 5) & 31) << 3) | (((pixel >> 5) & 31) >> 2))),
                                char(((((pixel >> 10) & 31) << 3) | (((pixel >> 10) & 31) >> 2)))};
                            dump.write(rgb, sizeof(rgb));
                        }
                    }
                }
            }
            uint64_t commands = 0;
            uint64_t nonzero_vram_words = 0;
            unsigned min_x = 1024, min_y = 512, max_x = 0, max_y = 0;
            for (uint64_t count : gp0_command_counts_) commands += count;
            for (size_t i = 0; i + 1 < vram_.size(); i += 2) {
                if (vram_[i] || vram_[i + 1]) {
                    ++nonzero_vram_words;
                    const unsigned x = unsigned((i / 2) % 1024);
                    const unsigned y = unsigned((i / 2) / 1024);
                    min_x = std::min(min_x, x); min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x); max_y = std::max(max_y, y);
                }
            }
            std::cerr << "ZN GPU summary status=0x" << std::hex << status_
                      << " display_start=(" << display_start_x_ << ',' << display_start_y_ << ')'
                      << " display_window=(" << horizontal_start_ << ',' << horizontal_end_
                      << ',' << vertical_start_ << ',' << vertical_end_ << ')'
                      << " vram_bbox=(" << std::dec << min_x << ',' << min_y << ','
                      << max_x << ',' << max_y << ')'
                      << " display_mode=" << ((status_ >> 17) & 7)
                      << std::dec << " GP0_commands=" << commands
                      << " nonzero_vram_words=" << nonzero_vram_words << '\n';
            for (unsigned op = 0; op < gp0_command_counts_.size(); ++op)
                if (gp0_command_counts_[op])
                    std::cerr << "  GP0[0x" << std::hex << op << "]=0x"
                              << gp0_command_counts_[op] << std::dec << '\n';
        }
    };
    frame.rgb.resize(size_t(frame.width) * frame.height * 3, 0);
    if (!frame.display_enabled || vram_.empty()) { report_gpu_summary(); return frame; }

    constexpr size_t vram_width = 1024;
    const size_t vram_height = std::max<size_t>(1, vram_.size() / (vram_width * 2));
    const bool rgb24 = (status_ & (1u << 21)) != 0;
    // Match MAME's PSX GPU scanout window. VRAM is larger than the active
    // display area; GP1(06)/(07) select the horizontal/vertical beam ranges.
    // Reading a full-width/full-height rectangle directly from VRAM makes
    // titles with non-default timings appear stretched or mostly black.
    const int overscan_left = (status_ & (1u << 20)) ? 0x27e : 0x260;
    const int overscan_top = (status_ & (1u << 20)) ? 0x23 : 0x10;
    int src_y = 0;
    int dst_y = int(vertical_start_) - overscan_top;
    int lines = int(vertical_end_) - int(vertical_start_);
    if (dst_y < 0) {
        src_y = -dst_y;
        lines += dst_y;
        dst_y = 0;
    }
    if (status_ & (1u << 22)) lines *= 2;
    lines = std::clamp(lines, 0, std::max(0, int(frame.height) - dst_y));

    int src_x = 0;
    int dst_x = (int(horizontal_start_) - overscan_left) * int(frame.width) / 2560;
    int columns = (int(horizontal_end_) - int(horizontal_start_)) * int(frame.width) / 2560;
    if (dst_x < 0) {
        src_x = -dst_x;
        columns += dst_x;
        dst_x = 0;
    }
    columns = std::clamp(columns, 0, std::max(0, int(frame.width) - dst_x));
    if (hardware_renderer_) {
        frame.rgb = hardware_renderer_->scanout_rgb(
            frame.width, frame.height, frame.display_enabled, rgb24,
            display_start_x_, display_start_y_, static_cast<unsigned>(vram_height),
            src_x, src_y, dst_x, dst_y, columns, lines, hardware_scanout_async_);
        report_gpu_summary();
        return frame;
    }
    auto expand5 = [](unsigned value) -> uint8_t {
        return static_cast<uint8_t>((value << 3) | (value >> 2));
    };
    if (!rgb24) {
        for (int y = 0; y < lines; ++y) {
            const size_t row = ((display_start_y_ + unsigned(src_y + y)) % vram_height) * vram_width;
            for (int x = 0; x < columns; ++x) {
                const size_t pixel = (row + ((display_start_x_ + unsigned(src_x + x)) & 0x3ff)) * 2;
                const uint16_t color = uint16_t(vram_[pixel] | (uint16_t(vram_[pixel + 1]) << 8));
                const size_t out = (size_t(dst_y + y) * frame.width + dst_x + x) * 3;
                frame.rgb[out] = expand5(color & 31);
                frame.rgb[out + 1] = expand5((color >> 5) & 31);
                frame.rgb[out + 2] = expand5((color >> 10) & 31);
            }
        }
    } else {
        const size_t vram_bytes = vram_.size();
        for (int y = 0; y < lines; ++y) {
            const size_t row = ((display_start_y_ + unsigned(src_y + y)) % vram_height) * vram_width * 2;
            const size_t start = row + size_t(display_start_x_) * 2;
            for (int x = 0; x < columns; ++x) {
                const size_t source = (start + size_t(src_x + x) * 3) % vram_bytes;
                const size_t out = (size_t(dst_y + y) * frame.width + dst_x + x) * 3;
                frame.rgb[out] = vram_[source];
                frame.rgb[out + 1] = vram_[(source + 1) % vram_bytes];
                frame.rgb[out + 2] = vram_[(source + 2) % vram_bytes];
            }
        }
    }
    report_gpu_summary();
    (void)horizontal_start_; (void)horizontal_end_;
    (void)vertical_start_; (void)vertical_end_;
    return frame;
}
}
