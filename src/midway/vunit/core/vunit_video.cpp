#include "midway/vunit/core/vunit_video.hpp"
#ifdef MAMERECOMP_VUNIT_OPENGL
#include "midway/vunit/core/vunit_video_gpu.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace mamerecomp::vunit {
namespace {
constexpr std::size_t kPageWords = 0x40000;
// MAME exposes 0x200000 32-bit words to the board, but the renderer addresses
// the texture store as bytes. Its texture pointer plus the DMA texture base can
// therefore span 8 MiB, even though textureram_w populates the lower 4 MiB.
constexpr std::size_t kTextureBytes = 0x800000;
constexpr std::size_t kTextureWords = 0x200000;
constexpr std::size_t kPaletteWords = 0x8000;
}

Video::Video() : m_vram(0x80000), m_texture(kTextureBytes),
                 m_palette(kPaletteWords) {}

Video::~Video() = default;

void Video::enable_gpu_renderer() {
#ifdef MAMERECOMP_VUNIT_OPENGL
    if (!m_gpu_renderer) {
        auto renderer = std::make_unique<VUnitGpuRenderer>();
        std::string error;
        if (!renderer->initialize(error))
            throw std::runtime_error("V-Unit OpenGL initialization failed: " + error);
        m_gpu_renderer = std::move(renderer);
    }
#else
    throw std::runtime_error("V-Unit OpenGL raster backend was not built; configure with "
                             "-DMAMERECOMP_ENABLE_VUNIT_OPENGL=ON");
#endif
}

void Video::disable_gpu_renderer() noexcept {
#ifdef MAMERECOMP_VUNIT_OPENGL
    flush_gpu_commands(0x3);
    m_gpu_renderer.reset();
    m_gpu_commands.clear();
    m_gpu_dirty_pages = 0;
#endif
}

bool Video::gpu_renderer_enabled() const noexcept {
#ifdef MAMERECOMP_VUNIT_OPENGL
    return m_gpu_renderer != nullptr;
#else
    return false;
#endif
}

const std::string& Video::gpu_renderer_name() const noexcept {
#ifdef MAMERECOMP_VUNIT_OPENGL
    static const std::string empty;
    return m_gpu_renderer ? m_gpu_renderer->renderer_name() : empty;
#else
    static const std::string empty;
    return empty;
#endif
}

void Video::reset() noexcept {
    std::fill(m_vram.begin(), m_vram.end(), 0);
    std::fill(m_texture.begin(), m_texture.end(), 0);
    std::fill(m_palette.begin(), m_palette.end(), 0);
    m_dma_data.fill(0);
    m_video_regs.fill(0);
    m_dma_index = m_page_control = m_scanline = 0;
    m_dma_commands = 0;
    m_display_page_flips = 0;
    m_gpu_commands.clear();
    ++m_vram_generation[0];
    ++m_vram_generation[1];
    ++m_texture_generation;
    m_video_changed = true;
    m_scanline_irq_enabled = false;
}

void Video::queue_word(std::uint32_t value) noexcept {
    if (m_dma_index < m_dma_data.size())
        m_dma_data[m_dma_index++] = static_cast<std::uint16_t>(value);
}

void Video::trigger(std::uint32_t offset) noexcept {
    if (offset != 0) process_command();
    m_dma_index = 0;
}

void Video::set_page_control(std::uint32_t value) noexcept {
    if ((m_page_control ^ value) & 1) {
        m_video_changed = true;
        ++m_display_page_flips;
    }
    m_page_control = static_cast<std::uint16_t>(value);
}

std::uint32_t Video::video_register(unsigned index) const noexcept {
    return index < m_video_regs.size() ? m_video_regs[index] : 0xffffffffu;
}

void Video::write_video_register(unsigned index, std::uint32_t value,
                                 std::uint32_t mem_mask) noexcept {
    if (index >= m_video_regs.size()) return;
    const auto old = m_video_regs[index];
    const auto mask = static_cast<std::uint16_t>(mem_mask);
    m_video_regs[index] = (old & ~mask) | (static_cast<std::uint16_t>(value) & mask);
    if (index == 0) m_scanline_irq_enabled = true;
}

std::uint32_t Video::read_vram(std::uint32_t offset) const noexcept {
    const_cast<Video*>(this)->flush_gpu_commands(
        offset < kPageWords ? 0x1 : offset < m_vram.size() ? 0x2 : 0x0);
    return offset < m_vram.size() ? m_vram[offset] : 0xffffffffu;
}

