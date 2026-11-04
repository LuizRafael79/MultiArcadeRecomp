#include "cps1_driver.hpp"

namespace mamerecomp::cps1 {
const Cps1MachineProfile& cps1_10mhz_machine() {
    static constexpr Cps1MachineProfile profile{
        "cps1_10MHz", 10'000'000, 3'579'545, 8'000'000, 512, 262, 240,
        Cps1AddressMap::Main, Cps1SoundBoard::Ym2151Oki,
    };
    return profile;
}
}
