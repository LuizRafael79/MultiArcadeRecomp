#include "chips/sega_315_5011/sega_315_5011.hpp"

namespace mamerecomp::chips::sega3155011 {

SpriteWindow Comparator::compare(unsigned top, unsigned bottom, bool flip) const {
    if (top >= bottom) return {};
    if (flip) return {224 - static_cast<int>(bottom), 224 - static_cast<int>(top), true};
    return {static_cast<int>(top), static_cast<int>(bottom), true};
}

bool Comparator::active(const SpriteWindow& window, int line) const {
    return window.valid && line >= window.first_line && line < window.last_line;
}

} // namespace mamerecomp::chips::sega3155011
