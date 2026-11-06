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
    explicit RootTimers(Irq irq = {}) : irq_(std::move(irq)) {}
    uint32_t read(unsigned dword_offset);
    uint32_t peek(unsigned dword_offset) const;
    void write(unsigned dword_offset, uint32_t value);
    void advance(uint64_t cpu_cycles);
    uint64_t next_event_cycle() const { return next_event_cycle_; }
private:
    struct Timer { uint32_t count = 0, mode = 0, target = 0; uint64_t phase = 0; bool irq_fired = false; };
    void recompute_next_event();
    std::array<Timer, 3> timers_{};
    Irq irq_;
    uint64_t elapsed_cycles_ = 0;
    uint64_t next_event_cycle_ = 1;
};

} // namespace mamerecomp::psx
