#pragma once

#include "boards/sega/common/video_session.hpp"
#include "boards/sega/common/sega_address_space.hpp"
#include "boards/sega/system16b/video.hpp"

#include <memory>

namespace multiarcaderecomp {

std::unique_ptr<SegaBoardVideoRenderer> make_system16b_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, SegaVideoSignals& signals, SegaVideoBackend backend,
    bool capture_reference);

} // namespace multiarcaderecomp
