#pragma once

namespace multiarcaderecomp::chips::sega3155011 {

struct SpriteWindow {
    int first_line = 0;
    int last_line = 0;
    bool valid = false;
};

// 315-5011 sprite line comparator: select the descriptor's active vertical
// interval, applying the board's global flip signal to that interval.
class Comparator {
public:
    SpriteWindow compare(unsigned top, unsigned bottom, bool flip) const;
    bool active(const SpriteWindow& window, int line) const;
};

} // namespace multiarcaderecomp::chips::sega3155011
