#pragma once

#include "boards/sega/common/video_session.hpp"
#include "boards/sega/common/sega_address_space.hpp"

#include <memory>

namespace mamerecomp {

std::unique_ptr<SegaBoardVideoRenderer> make_system18_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, SegaVideoSignals& signals);

} // namespace mamerecomp