void Video::write_vram(std::uint32_t offset, std::uint32_t value,
                       std::uint32_t mem_mask) noexcept {
    if (offset >= m_vram.size()) return;
    const auto page = (offset >= kPageWords) ? 1u : 0u;
    flush_gpu_commands(static_cast<std::uint8_t>(1u << page));
    const auto mask = static_cast<std::uint16_t>(mem_mask);
    auto& dst = m_vram[offset];
    const auto next = static_cast<std::uint16_t>((dst & ~mask) | (value & mask));
    if (dst != next) { dst = next; ++m_vram_generation[page]; }
    if ((offset & 0x40000u) == ((m_page_control & 1) ? 0x40000u : 0u))
        m_video_changed = true;
}

std::uint32_t Video::read_texture(std::uint32_t offset) const noexcept {
    if (offset >= kTextureWords) return 0xffffffffu;
    const auto i = static_cast<std::size_t>(offset) * 2;
    return static_cast<std::uint32_t>(m_texture[i]) |
           (static_cast<std::uint32_t>(m_texture[i + 1]) << 8);
}

void Video::write_texture(std::uint32_t offset, std::uint32_t value) noexcept {
    if (offset >= kTextureWords) return;
    flush_gpu_commands();
    const auto i = static_cast<std::size_t>(offset) * 2;
    const auto low = static_cast<std::uint8_t>(value);
    const auto high = static_cast<std::uint8_t>(value >> 8);
    if (m_texture[i] != low || m_texture[i + 1] != high) {
        m_texture[i] = low;
        m_texture[i + 1] = high;
        ++m_texture_generation;
    }
}

std::uint32_t Video::read_palette(std::uint32_t offset) const noexcept {
    return offset < m_palette.size() ? m_palette[offset] : 0xffffffffu;
}

void Video::write_palette(std::uint32_t offset, std::uint32_t value,
                          std::uint32_t mem_mask) noexcept {
    if (offset >= m_palette.size()) return;
    auto& dst = m_palette[offset];
    const auto next = (dst & ~mem_mask) | (value & mem_mask);
    if (dst != next) {
        dst = next;
        ++m_palette_generation;
    }
}

const std::vector<std::uint16_t>& Video::visible_frame() const noexcept {
    // MAME selects the display page with bit 0 (bit 2 selects the render page).
    const_cast<Video*>(this)->flush_gpu_commands(
        static_cast<std::uint8_t>(1u << (m_page_control & 1u)));
    const std::size_t offset = (m_page_control & 1) ? kPageWords : 0;
    // Keep a cached contiguous page to provide a stable framebuffer reference.
    static thread_local std::vector<std::uint16_t> frame(width * height);
    for (unsigned y = 0; y < height; ++y)
        std::copy_n(m_vram.begin() + offset + y * width, width,
                    frame.begin() + y * width);
    return frame;
}

std::vector<std::uint8_t> Video::visible_rgb() const {
#ifdef MAMERECOMP_VUNIT_OPENGL
    auto* self = const_cast<Video*>(this);
    if (self->m_gpu_renderer) {
        self->flush_gpu_commands();
        std::vector<std::uint8_t> rgb;
        std::string error;
        if (self->m_gpu_renderer->convert_visible_rgb(
                m_page_control & 1u, m_palette, m_palette_generation, m_vram,
                m_vram_generation, m_scanline_strength, rgb, error))
            return rgb;
        std::fprintf(stderr, "[V-Unit GPU] palette conversion failed; returning to CPU: %s\n",
                     error.c_str());
        self->disable_gpu_renderer();
    }
#endif
    const auto& frame = visible_frame();
    std::vector<std::uint8_t> rgb(frame.size() * 3);
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const auto color = m_palette[frame[i] & 0x7fff];
        const auto expand = [](std::uint32_t v) -> std::uint8_t {
            v &= 0x1f;
            return static_cast<std::uint8_t>((v << 3) | (v >> 2));
        };
        const auto y = i / width;
        const float gain = (y & 1u) ? 1.0f : (1.0f - 0.28f * m_scanline_strength);
        rgb[i * 3 + 0] = static_cast<std::uint8_t>(std::lround(expand(color >> 10) * gain));
        rgb[i * 3 + 1] = static_cast<std::uint8_t>(std::lround(expand(color >> 5) * gain));
        rgb[i * 3 + 2] = static_cast<std::uint8_t>(std::lround(expand(color) * gain));
    }
    return rgb;
}

