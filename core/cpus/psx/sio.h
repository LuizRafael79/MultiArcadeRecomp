#pragma once

#include "psx_aot_abi.h"

#include <cstdint>

namespace multiarcaderecomp::psx {

// PSX SIO0 byte-wide serial controller subset, timed from R3000A cycles.
// Register/shift behavior follows MAME's BSD-3-Clause PSX SIO device.
class Sio0 {
public:
    using Irq = void (*)(void *, unsigned, bool);
    explicit Sio0(Irq irq = nullptr, void *opaque = nullptr,
                  unsigned irq_source = 7)
        : irq_(irq), opaque_(opaque), irq_source_(irq_source) {}
    uint32_t read(unsigned reg);
    void write(unsigned reg, uint32_t value, uint32_t mask = 0xffffffffu,
               const PsxBus *peripherals = nullptr);
    void advance(uint64_t cpu_cycles, const PsxBus *peripherals,
                 uint64_t cpu_cycle_hz = 8'467'200);
    uint64_t next_event_cycle() const { return next_event_cycle_; }
    void set_dsr_line(int state);
    bool busy() const { return tx_bits_ != 0 || rx_bits_ != 0 || !(status_ & 0x04u); }
    uint64_t transmitted_bytes() const { return transmitted_bytes_; }
    uint64_t received_bytes() const { return received_bytes_; }

private:
    void interrupt();
    void tick(const PsxBus *peripherals);
    void reschedule();

    uint32_t status_ = 0x05;
    uint32_t mode_ = 0, control_ = 0, baud_ = 0;
    uint8_t tx_data_ = 0, rx_data_ = 0xff;
    uint8_t tx_shift_ = 0, rx_shift_ = 0;
    unsigned tx_bits_ = 0, rx_bits_ = 0;
    uint64_t elapsed_cycles_ = 0, phase_ = 0;
    uint64_t transmitted_bytes_ = 0, received_bytes_ = 0;
    uint64_t next_event_cycle_ = UINT64_MAX;
    uint64_t cpu_cycle_hz_ = 8'467'200;
    int dsr_line_ = 1;
    Irq irq_ = nullptr;
    void *opaque_ = nullptr;
    unsigned irq_source_ = 7;
};

} // namespace multiarcaderecomp::psx
