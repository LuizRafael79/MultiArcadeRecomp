#include "cps1_driver.hpp"

namespace mamerecomp::cps1 {
const Cps1MachineProfile& qsound_machine();
const Cps1InputProfile& dino_inputs();
const Cps1RomSet& dino_romset();

const Cps1GameDriver& dino_driver() {
    static constexpr Cps1CpsBProfile cps_b{
        "dino", "CPS_B_21_QS2", "mapper_CD63B",
        0xff, 0xffff, 0xff, 0xff, 0xff, 0xff, 0x05,
        {0x06, 0x07, 0x00, 0x01}, 0x02, {0x16, 0x16, 0x16, 0x00, 0x00},
        0x17, 0x10,
    };
    static const Cps1GameDriver driver{
        "dino", "Cadillacs and Dinosaurs (World 930201)", "1993", {},
        "qsound", "dino", "init_dino", &qsound_machine(), &dino_inputs(),
        &cps_b, &dino_romset(), false,
    };
    return driver;
}
}
