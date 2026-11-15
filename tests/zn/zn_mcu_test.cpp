#include "zn_mcu.hpp"

#include <array>
#include <cassert>

int main()
{
    multiarcaderecomp::zn::ZnMcu mcu(100'000'000);
    mcu.select(false);
    mcu.advance(4'999);
    assert(mcu.dsr() == 1);
    mcu.advance(5'000); // first 50 us MCU-ready pulse
    assert(mcu.dsr() == 0);
    mcu.advance(5'500); // DSR returns high after the 5 us pulse
    assert(mcu.dsr() == 1);

    constexpr std::array<int, 8> expected{1, 1, 1, 1, 1, 0, 0, 0};
    for (int bit : expected) {
        mcu.clock(0);
        assert(mcu.data_out() == bit);
        mcu.clock(1);
    }
}
