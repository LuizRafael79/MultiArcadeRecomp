#include "rf5c68.hpp"

#include <cassert>

int main() {
    mamerecomp::chips::Rf5c68 chip;
    chip.write_sample_ram(0, 0x81); // positive magnitude 1
    chip.write_sample_ram(1, 0x01); // negative magnitude 1
    chip.write_register(0x07, 0xc0); // enable chip, select channel 0
    chip.write_register(0x00, 0xff); // envelope
    chip.write_register(0x01, 0xff); // equal left/right pan
    chip.write_register(0x02, 0x00);
    chip.write_register(0x03, 0x08); // advance one sample byte per output
    chip.write_register(0x06, 0x00);
    chip.write_register(0x08, 0xfe); // enable channel 0

    chip.advance_clocks(384);
    const auto positive = chip.output();
    assert(positive[0] == 64 && positive[1] == 64);
    chip.advance_clocks(384);
    const auto negative = chip.output();
    assert(negative[0] == -128 && negative[1] == -128);
}
