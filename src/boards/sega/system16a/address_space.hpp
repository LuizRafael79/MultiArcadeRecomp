#pragma once

#include "boards/sega/common/sega_address_space.hpp"
#include <span>
#include <cstdint>

namespace mamerecomp {

class System16ASound;

struct System16AVideoSignals {
    explicit System16AVideoSignals(SegaVideoSignals& common_signals) : common(common_signals) {}
    SegaVideoSignals& common;
    bool row_scroll_enabled = false;
    bool column_scroll_enabled = false;
};

SegaAddressSpace make_system16a_address_space(std::span<const std::uint8_t> maincpu_rom,
    System16AVideoSignals* video_signals = nullptr, SegaInputState* input_state = nullptr,
    System16ASound* sound = nullptr);

} // namespace mamerecomp
