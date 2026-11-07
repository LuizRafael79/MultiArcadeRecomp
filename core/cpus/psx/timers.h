#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace mamerecomp::psx {

// Three PSX root counters. CPU-clock operation and target/overflow IRQs are
// implemented; external video clocks and sync-to-blank behavior are approximated.
class RootTimers {
public:
    using Irq = std::function<void(unsigned, bool)>;
    explicit RootTimers(Irq irq = {}, uint64_t cpu_cycle_hz = 33'868'800)
        : irq_(std::move(irq)), cpu_cycle_hz_(cpu_cycle_hz) {}
    uint32_t read(unsigned dword_offset);
    uint32_t peek(unsigned dword_offset) const;
    void write(unsigned dword_offset, uint32_t value);
    void advance(uint64_t cpu_cycles);
    uint64_t next_event_cycle() const { return next_event_cycle_; }
private:
    struct Timer {
        uint32_t count = 0, mode = 0, target = 0;
        uint64_t phase = 0, event_cycle = UINT64_MAX;
        bool configured = false, event_armed = false;
    };
    void recompute_next_event();
    std::array<Timer, 3> timers_{};
    Irq irq_;
    uint64_t cpu_cycle_hz_;
    uint64_t elapsed_cycles_ = 0;
    uint64_t next_event_cycle_ = 1;
};

} // namespace mamerecomp::psx
