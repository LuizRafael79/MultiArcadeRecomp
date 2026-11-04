#pragma once

#include "midway/vunit/core/vunit_video.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace mamerecomp::vunit {

class VUnitGpuRenderer final {
public:
    VUnitGpuRenderer();
    ~VUnitGpuRenderer();
    VUnitGpuRenderer(const VUnitGpuRenderer&) = delete;
    VUnitGpuRenderer& operator=(const VUnitGpuRenderer&) = delete;

    bool render(std::span<const Video::GpuCommand> commands,
                std::span<const std::uint8_t> texture,
                std::span<std::uint16_t> vram,
                std::uint64_t texture_generation,
                std::array<std::uint64_t, 2> vram_generation,
                std::uint8_t readback_pages,
                std::string& error) noexcept;
    bool convert_visible_rgb(unsigned page,
                std::span<const std::uint32_t> palette,
                std::uint64_t palette_generation,
                std::span<const std::uint16_t> vram,
                std::array<std::uint64_t, 2> vram_generation,
                float scanline_strength,
                std::vector<std::uint8_t>& rgb,
                std::string& error) noexcept;
    bool initialize(std::string& error) noexcept;
    const std::string& renderer_name() const noexcept;

private:
    struct Impl;
    Impl* m_impl{};
};

} // namespace mamerecomp::vunit
