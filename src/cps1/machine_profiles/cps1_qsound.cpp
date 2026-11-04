#include "cps1_driver.hpp"

namespace mamerecomp::cps1 {
const Cps1MachineProfile& qsound_machine() {
    static constexpr Cps1MachineProfile profile{
        "qsound", 12'000'000, 8'000'000, 8'000'000, 512, 262, 240,
        Cps1AddressMap::QSound, Cps1SoundBoard::QSound,
    };
    return profile;
}
}
