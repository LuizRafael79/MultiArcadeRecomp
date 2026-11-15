// license:BSD-3-Clause
// Regression vectors for DSP16A DAU behavior used by the QSound DL-1425 ROM.

#include "dsp16a.hpp"
#include "dsp16a_dau_math.hpp"

#include <array>
#include <cassert>
#include <cstdint>

using multiarcaderecomp::dsp16::Dsp16A;
using multiarcaderecomp::dsp16::Dsp16ABus;

int main()
{
    // JTDSP16 F2 RND rounds by adding accumulator bit 15 to the upper
    // 20-bit value. In particular, a negative exact half rounds toward zero;
    // MAME currently uses a different tie rule and documents this as unclear.
    using multiarcaderecomp::dsp16::detail::round_accumulator_f2_11;
    assert(round_accumulator_f2_11(-0x8000) == 0);
    assert(round_accumulator_f2_11(-0x8001) == -0x10000);
    assert(round_accumulator_f2_11(-0x7fff) == 0);
    assert(round_accumulator_f2_11(0x8000) == 0x10000);
    assert(round_accumulator_f2_11(0x7fff) == 0);
    assert(round_accumulator_f2_11(0x7fffffff0LL) == 0x800000000LL);
    assert(round_accumulator_f2_11(-0x800000001LL) == -0x800000000LL);

    // F2=9 increments the source's 21-bit high field. JTDSP16 wires the
    // low-word clear input from the destination accumulator's AUC bit, while
    // retaining the source low word when that bit is clear.
    using multiarcaderecomp::dsp16::detail::increment_accumulator_high_f2_9;
    assert(increment_accumulator_high_f2_9(0x12345678, 0, 0) == 0x12350000);
    assert(increment_accumulator_high_f2_9(0x12345678, 0x10, 0) == 0x12355678);
    assert(increment_accumulator_high_f2_9(0x12345678, 0x10, 1) == 0x12350000);
    assert(increment_accumulator_high_f2_9(0x12345678, 0x20, 0) == 0x12350000);
    assert(increment_accumulator_high_f2_9(0x12345678, 0x20, 1) == 0x12355678);
    assert(increment_accumulator_high_f2_9(0x7fffffff0LL, 0, 0) == 0x800000000LL);

    // AUC[4:5], not PSW overflow/guard bits, control low-half retention on
    // an upper-half accumulator load in the JTDSP16 DAU.
    using multiarcaderecomp::dsp16::detail::load_accumulator_high;
    assert(load_accumulator_high(0x12345678, -2, false) == -0x20000);
    assert(load_accumulator_high(0x12345678, -2, true) == -0x1a988);
    assert(load_accumulator_high(0x12345678, 0x1234, false) == 0x12340000);
    assert(load_accumulator_high(0x12345678, 0x1234, true) == 0x12345678);

    // AUC[1:0] = 2 shifts the 32-bit P bus, then sign-extends that result.
    // A negative P whose top two bits are discarded must stay positive after
    // wraparound; widening the signed P before shifting makes it a huge
    // negative accumulator and clips QSound output samples.
    using multiarcaderecomp::dsp16::detail::align_product_left_two;
    using multiarcaderecomp::dsp16::detail::align_product;
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

    // JTDSP16's heads/tails condition reads LFSR[31] and advances the LFSR
    // on an F2 condition check. MAME still marks this condition unimplemented.
    constexpr std::uint32_t lfsr_seed = 0xcafe'cafeU;
    constexpr std::uint32_t lfsr_feedback =
        ((lfsr_seed >> 31) ^ (lfsr_seed >> 21) ^ (lfsr_seed >> 1) ^ lfsr_seed) & 1U;
    constexpr std::uint16_t f2_heads_tails = 0x9800U | (14U << 5) | 0x08U;
    Dsp16A random_dsp;
    const std::array<std::uint16_t, 1> random_firmware{f2_heads_tails};
    random_dsp.set_firmware(random_firmware);
    random_dsp.run_cycles(2);
    assert(random_dsp.state().lfsr == ((lfsr_seed << 1) | lfsr_feedback));

    // MAME's XAAU PT increment wraps the 12-bit offset without carrying into
    // the page nibble. DL-1425 executes X=*PT++[I] in its sample path.
    std::uint16_t external_read_address = 0;
    Dsp16ABus pt_bus;
    pt_bus.program_read = [&](std::uint16_t address) {
        external_read_address = address;
        return std::uint16_t(0x1234);
    };
    Dsp16A pt_dsp(pt_bus);
    constexpr std::array<std::uint16_t, 3> pt_firmware{
        0x5080, 0x8fff, // PT = 0x8fff via R=N
        0xf844,         // F1 ; Y=Y ; X=*PT++
    };
    pt_dsp.set_firmware(pt_firmware);
    pt_dsp.run_cycles(12);
    assert(external_read_address == 0x8fff);
    assert(pt_dsp.state().pt == 0x8000);

    // When a two-cycle instruction is the final word loaded by a DO loop,
    // MAME overlaps the ROM read with OP2. The first cached iteration starts
    // two cycles after the load-pass body, with no extra Prefetch cycle.
    Dsp16A* do_cycle_dsp_ptr = nullptr;
    std::array<std::uint64_t, 2> do_body_cycles{};
    unsigned do_body_count = 0;
    Dsp16ABus do_cycle_bus;
    do_cycle_bus.instruction_trace = [&](multiarcaderecomp::dsp16::Dsp16ATracePhase phase,
                                         std::uint16_t pc, std::uint16_t opcode,
                                         const multiarcaderecomp::dsp16::Dsp16AState&) {
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::Before &&
            pc == 1 && opcode == 0xa0d1 && do_body_count < do_body_cycles.size())
            do_body_cycles[do_body_count++] = do_cycle_dsp_ptr->cycles();
    };
    Dsp16A do_cycle_dsp(do_cycle_bus);
    do_cycle_dsp_ptr = &do_cycle_dsp;
    constexpr std::array<std::uint16_t, 3> do_cycle_firmware{
        0x7082, // DO 2 { two-cycle instruction }
        0xa0d1, // AU: *R0++ = Y
        0x0002, // branch to self after the loop
    };
    do_cycle_dsp.set_firmware(do_cycle_firmware);
    do_cycle_dsp.run_cycles(16);
    assert(do_body_count == 2);
    assert(do_body_cycles[1] - do_body_cycles[0] == 2);

    // External INT is not arbitrated while the first pass of a DO loop loads
    // its cache. The QSound DSP receives its host command IRQ on this path.
    bool body_instruction_seen = false;
    bool interrupt_acknowledged = false;
    bool interrupt_status_seen = false;
    Dsp16A* irq_dsp_ptr = nullptr;
    Dsp16ABus irq_bus;
    irq_bus.interrupt_ack = [&](bool level) {
        if (!level) {
            assert(body_instruction_seen);
            interrupt_acknowledged = true;
        }
    };
    irq_bus.instruction_trace = [&](multiarcaderecomp::dsp16::Dsp16ATracePhase phase,
                                    std::uint16_t pc, std::uint16_t opcode,
                                    const multiarcaderecomp::dsp16::Dsp16AState& state) {
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::After &&
            pc == 2 && opcode == 0x7082) // DO 2 { one instruction }
            irq_dsp_ptr->set_irq(true);
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::Before &&
            pc == 3 && opcode == 0x2000)
            body_instruction_seen = true;
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::After &&
            pc == 0x0820 && opcode == 0x41c0)
            interrupt_status_seen = (state.a[1] & 0x0001'0000LL) != 0;
    };
    Dsp16A irq_dsp(irq_bus);
    irq_dsp_ptr = &irq_dsp;
    std::array<std::uint16_t, 0x0821> irq_firmware{};
    irq_firmware[0] = 0x51c0; // R=N to PIOC
    irq_firmware[1] = 0x0820; // enable INT, and branch vector to 0x0820
    irq_firmware[2] = 0x7082; // DO 2 { one instruction }
    irq_firmware[3] = 0x2000; // body must run before IRQ arbitration
    irq_firmware[0x0820] = 0x41c0; // copy PIOC to A0 in the IRQ handler
    irq_dsp.set_firmware(irq_firmware);
    irq_dsp.run_cycles(32);
    assert(body_instruction_seen);
    assert(interrupt_acknowledged);
    assert(interrupt_status_seen);

    // K=1 is outside the documented DSP16A DO range, but verify the
    // interpreter does not run the load-pass body a second time.
    unsigned single_pass_count = 0;
    Dsp16ABus single_pass_bus;
    single_pass_bus.instruction_trace = [&](multiarcaderecomp::dsp16::Dsp16ATracePhase phase,
                                            std::uint16_t pc, std::uint16_t opcode,
                                            const multiarcaderecomp::dsp16::Dsp16AState&) {
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::After &&
            pc == 1 && opcode == 0x2000)
            ++single_pass_count;
    };
    Dsp16A single_pass_dsp(single_pass_bus);
    constexpr std::array<std::uint16_t, 3> single_pass_firmware{
        0x7081, // DO 1 { one instruction }
        0x2000,
        0x0002, // branch to self after the loop
    };
    single_pass_dsp.set_firmware(single_pass_firmware);
    single_pass_dsp.run_cycles(12);
    assert(single_pass_count == 1);

    bool aliased_accumulator_seen = false;
    Dsp16ABus alias_bus;
    alias_bus.data_read = [](std::uint16_t) { return std::uint16_t(2); };
    alias_bus.instruction_trace = [&](multiarcaderecomp::dsp16::Dsp16ATracePhase phase,
                                      std::uint16_t pc, std::uint16_t opcode,
                                      const multiarcaderecomp::dsp16::Dsp16AState& state) {
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::After &&
            pc == 2 && opcode == 0xc9a0) {
            aliased_accumulator_seen = true;
            assert(state.a[0] == 3);
            assert(state.y == 1); // snapshot source A0 before destination write
        }
    };
    Dsp16A alias_dsp(alias_bus);
    constexpr std::array<std::uint16_t, 4> alias_firmware{
        0x99ae, // increment A0 (always-true DAU condition)
        0xbc00, // load YL=2 from [R0], writing the F1 result to A1
        0xc9a0, // F1: A0=A0+Y ; Y=A0 ; X=*PT++
        0x0003, // branch to self after the aliasing case
    };
    alias_dsp.set_firmware(alias_firmware);
    alias_dsp.run_cycles(20);
    assert(aliased_accumulator_seen);

    // F2 left shifts match both MAME and JTDSP16: the 32-bit shift result is
    // sign-extended into the architectural accumulator rather than retaining
    // discarded guard bits from the 36-bit source.
    bool f2_shift_seen = false;
    Dsp16ABus f2_bus;
    f2_bus.data_read = [](std::uint16_t) { return std::uint16_t(0x7000); };
    f2_bus.instruction_trace = [&](multiarcaderecomp::dsp16::Dsp16ATracePhase phase,
                                   std::uint16_t pc, std::uint16_t opcode,
                                   const multiarcaderecomp::dsp16::Dsp16AState& state) {
        if (phase == multiarcaderecomp::dsp16::Dsp16ATracePhase::After &&
            pc == 2 && opcode == 0x982e) {
            f2_shift_seen = true;
            assert(state.a[0] == -0x20000000LL);
        }
    };
    Dsp16A f2_dsp(f2_bus);
    constexpr std::array<std::uint16_t, 4> f2_firmware{
        0xb810, // load YH=0x7000 from [R0]
        0x3180, // A0=Y
        0x982e, // F2: A0=A0<<1
        0x0003, // branch to self
    };
    f2_dsp.set_firmware(f2_firmware);
    f2_dsp.run_cycles(20);
    assert(f2_shift_seen);
}
