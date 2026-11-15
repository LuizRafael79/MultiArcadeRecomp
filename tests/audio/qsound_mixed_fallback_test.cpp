#include "chips/qsound.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

int main() {
    std::array<std::uint16_t, 0x1000> firmware{};

    multiarcaderecomp::chips::QSound lle({60'000'000,
        multiarcaderecomp::chips::QSoundMode::Lle, firmware});
    assert(lle.read_status() == 0x80);
    lle.begin_command_write();
    // MAME clears DSP-ready in qsound_w before scheduler().synchronize()
    // commits the command payload.
    assert(lle.read_status() == 0x00);

    // Force an unsupported DSP16A opcode at reset PC. Mixed mode must retain
    // the synchronized HLE state and switch paths instead of aborting the run.
    firmware[0] = 0xf000;

    multiarcaderecomp::chips::QSound qsound({60'000'000,
        multiarcaderecomp::chips::QSoundMode::Mixed, firmware});
    assert(qsound.mode() == multiarcaderecomp::chips::QSoundMode::Lle);

    qsound.run_clocks(2 * 1248);
    assert(qsound.mode() == multiarcaderecomp::chips::QSoundMode::Hle);
    assert(qsound.pcm_frames_produced() == 1);

    qsound.write(0, 0x00);
    qsound.write(1, 0x00);
    qsound.write(2, 0x06);
    qsound.run_clocks(2 * 1248);
    assert(qsound.pcm_frames_produced() == 2);

    std::array<std::int16_t, 4> stereo{};
    qsound.render(stereo);
}
