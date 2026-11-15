#pragma once

#include "boards/sega/common/video_session.hpp"
#include "boards/sega/system16a/address_space.hpp"
#include "boards/sega/system16a/video.hpp"

#include <memory>

namespace multiarcaderecomp {

std::unique_ptr<SegaBoardVideoRenderer> make_system16a_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, System16AVideoSignals& signals, SegaVideoBackend backend);

} // namespace multiarcaderecomp
