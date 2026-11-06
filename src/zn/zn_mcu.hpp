#pragma once

#include <array>
#include <cstdint>

namespace mamerecomp::zn {

// Sony ZN controller/security MCU serial HLE, based on MAME's znmcu_device.
// The MCU ROM is NO_DUMP in the supplied sets; this models the observable
// serial protocol used by the ZN BIOS and game I/O path.
class ZnMcu {
public:
    explicit ZnMcu(uint64_t cpu_cycle_hz) : cpu_cycle_hz_(cpu_cycle_hz) {}
    void select(bool active);
    void clock(int state);
    void advance(uint64_t cpu_cycles);
    void set_analog_select(bool active) { analog_select_ = active; }
    void set_trackball_select(bool active) { trackball_select_ = active; }
    void set_dsw(uint8_t value) { dsw_ = value & 0x0f; }
    int data_out() const { return data_out_; }
    int dsr() const { return dsr_; }
    bool event_pending() const { return pending_; }
    uint64_t next_event_cycle() const { return pending_ ? deadline_ : UINT64_MAX; }

private:
    void timer_fired(bool low_pulse);
    uint64_t usec_cycles(uint64_t usec) const;

    uint64_t cpu_cycle_hz_;
    uint64_t now_cycles_ = 0;
    uint64_t deadline_ = 0;
    std::array<uint8_t, 9> send_{};
    std::array<uint16_t, 4> trackball_{};
    bool active_ = true;
    bool analog_select_ = false;
    bool trackball_select_ = false;
    uint8_t dsw_ = 0x0f;
    bool pending_ = false;
    bool pending_low_pulse_ = false;
    int clock_ = 1;
    int data_out_ = 1;
    int dsr_ = 1;
    unsigned bit_ = 0;
    unsigned byte_ = 0;
    unsigned data_bytes_ = 0;
};

} // namespace mamerecomp::zn
