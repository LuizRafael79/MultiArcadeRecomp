#pragma once

#include "boards/sega/common/board_family.hpp"

#include <cstdint>
#include <string_view>

namespace mamerecomp::sega {

struct BoardRegistration {
    std::string_view id;
    SegaBoardFamily family;
    std::string_view module_id;
    std::uint32_t module_revision;
    std::uint32_t maincpu_clock_hz;
    std::uint32_t refresh_rate_num;
    std::uint32_t refresh_rate_den;
    bool supports_opengl_video;
};

const BoardRegistration* find_board(std::string_view id);
const BoardRegistration* find_board(SegaBoardFamily family);

} // namespace mamerecomp::sega
