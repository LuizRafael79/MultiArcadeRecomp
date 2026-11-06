// license:BSD-3-Clause
// Protocol behavior adapted from MAME src/mame/sony/znmcu.cpp.
#include "zn_mcu.hpp"

namespace mamerecomp::zn {

uint64_t ZnMcu::usec_cycles(uint64_t usec) const
{
    return (cpu_cycle_hz_ * usec + 999'999) / 1'000'000;
}

void ZnMcu::select(bool active)
{
    if (active_ == active) return;
    active_ = active;
    if (!active_) {
        bit_ = 0;
        byte_ = 0;
        deadline_ = now_cycles_ + usec_cycles(50);
        pending_ = true;
        pending_low_pulse_ = true;
    } else {
        data_out_ = 1;
        dsr_ = 1;
        pending_ = false;
    }
}

void ZnMcu::timer_fired(bool low_pulse)
{
    if (low_pulse) {
        dsr_ = 0;
        if (byte_ == 0) {
            data_bytes_ = analog_select_ ? 8u : trackball_select_ ? 6u : 1u;
            send_.fill(0);
            send_[0] = static_cast<uint8_t>((data_bytes_ << 4) | dsw_);
            if (analog_select_) {
                for (unsigned i = 0; i < 8; ++i) send_[i + 1] = 0xff;
            } else if (trackball_select_) {
                for (unsigned i = 0; i < 4; ++i) {
                    const uint16_t delta = uint16_t(0 - trackball_[i]);
                    trackball_[i] = 0;
                    send_[1 + i] = static_cast<uint8_t>(delta);
                }
                send_[2] = send_[1] >> 4;
                send_[5] = send_[4] >> 4;
            }
        }
        deadline_ = now_cycles_ + usec_cycles(5);
        pending_ = true;
        pending_low_pulse_ = false;
    } else {
        dsr_ = 1;
        pending_ = false;
    }
}

void ZnMcu::clock(int state)
{
    state = state != 0;
    if (clock_ != state && !state && !active_) {
        const uint8_t data = byte_ <= data_bytes_ && byte_ < send_.size()
            ? send_[byte_] : 0;
        data_out_ = (data >> bit_) & 1;
        if (++bit_ == 8) {
            bit_ = 0;
            ++byte_;
            if (byte_ < data_bytes_) {
                deadline_ = now_cycles_ + usec_cycles(50);
                pending_ = true;
                pending_low_pulse_ = true;
            }
        }
    }
    clock_ = state;
}

void ZnMcu::advance(uint64_t cpu_cycles)
{
    now_cycles_ = cpu_cycles;
    if (pending_ && now_cycles_ >= deadline_)
        timer_fired(pending_low_pulse_);
}

} // namespace mamerecomp::zn
