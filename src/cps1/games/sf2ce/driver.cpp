#include "cps1_driver.hpp"

namespace multiarcaderecomp::cps1 {
const Cps1MachineProfile& cps1_12mhz_machine();
const Cps1InputProfile& sf2_inputs();
const Cps1RomSet& sf2ce_romset();

const Cps1GameDriver& sf2ce_driver() {
    static constexpr Cps1CpsBProfile cps_b{
        "sf2ce", "CPS_B_21_DEF", "mapper_S9263B",
        0x19, 0xffff, 0x00, 0x01, 0x02, 0x03, 0x13,
        {0x14, 0x15, 0x16, 0x17}, 0x18, {0x02, 0x04, 0x08, 0x30, 0x30},
        0x04,
    };
    static const Cps1GameDriver driver{
        "sf2ce", "Street Fighter II': Champion Edition (World 920513)", "1992", {},
        "cps1_12MHz", "sf2", "empty_init", &cps1_12mhz_machine(), &sf2_inputs(),
        &cps_b, &sf2ce_romset(), true,
    };
    return driver;
}
}
