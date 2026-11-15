#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace multiarcaderecomp::vunit {

class VUnitGpuRenderer;

class Video final {
public:
    static constexpr unsigned width = 512;
    static constexpr unsigned height = 400;
    struct Vertex { float x{}, y{}, u{}, v{}; };
    struct GpuCommand {
        std::array<Vertex, 4> vertices{};
        std::uint16_t color_base{};
        std::uint16_t render_page{};
        std::uint32_t texture_base{};
        std::uint8_t mode{};
        bool dither{};
    };

    Video();
    ~Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;
    void enable_gpu_renderer();
    void disable_gpu_renderer() noexcept;
    bool gpu_renderer_enabled() const noexcept;
    const std::string& gpu_renderer_name() const noexcept;
    void reset() noexcept;
    void queue_word(std::uint32_t value) noexcept;
    void trigger(std::uint32_t offset) noexcept;
    std::uint32_t dma_queue_entries() const noexcept { return 0; }
    std::uint32_t page_control() const noexcept { return m_page_control; }
    void set_page_control(std::uint32_t value) noexcept;
    std::uint32_t video_register(unsigned index) const noexcept;
    void write_video_register(unsigned index, std::uint32_t value,
                              std::uint32_t mem_mask = 0xffffffffu) noexcept;
    std::uint32_t scanline() const noexcept { return m_scanline; }
    void set_scanline(std::uint32_t value) noexcept { m_scanline = value; }
    bool scanline_irq_enabled() const noexcept { return m_scanline_irq_enabled; }
    std::uint32_t read_vram(std::uint32_t offset) const noexcept;
    void write_vram(std::uint32_t offset, std::uint32_t value,
                    std::uint32_t mem_mask = 0xffffffffu) noexcept;
    std::uint32_t read_texture(std::uint32_t offset) const noexcept;
    void write_texture(std::uint32_t offset, std::uint32_t value) noexcept;
    std::uint32_t read_palette(std::uint32_t offset) const noexcept;
    void write_palette(std::uint32_t offset, std::uint32_t value,
                       std::uint32_t mem_mask = 0xffffffffu) noexcept;
    const std::vector<std::uint16_t>& visible_frame() const noexcept;
    std::vector<std::uint8_t> visible_rgb() const;
    void set_scanline_strength(float strength) noexcept {
        m_scanline_strength = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    }
    std::uint64_t dma_commands() const noexcept { return m_dma_commands; }
    std::uint64_t display_page_flips() const noexcept { return m_display_page_flips; }

private:
    void process_command() noexcept;
    void raster_command(const GpuCommand& command) noexcept;
    void flush_gpu_commands(std::uint8_t readback_pages = 0) noexcept;
    void raster_triangle(const Vertex& a, const Vertex& b, const Vertex& c,
                         std::uint16_t base, unsigned mode,
                         bool dither, std::uint8_t flat) noexcept;
    void draw_pixel(int x, int y, float u, float v, std::uint16_t base,
                    unsigned mode, bool dither, std::uint8_t flat) noexcept;

    std::vector<std::uint16_t> m_vram;
    std::vector<std::uint8_t> m_texture;
    std::vector<std::uint32_t> m_palette;
    std::array<std::uint32_t, 16> m_dma_data{};
    std::array<std::uint32_t, 16> m_video_regs{};
    std::uint32_t m_dma_index{};
    std::uint32_t m_page_control{};
    std::uint32_t m_scanline{};
    std::uint64_t m_dma_commands{};
    std::uint64_t m_display_page_flips{};
    std::uint64_t m_texture_generation{1};
    std::uint64_t m_palette_generation{1};
    std::array<std::uint64_t, 2> m_vram_generation{1, 1};
    std::vector<GpuCommand> m_gpu_commands;
    std::uint8_t m_gpu_dirty_pages{};
    float m_scanline_strength{};
#ifdef MULTIARCADERECOMP_VUNIT_OPENGL
    std::unique_ptr<VUnitGpuRenderer> m_gpu_renderer;
#endif
    bool m_video_changed{true};
    bool m_scanline_irq_enabled{};
};

} // namespace multiarcaderecomp::vunit
