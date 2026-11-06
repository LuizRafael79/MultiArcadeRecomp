#pragma once

// PSX DMA register behavior follows MAME's BSD-3-Clause implementation in
// src/devices/cpu/psx/dma.cpp. This is an independent adapter; transfers are
// device callbacks and timing are supplied by the shared PSX machine layer.

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mamerecomp::psx {

class DmaController {
public:
    using DeviceWrite = std::function<void(uint32_t, std::span<const uint32_t>)>;
    using DeviceRead = std::function<std::vector<uint32_t>(uint32_t, size_t)>;
    using Irq = std::function<void(unsigned, bool)>;

    explicit DmaController(std::vector<uint8_t> &ram, Irq irq = {});
    void set_device_write(unsigned channel, DeviceWrite callback);
    void set_device_read(unsigned channel, DeviceRead callback);
    uint32_t read(unsigned dword_offset) const;
    void write(unsigned dword_offset, uint32_t value);
    void advance_to(uint64_t cpu_cycles, uint64_t cpu_hz);
    uint64_t next_event_cycle() const;
    uint32_t dpcr() const { return dpcr_; }
    uint32_t dicr() const { return dicr_; }

private:
    struct Channel { uint32_t base = 0, block = 0, control = 0; };
    uint32_t ram_word(uint32_t address) const;
    void set_ram_word(uint32_t address, uint32_t value);
    void start(unsigned channel);
    void complete(unsigned channel);
    void update_irq();
    std::vector<uint32_t> transfer_words(unsigned channel, uint32_t count,
                                          bool decrement) const;

    std::vector<uint8_t> &ram_;
    Irq irq_;
    Channel channels_[7]{};
    DeviceWrite device_write_[7]{};
    DeviceRead device_read_[7]{};
    bool deferred_write_[7]{};
    uint32_t deferred_address_[7]{};
    uint32_t deferred_count_[7]{};
    bool deferred_decrement_[7]{};
    uint32_t dpcr_ = 0;
    uint32_t dicr_ = 0;
    uint64_t current_cpu_cycles_ = 0;
    uint64_t cpu_hz_ = 33'868'800;
    uint64_t completion_cycles_[7]{};
};

} // namespace mamerecomp::psx
