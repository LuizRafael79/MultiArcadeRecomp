#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mamerecomp::psx {

struct GpuFrame {
    unsigned width = 0;
    unsigned height = 0;
    bool display_enabled = false;
    std::vector<uint8_t> rgb;
};

// Early PSX GP0/GP1 device. VRAM is the same byte array exposed to board
// presenters. Rasterization and display timing are still approximations.
class Gpu {
public:
    explicit Gpu(std::vector<uint8_t> &vram, unsigned gpu_type = 2);
    bool advance_screen_timing(uint64_t cpu_cycles, uint64_t cpu_cycle_hz);
    uint64_t cycles_until_screen_event(uint64_t cpu_cycle_hz) const;
    const std::optional<GpuFrame>& last_vblank_frame() const { return last_vblank_frame_; }
    uint32_t read_data();
    uint32_t read_status() const;
    void write_data(uint32_t value);
    void write_status(uint32_t value);
    void dma_write(std::span<const uint32_t> words);
    std::vector<uint32_t> dma_read(size_t words);
    GpuFrame framebuffer_rgb() const;
    uint64_t profile_execute_packet_ns() const { return profile_execute_packet_ns_; }
    uint64_t profile_execute_packet_calls() const { return profile_execute_packet_calls_; }
    void set_frame_capture_enabled(bool enabled) {
        frame_capture_enabled_ = enabled;
        if (!enabled) last_vblank_frame_.reset();
    }

private:
    void gp0(uint32_t value);
    void execute_packet();
    void decode_tpage(uint32_t tpage);
    void put_pixel(int x, int y, uint16_t color);
    void write_pixel(int x, int y, uint16_t color);
    uint16_t get_pixel(int x, int y) const;
    uint16_t get_pixel_linear(int x, int y) const;
    void fill_rect(int x, int y, int w, int h, uint16_t color, bool honor_mask = false);
    void draw_solid_rect(int x, int y, int w, int h, uint16_t color,
                         bool semi_transparent);
    void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                       uint16_t color, bool semi_transparent);
    void draw_line(int x0, int y0, int x1, int y1, uint32_t color0,
                   uint32_t color1, bool gouraud, bool semi_transparent);
    void draw_flat_polygon(const int *x, const int *y, unsigned count,
                           uint16_t color, bool semi_transparent);
    void draw_gouraud_polygon(const int *x, const int *y,
                              const uint32_t *color, unsigned count,
                              bool semi_transparent);
    void draw_textured_polygon(const int *x, const int *y, const int *u, const int *v,
                               const uint32_t *color, unsigned count,
                               uint32_t clut, uint32_t tpage, bool raw_texture,
                               bool gouraud, bool semi_transparent);
    uint16_t sample_texture(int u, int v, uint32_t clut, uint32_t tpage) const;
    static int16_t coord_x(uint32_t v);
    static int16_t coord_y(uint32_t v);
    size_t wrap_vram_y(size_t y) const;
    size_t wrap_vram_pixel(size_t pixel) const;

    std::vector<uint8_t> &vram_;
    size_t vram_height_ = 1;
    size_t vram_height_mask_ = 0;
    size_t vram_pixel_count_ = 1;
    size_t vram_pixel_mask_ = 0;
    bool vram_height_power_of_two_ = true;
    bool vram_pixel_count_power_of_two_ = true;
    unsigned gpu_type_ = 2;
    std::vector<uint32_t> packet_;
    mutable std::array<uint64_t, 256> gp0_command_counts_{};
    uint32_t status_ = 0x14802000;
    uint32_t gpu_info_ = 0;
    bool screen_vpos_odd_ = false;
    uint64_t screen_elapsed_cycles_ = 0;
    uint64_t screen_frame_phase_ = 0;
    uint64_t screen_line_phase_ = 0;
    uint64_t screen_cycle_hz_ = 0;
    std::optional<GpuFrame> last_vblank_frame_;
    bool frame_capture_enabled_ = true;
    bool profile_timing_ = false;
    uint64_t profile_execute_packet_ns_ = 0;
    uint64_t profile_execute_packet_calls_ = 0;
    uint32_t texture_page_ = 0;
    unsigned texture_window_x_ = 0, texture_window_y_ = 0;
    unsigned texture_window_mask_x_ = 255, texture_window_mask_y_ = 255;
    int draw_area_x1_ = 0, draw_area_y1_ = 0;
    int draw_area_x2_ = 1023, draw_area_y2_ = 1023;
    int draw_offset_x_ = 0, draw_offset_y_ = 0;
    int texture_flip_x_ = 0, texture_flip_y_ = 0;
    uint32_t display_start_x_ = 0, display_start_y_ = 0;
    uint32_t horizontal_start_ = 0x260, horizontal_end_ = 0xc60;
    uint32_t vertical_start_ = 0x10, vertical_end_ = 0x100;
    uint32_t transfer_x_ = 0, transfer_y_ = 0, transfer_w_ = 0, transfer_h_ = 0;
    uint32_t transfer_pos_ = 0;
    bool transfer_to_vram_ = false;
    bool transfer_from_vram_ = false;
    bool draw_mask_ = false;
    bool check_mask_ = false;
};

} // namespace mamerecomp::psx
