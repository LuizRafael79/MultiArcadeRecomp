#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mamerecomp::psx {

struct GpuHwVertex {
    int x = 0;
    int y = 0;
    int u = 0;
    int v = 0;
    uint32_t color = 0;
};

struct GpuHwState {
    int draw_x1 = 0, draw_y1 = 0, draw_x2 = 1023, draw_y2 = 1023;
    int offset_x = 0, offset_y = 0;
    int window_x = 0, window_y = 0, window_mask_x = 255, window_mask_y = 255;
    int flip_x = 1, flip_y = 1;
    uint32_t texture_page = 0;
    uint32_t gpu_type = 2;
    bool set_mask = false;
    bool check_mask = false;
    bool dither_enable = false;
};

class GpuHardwareRenderer {
public:
    explicit GpuHardwareRenderer(std::span<const uint8_t> initial_vram,
                                 void* native_window = nullptr,
                                 void* native_context = nullptr);
    ~GpuHardwareRenderer();
    GpuHardwareRenderer(const GpuHardwareRenderer&) = delete;
    GpuHardwareRenderer& operator=(const GpuHardwareRenderer&) = delete;

    void draw_polygon(std::span<const GpuHwVertex> vertices, const GpuHwState& state,
                      bool textured, bool gouraud, bool raw_texture,
                      bool semi_transparent, uint32_t clut);
    void fill_rect(int x, int y, int width, int height, uint16_t color);
    void draw_rect(int x, int y, int width, int height, uint32_t color,
                   const GpuHwState& state, bool semi_transparent);
    void draw_line(const GpuHwVertex& a, const GpuHwVertex& b,
                   const GpuHwState& state, bool gouraud,
                   bool semi_transparent);
    void draw_textured_rect(int x, int y, int width, int height,
                            int u, int v, uint32_t color, uint32_t clut,
                            const GpuHwState& state, bool raw_texture,
                            bool semi_transparent);
    void sync_to_cpu(std::vector<uint8_t>& vram);
    void sync_rect_to_cpu(std::vector<uint8_t>& vram, unsigned x, unsigned y,
                          unsigned width, unsigned height);
    uint64_t vram_write_generation();
    void sync_from_cpu(std::span<const uint8_t> vram);
    void upload_rect(std::span<const uint8_t> vram, unsigned x, unsigned y,
                     unsigned width, unsigned height);
    void copy_rect(int sx, int sy, int dx, int dy, int width, int height,
                   const GpuHwState& state);
    std::vector<uint8_t> scanout_rgb(unsigned width, unsigned height,
                                     bool display_enabled, bool rgb24,
                                     unsigned display_start_x, unsigned display_start_y,
                                     unsigned vram_height, int source_x, int source_y,
                                     int destination_x, int destination_y,
                                     int copy_width, int copy_height,
                                     bool asynchronous = false,
                                     bool direct_present = false);
    bool present_scanout();
    const std::string& renderer_name() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mamerecomp::psx