void Video::process_command() noexcept {
    if (m_dma_index < 15) return;
    const bool textured = (m_dma_data[0] & 0x300u) == 0x100u;
    const unsigned flags = m_dma_data[0] & 0x0c00u;
    unsigned mode = 0;
    std::uint16_t color_base = static_cast<std::uint16_t>(m_dma_data[1]);
    const auto command = static_cast<std::uint16_t>(m_dma_data[0]);
    if (!textured) {
        mode = 0; // flat shaded
        color_base = static_cast<std::uint16_t>(color_base + (command & 0xff));
    } else if (flags == 0x0000u) {
        mode = 3; // opaque texture
    } else if (flags == 0x0800u) {
        mode = 1; // transparent texture
    } else if (flags == 0x0c00u) {
        mode = 2; // transparent texture mask
    } else {
        mode = 0; // MAME falls back to a flat quad for masked opaque commands
        color_base = static_cast<std::uint16_t>(color_base + (command & 0xff));
    }
    const auto vertex = [this](unsigned n) {
        Vertex v;
        v.x = static_cast<std::int16_t>(m_dma_data[2 + n * 2]) + 0.5f;
        v.y = static_cast<std::int16_t>(m_dma_data[3 + n * 2]) + 0.5f;
        if (n < 4) {
            const auto uv = m_dma_data[10 + n];
            v.u = static_cast<float>(uv & 0xff) * 65536.0f + 32768.0f;
            v.v = static_cast<float>(uv >> 8) * 65536.0f + 32768.0f;
        }
        return v;
    };
    std::array<Vertex, 4> verts{vertex(0), vertex(1), vertex(2), vertex(3)};
    // Match MAME's right/bottom inclusive adjustment on the quad edges.
    std::uint8_t right = 0, bottom = 0, equal = 0;
    for (unsigned n = 0; n < 4; ++n) {
        const auto& a = verts[n];
        const auto& b = verts[(n + 1) & 3];
        if (a.x == b.x && a.y == b.y) equal |= 1u << n;
        if (b.y > a.y || (b.y == a.y && b.x < a.x)) right |= 1u << n;
        if (b.x < a.x || (b.x == a.x && b.y < a.y)) bottom |= 1u << n;
    }
    if (equal != 0x0f) {
        for (unsigned n = 0; n < 4; ++n) {
            unsigned effective = n;
            while (equal & (1u << effective)) effective = (effective + 1) & 3;
            if (right & (1u << effective)) verts[n].x += 0.001f;
            if (bottom & (1u << effective)) verts[n].y += 0.001f;
        }
    }
    const bool dither = (command & 0x2000u) != 0;
    GpuCommand render_command;
    render_command.vertices = verts;
    render_command.color_base = color_base;
    render_command.render_page = static_cast<std::uint16_t>((m_page_control >> 2) & 1);
    render_command.texture_base = m_dma_data[14] * 256;
    render_command.mode = static_cast<std::uint8_t>(mode);
    render_command.dither = dither;
#ifdef MAMERECOMP_VUNIT_OPENGL
    if (m_gpu_renderer) {
        try {
            m_gpu_commands.push_back(render_command);
            m_gpu_dirty_pages |= static_cast<std::uint8_t>(1u << render_command.render_page);
        } catch (...) {
            // Keep the CPU backend usable even if a transient allocation fails.
            flush_gpu_commands(static_cast<std::uint8_t>(1u << render_command.render_page));
            raster_command(render_command);
            ++m_vram_generation[render_command.render_page];
        }
    } else {
        raster_command(render_command);
    }
#else
    raster_command(render_command);
#endif
    ++m_dma_commands;
    m_video_changed = true;
}

void Video::raster_command(const GpuCommand& command) noexcept {
    const auto old_page_control = m_page_control;
    const auto old_texture_base = m_dma_data[14];
    m_page_control = static_cast<std::uint16_t>(
        (m_page_control & ~std::uint32_t{4}) | (command.render_page ? 4u : 0u));
    m_dma_data[14] = command.texture_base / 256;
    const auto flat = static_cast<std::uint8_t>(m_dma_data[0]);
    raster_triangle(command.vertices[0], command.vertices[1], command.vertices[2],
                    command.color_base, command.mode, command.dither, flat);
    raster_triangle(command.vertices[0], command.vertices[2], command.vertices[3],
                    command.color_base, command.mode, command.dither, flat);
    m_page_control = old_page_control;
    m_dma_data[14] = old_texture_base;
}

