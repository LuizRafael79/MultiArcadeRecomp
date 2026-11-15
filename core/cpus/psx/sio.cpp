// license:BSD-3-Clause
// PSX serial register and shift behavior adapted from MAME's sio.cpp.
#include "sio.h"

#include <algorithm>

namespace multiarcaderecomp::psx {

uint32_t Sio0::read(unsigned reg)
{
    switch (reg & 3) {
    case 0: {
        const uint32_t value = rx_data_;
        status_ &= ~0x02u;
        rx_data_ = 0xff;
        return value;
    }
    case 1: return status_;
    case 2: return ((control_ & 0xffff) << 16) | (mode_ & 0xffff);
    case 3: return (baud_ & 0xffff) << 16;
    }
    return 0;
}

void Sio0::write(unsigned reg, uint32_t value, uint32_t mask,
                 const PsxBus *peripherals)
{
    switch (reg & 3) {
    case 0:
        if (mask & 0xff) {
            tx_data_ = uint8_t(value);
            status_ &= ~(0x01u | 0x04u);
            // MAME reschedules its SIO timer from the data-register write.
            // Keep the first bit a full baud period after this write instead
            // of inheriting a fractional period accumulated while idle.
            phase_ = 0;
        }
        break;
    case 2:
        if (mask & 0xffff) {
            mode_ = value & 0xffff;
            phase_ = 0;
        }
        if (mask & 0xffff0000) {
            control_ = (value >> 16) & 0xffff;
            if (control_ & (1u << 6)) {
                status_ |= 0x05;
                status_ &= ~(0x02u | 0x10u | 0x200u);
                tx_bits_ = rx_bits_ = 0;
                tx_data_ = 0xff;
                phase_ = 0;
                if (irq_) irq_(opaque_, irq_source_, false);
                // MAME's SIO reset drives TXD high. CAT702 samples that pin
                // on the next rising SCK edge, so propagate the line state
                // even when the reset happens between serial transfers.
                if (peripherals && peripherals->serial_data)
                    peripherals->serial_data(peripherals->opaque, 1);
            }
            if (control_ & (1u << 4)) {
                status_ &= ~0x200u;
                control_ &= ~(1u << 4);
                if (irq_) irq_(opaque_, irq_source_, false);
            }
        }
        break;
    case 3:
        if (mask & 0xffff0000) {
            baud_ = (value >> 16) & 0xffff;
            phase_ = 0;
        }
        break;
    default: break;
    }
    reschedule();
}

void Sio0::interrupt()
{
    status_ |= 0x200;
    if (irq_) irq_(opaque_, irq_source_, true);
}

void Sio0::set_dsr_line(int state)
{
    state = state != 0;
    if (state == dsr_line_) return;
    dsr_line_ = state;
    if (state) {
        status_ &= ~0x80u; // DSR is active low.
    } else if ((status_ & 0x80u) == 0) {
        status_ |= 0x80u;
        if (control_ & (1u << 12)) interrupt();
    }
}

void Sio0::tick(const PsxBus *peripherals)
{
    if (!tx_bits_ && (control_ & 1) && !(status_ & 4)) {
        tx_bits_ = 8;
        tx_shift_ = tx_data_;
        rx_bits_ = 8;
        rx_shift_ = 0;
        status_ |= 0x05;
    }
    if (tx_bits_) {
        if (peripherals && peripherals->serial_clock)
            peripherals->serial_clock(peripherals->opaque, 0);
        if (peripherals && peripherals->serial_data)
            peripherals->serial_data(peripherals->opaque, tx_shift_ & 1);
        tx_shift_ >>= 1;
        --tx_bits_;
        if (peripherals && peripherals->serial_clock)
            peripherals->serial_clock(peripherals->opaque, 1);
        if (rx_bits_) {
            const unsigned input = peripherals && peripherals->serial_read
                ? unsigned(peripherals->serial_read(peripherals->opaque) != 0) : 1u;
            rx_shift_ = uint8_t((rx_shift_ >> 1) | (input << 7));
            if (--rx_bits_ == 0) {
                ++received_bytes_;
                if (status_ & 2) status_ |= 0x10;
                else { rx_data_ = rx_shift_; status_ |= 2; }
                if (control_ & (1u << 11)) interrupt();
            }
        }
        if (!tx_bits_) {
            ++transmitted_bytes_;
            if (control_ & (1u << 10)) interrupt();
        }
    }
}

void Sio0::advance(uint64_t cpu_cycles, const PsxBus *peripherals,
                   uint64_t cpu_cycle_hz)
{
    cpu_cycle_hz_ = cpu_cycle_hz;
    if (peripherals && peripherals->serial_dsr)
        set_dsr_line(peripherals->serial_dsr(peripherals->opaque));
    if (cpu_cycles <= elapsed_cycles_) return;
    const uint64_t delta = cpu_cycles - elapsed_cycles_;
    elapsed_cycles_ = cpu_cycles;
    // MAME's timer is stopped whenever TX is empty. Do not accumulate baud
    // phase while idle, or the next byte can begin partway through its first
    // bit period and desynchronize edge driven peripherals such as CAT702.
    if ((status_ & 0x04u) && !tx_bits_) {
        phase_ = 0;
        next_event_cycle_ = UINT64_MAX;
        return;
    }
    unsigned prescaler = 0;
    switch (mode_ & 3) {
    case 1: prescaler = 1; break;
    case 2: prescaler = 16; break;
    case 3: prescaler = 64; break;
    default:
        next_event_cycle_ = UINT64_MAX;
        return;
    }
    if (!baud_ || !cpu_cycle_hz) {
        next_event_cycle_ = UINT64_MAX;
        return;
    }
    // MAME's SIO period is expressed in 33.8688 MHz reference clocks. Convert
    // the elapsed execute cycles using the PSX core's effective cycle rate
    // (one execute cycle per four input clocks, per MAME's PSX core).
    const uint64_t divisor = cpu_cycle_hz * prescaler * baud_;
    const uint64_t numerator = delta * 33'868'800ull + phase_;
    uint64_t ticks = numerator / divisor;
    phase_ = numerator % divisor;
    while (ticks--) tick(peripherals);
    reschedule();
}

void Sio0::reschedule()
{
    if (((status_ & 0x04u) && !tx_bits_) || !baud_ || !cpu_cycle_hz_) {
        next_event_cycle_ = UINT64_MAX;
        return;
    }
    unsigned prescaler = 0;
    switch (mode_ & 3) {
    case 1: prescaler = 1; break;
    case 2: prescaler = 16; break;
    case 3: prescaler = 64; break;
    default:
        next_event_cycle_ = UINT64_MAX;
        return;
    }
    const uint64_t divisor = cpu_cycle_hz_ * prescaler * baud_;
    if (phase_ >= divisor) {
        next_event_cycle_ = elapsed_cycles_ + 1;
        return;
    }
    const uint64_t remaining = divisor - phase_;
    const uint64_t cycles = (remaining + 33'868'800ull - 1) / 33'868'800ull;
    next_event_cycle_ = elapsed_cycles_ + std::max<uint64_t>(1, cycles);
}

} // namespace multiarcaderecomp::psx
