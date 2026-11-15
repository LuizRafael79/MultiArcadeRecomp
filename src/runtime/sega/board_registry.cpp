#include "runtime/sega/board_registry.hpp"

#include <array>

namespace multiarcaderecomp::sega {
namespace {
#ifdef MULTIARCADERECOMP_SEGA_OPENGL_VIDEO_AVAILABLE
constexpr bool system16_opengl_video = true;
#else
constexpr bool system16_opengl_video = false;
#endif
constexpr std::array registrations{
    BoardRegistration{"system16a", SegaBoardFamily::System16A, "system16a", 16, 10000000, 60, 1, system16_opengl_video},
    // MAME: 25.1748 MHz / 4 = pixel clock; 400 pixels per line, 262 lines.
    BoardRegistration{"system16b", SegaBoardFamily::System16B, "system16b", 18, 10000000, 62937, 1048, system16_opengl_video},
    BoardRegistration{"system18", SegaBoardFamily::System18, "system18", 18, 10000000, 5723, 100, false},
};
}

const BoardRegistration* find_board(std::string_view id) {
    for (const auto& registration : registrations)
        if (registration.id == id) return &registration;
    return nullptr;
}

const BoardRegistration* find_board(SegaBoardFamily family) {
    for (const auto& registration : registrations)
        if (registration.family == family) return &registration;
    return nullptr;
}

} // namespace multiarcaderecomp::sega