void Video::flush_gpu_commands(std::uint8_t readback_pages) noexcept {
#ifdef MAMERECOMP_VUNIT_OPENGL
    if (!m_gpu_renderer) return;
    readback_pages &= m_gpu_dirty_pages;
    if (m_gpu_commands.empty() && readback_pages == 0) return;
    std::string error;
    if (m_gpu_renderer->render(m_gpu_commands, m_texture, m_vram,
                               m_texture_generation, m_vram_generation,
                               readback_pages, error)) {
        m_gpu_commands.clear();
        m_gpu_dirty_pages &= static_cast<std::uint8_t>(~readback_pages);
        return;
    }
    std::fprintf(stderr, "[V-Unit GPU] backend failed; replaying %zu DMA commands on CPU: %s\n",
                 m_gpu_commands.size(), error.c_str());
    for (const auto& command : m_gpu_commands) raster_command(command);
    m_gpu_commands.clear();
    m_gpu_dirty_pages = 0;
    m_gpu_renderer.reset();
#else
    (void)readback_pages;
#endif
}

namespace {
float edge_value(const Video::Vertex& a, const Video::Vertex& b, float x, float y) {
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}
}

void Video::raster_triangle(const Vertex& a, const Vertex& b, const Vertex& c,
                            std::uint16_t base, unsigned mode,
                            bool dither, std::uint8_t flat) noexcept {
    const float area = edge_value(a, b, c.x, c.y);
    if (std::abs(area) < 1e-6f) return;
    const int min_x = std::clamp(static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))), 0, 511);
    const int max_x = std::clamp(static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))), 0, 511);
    const int min_y = std::clamp(static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))), 0, 399);
    const int max_y = std::clamp(static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))), 0, 399);
    const float inv_area = 1.0f / area;
    const float dwa_dx = (c.y - b.y) * inv_area;
    const float dwb_dx = (a.y - c.y) * inv_area;
    const float dwc_dx = (b.y - a.y) * inv_area;
    const float du_dx = dwa_dx * a.u + dwb_dx * b.u + dwc_dx * c.u;
    const float dv_dx = dwa_dx * a.v + dwb_dx * b.v + dwc_dx * c.v;
    for (int y = min_y; y <= max_y; ++y) {
        const float py = y + 0.5f;
        float wa = edge_value(b, c, min_x + 0.5f, py) * inv_area;
        float wb = edge_value(c, a, min_x + 0.5f, py) * inv_area;
        float wc = 1.0f - wa - wb;
        float u = wa * a.u + wb * b.u + wc * c.u;
        float v = wa * a.v + wb * b.v + wc * c.v;
        for (int x = min_x; x <= max_x; ++x) {
            if (wa >= -1e-6f && wb >= -1e-6f && wc >= -1e-6f)
                draw_pixel(x, y, u, v, base, mode, dither, flat);
            wa += dwa_dx;
            wb += dwb_dx;
            wc += dwc_dx;
            u += du_dx;
            v += dv_dx;
        }
    }
}

void Video::draw_pixel(int x, int y, float u, float v, std::uint16_t base,
                       unsigned mode, bool dither, std::uint8_t flat) noexcept {
    if (dither && ((x ^ y) & 1)) return;
    const std::size_t page = (m_page_control & 4) ? kPageWords : 0;
    auto& dst = m_vram[page + static_cast<std::size_t>(y) * width + x];
    if (mode == 0) {
        dst = static_cast<std::uint16_t>(base);
        return;
    }
    const auto tu = static_cast<int>(u) >> 16;
    const auto tv = (static_cast<int>(v) >> 8) & 0xff00;
    const std::size_t texture_index = static_cast<std::size_t>(
        (tv + tu) & 0x3fffff) +
        static_cast<std::size_t>(m_dma_data[14]) * 256;
    const auto texel = texture_index < m_texture.size()
        ? m_texture[texture_index] : std::uint8_t{0};
    if (mode == 1) {
        if (texel) dst = static_cast<std::uint16_t>(base + texel);
    } else if (mode == 2) {
        if (texel) dst = static_cast<std::uint16_t>(base);
    } else {
        dst = static_cast<std::uint16_t>(base + texel);
    }
    (void)flat;
}

} // namespace mamerecomp::vunit
