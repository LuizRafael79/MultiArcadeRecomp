#include "cps1_driver.hpp"

namespace mamerecomp::cps1 {
const Cps1InputProfile& sf2_inputs() {
    static constexpr Cps1InputProfile profile{"sf2", 0xffff, {0xff, 0xff, 0xfc, 0x9c}};
    return profile;
}
}
