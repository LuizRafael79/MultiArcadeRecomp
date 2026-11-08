#include "gpu_hw.hpp"

#include <stdexcept>

namespace mamerecomp::psx {

struct GpuHardwareRenderer::Impl {};

GpuHardwareRenderer::GpuHardwareRenderer(std::span<const uint8_t>, void*, void*)
    : impl_(std::make_unique<Impl>()) {
    throw std::runtime_error("PSX OpenGL rasterizer was not built");
}
GpuHardwareRenderer::~GpuHardwareRenderer() = default;
void GpuHardwareRenderer::draw_polygon(std::span<const GpuHwVertex>, const GpuHwState&,
        bool, bool, bool, bool, uint32_t) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::fill_rect(int, int, int, int, uint16_t, const GpuHwState&) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::draw_rect(int, int, int, int, uint32_t, const GpuHwState&, bool) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::draw_line(const GpuHwVertex&, const GpuHwVertex&, const GpuHwState&, bool, bool) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::draw_polyline(std::span<const GpuHwVertex>, const GpuHwState&, bool, bool) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::draw_textured_rect(int, int, int, int, int, int, uint32_t, uint32_t, const GpuHwState&, bool, bool) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::sync_to_cpu(std::vector<uint8_t>&) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::sync_rect_to_cpu(std::vector<uint8_t>&, unsigned, unsigned, unsigned, unsigned) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
uint64_t GpuHardwareRenderer::vram_write_generation() { return 0; }
void GpuHardwareRenderer::sync_from_cpu(std::span<const uint8_t>) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::upload_rect(std::span<const uint8_t>, unsigned, unsigned, unsigned, unsigned) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
void GpuHardwareRenderer::copy_rect(int, int, int, int, int, int, const GpuHwState&) { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
std::vector<uint8_t> GpuHardwareRenderer::scanout_rgb(unsigned, unsigned, bool, bool,
    unsigned, unsigned, unsigned, int, int, int, int, int, int, bool, bool) {
    throw std::runtime_error("PSX OpenGL rasterizer was not built");
}
bool GpuHardwareRenderer::present_scanout() { throw std::runtime_error("PSX OpenGL rasterizer was not built"); }
const std::string& GpuHardwareRenderer::renderer_name() const noexcept { static const std::string empty; return empty; }

} // namespace mamerecomp::psx
