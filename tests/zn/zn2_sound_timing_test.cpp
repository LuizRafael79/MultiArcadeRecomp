#include "zn2_sound_timing.hpp"

#include <cassert>

int main()
{
    using multiarcaderecomp::zn::qsound_target_frames;
    assert(qsound_target_frames(0) == 0);
    assert(qsound_target_frames(8'000'000) == 24'038);
    // Preserve the 60 MHz / 2496 fractional cadence: truncating the rate to
    // 24,038 Hz would produce 1,442,280 frames over this minute, 27 too few.
    assert(qsound_target_frames(480'000'000) == 1'442'307);
}
