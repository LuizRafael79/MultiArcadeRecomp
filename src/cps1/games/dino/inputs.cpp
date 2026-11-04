#include "cps1_driver.hpp"

namespace mamerecomp::cps1 {
const Cps1InputProfile& dino_inputs() {
    static constexpr Cps1InputProfile profile{"dino", 0xffff, {0xff, 0xff, 0xff, 0xff},
        0xff, 0xff, 0xff};
    return profile;
}
}
