// license:BSD-3-Clause
// Regression vectors for DSP16A DAU behavior used by the QSound DL-1425 ROM.

#include "dsp16a.hpp"
#include "dsp16a_dau_math.hpp"

#include <array>
#include <cassert>
#include <cstdint>

using mamerecomp::dsp16::Dsp16A;
using mamerecomp::dsp16::Dsp16ABus;

int main()
{
    // JTDSP16 F2 RND rounds by adding accumulator bit 15 to the upper
    // 20-bit value. In particular, a negative exact half rounds toward zero;
    // MAME currently uses a different tie rule and documents this as unclear.
    using mamerecomp::dsp16::detail::round_accumulator_f2_11;
    assert(round_accumulator_f2_11(-0x8000) == 0);
    assert(round_accumulator_f2_11(-0x8001) == -0x10000);
    assert(round_accumulator_f2_11(-0x7fff) == 0);
    assert(round_accumulator_f2_11(0x8000) == 0x10000);
    assert(round_accumulator_f2_11(0x7fff) == 0);

    // AUC[4:5], not PSW overflow/guard bits, control low-half retention on
    // an upper-half accumulator load in the JTDSP16 DAU.
    using mamerecomp::dsp16::detail::load_accumulator_high;
    assert(load_accumulator_high(0x12345678, -2, false) == -0x20000);
    assert(load_accumulator_high(0x12345678, -2, true) == -0x1a988);
    assert(load_accumulator_high(0x12345678, 0x1234, false) == 0x12340000);
    assert(load_accumulator_high(0x12345678, 0x1234, true) == 0x12345678);

    // AUC[1:0] = 2 shifts the 32-bit P bus, then sign-extends that result.
    // A negative P whose top two bits are discarded must stay positive after
    // wraparound; widening the signed P before shifting makes it a huge
    // negative accumulator and clips QSound output samples.
    using mamerecomp::dsp16::detail::align_product_left_two;
    using mamerecomp::dsp16::detail::align_product;
    assert(align_product_left_two(static_cast<std::int32_t>(0xc0008000U)) == 0x00020000);
    assert(align_product_left_two(static_cast<std::int32_t>(0x60008000U)) == -0x7ffe0000LL);
    assert(align_product(static_cast<std::int32_t>(0xc0008000U), 0) ==
           static_cast<std::int32_t>(0xc0008000U));
    assert(align_product(static_cast<std::int32_t>(0xc0008000U), 1) ==
           static_cast<std::int32_t>(0xf0002000U));
    assert(align_product(static_cast<std::int32_t>(0xc0008000U), 2) == 0x00020000);
    assert(align_product(static_cast<std::int32_t>(0xc0008000U), 3) ==
           static_cast<std::int32_t>(0xc0008000U));

    // DL-1425 sets AUC=0x02 in its sample loop. With AUC[6]=0, a YH
    // transfer clears YL even in this alignment mode.
    Dsp16ABus bus;
    bus.data_read = [](std::uint16_t address) {
        assert(address == 0);
        return std::uint16_t(0x6b69);
    };

    Dsp16A dsp(bus);
    constexpr std::array<std::uint16_t, 4> firmware{
        0x5130, 0x0002, // auc = 0x0002 (DL-1425 sample-loop mode)
        0xb800, // load YL from [R0]
        0xb810, // load YH from [R0], must clear YL when AUC[6] == 0
    };
    dsp.set_firmware(firmware);
    dsp.run_cycles(48);

    assert(dsp.state().auc == 2);
    assert(dsp.state().y == 0x6b690000);
}
