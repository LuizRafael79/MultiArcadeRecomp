// Common PlayStation memory map used by ZN-1, ZN-2 and System 12 adapters.
#pragma once

#include "psx_aot_abi.h"
#include "dma.h"
#include "gpu.h"
#include "gte.h"
#include "timers.h"
#include "mdec.h"
#include "sio.h"
#include "spu.h"
#include "../../runtime/machine_io.hpp"

#include <cstdint>
#include <array>
#include <utility>
#include <vector>

namespace mamerecomp::psx {

struct MemoryConfig {
    size_t ram_bytes = 2 * 1024 * 1024;
    size_t vram_bytes = 2 * 1024 * 1024;
    uint32_t ram_config = 0x00000800; // MAME console default: 2 MiB window.
    // Rate of the CPU core's execution-cycle counter, not the input crystal.
    // MAME converts four input clocks into one execute cycle; PSX cores expose
    // one execution cycle per retired instruction to this scheduler.
    uint64_t cpu_cycle_hz = 8'467'200;
    size_t bios_window_bytes = 0x80000;
};

// MAME configures the ZN CPUs at 67.7376 MHz (ZN-1) and 100 MHz (ZN-2),
// then maps four crystal clocks to one PSX execute cycle. Keep device time
// aligned to that execute-cycle rate, as the instruction counter does.
inline constexpr MemoryConfig kZn1MemoryConfig{4 * 1024 * 1024, 2 * 1024 * 1024, 0x00000c00, 16'934'400};
inline constexpr MemoryConfig kZn2MemoryConfig{4 * 1024 * 1024, 2 * 1024 * 1024, 0x00000300, 25'000'000};
inline constexpr MemoryConfig kSystem12MemoryConfig{4 * 1024 * 1024, 2 * 1024 * 1024, 0x00000300, 25'000'000, 0x400000};

// Owns the common RAM/scratch/VRAM storage and maps BIOS plus the PSX
// expansion/internal register windows. Board-specific address ranges are
// delegated to PsxBus callbacks supplied at construction.
class MachineMemory {
public:
    explicit MachineMemory(MemoryConfig config = {});
    bool load_bios(const uint8_t *bytes, size_t size);
    PsxBus make_bus();
    void attach_cpu(PsxCpuState *state);
    bool read(uint32_t address, unsigned size, uint32_t *value);
    bool write(uint32_t address, unsigned size, uint32_t value, uint32_t pc = 0);
    void set_board_bus(const PsxBus *bus) {
        board_bus_ = bus;
        board_memory_bus_ = bus
            ? mamerecomp::runtime::AddressBus{this, board_read_adapter,
                                               board_write_adapter}
            : mamerecomp::runtime::AddressBus{};
    }

    std::vector<uint8_t> &ram() { return ram_; }
    std::vector<uint8_t> &vram() { return vram_; }
    const std::vector<uint8_t> &bios() const { return bios_; }
    uint32_t ram_config() const { return ram_config_; }
    void raise_irq(unsigned source);
    void set_irq(unsigned source, bool active);
    DmaController &dma() { return dma_; }
    void set_dma_device_read(unsigned channel, DmaController::DeviceRead callback) {
        dma_.set_device_read(channel, std::move(callback));
    }
    Gpu &gpu() { return gpu_; }
    Sio0 &sio() { return sio_; }
    std::vector<int16_t> take_spu_audio() {
        if (cpu_state_) {
            advance_spu_to(cpu_state_->cycles);
            update_event_deadline();
        }
        return spu_.take_audio();
    }
    uint64_t profile_gte_ns() const { return profile_gte_ns_; }
    uint64_t profile_gte_commands() const { return profile_gte_commands_; }

private:
    static bool read_cb(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool write_cb(void *, uint32_t, unsigned, uint32_t, uint32_t);
    static bool board_read_adapter(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool board_write_adapter(void *, uint32_t, unsigned, uint32_t, uint32_t);
    static bool cache_read_cb(void *, uint32_t, unsigned, uint32_t *);
    static bool cache_write_cb(void *, uint32_t, unsigned, uint32_t);
    static void check_interrupts_cb(void *, PsxCpuState *, uint32_t);
    static bool scheduler_yield_requested_cb(void *);
    static void sio_irq_cb(void *, unsigned, bool);
    static bool cop2_command_cb(void *, PsxCpuState *, uint32_t, uint32_t);
    static bool cop2_read_cb(void *, PsxCpuState *, uint32_t, bool, unsigned, uint32_t *);
    static bool cop2_write_cb(void *, PsxCpuState *, uint32_t, bool, unsigned, uint32_t);
    bool ram_offset(uint32_t address, size_t *offset) const;
    size_t rom_window_size() const;
    bool byte_read(uint32_t address, uint8_t *value);
    bool byte_write(uint32_t address, uint8_t value);
    uint32_t cache_readword(uint32_t address) const;
    void cache_writeword(uint32_t address, uint32_t value);
    void advance_gpu_to(uint64_t cycles);
    void advance_spu_to(uint64_t cycles);
    void update_event_deadline();
    void sync_irq_line();

    std::vector<uint8_t> ram_;
    DmaController dma_;
    std::vector<uint8_t> scratch_ = std::vector<uint8_t>(1024);
    std::vector<uint8_t> vram_;
    Gpu gpu_;
    Gte gte_;
    RootTimers timers_;
    Sio0 sio_;
    Sio0 sio1_;
    Spu spu_;
    psxmdec_device mdec_;
    std::vector<uint8_t> bios_;
    uint32_t ram_config_;
    size_t visible_ram_bytes_;
    uint64_t cpu_cycle_hz_;
    uint64_t current_cpu_cycles_ = 0;
    uint64_t next_gpu_event_cycle_ = 0;
    uint64_t next_spu_irq_cycle_ = UINT64_MAX;
    bool profile_timing_ = false;
    uint64_t profile_gte_ns_ = 0;
    uint64_t profile_gte_commands_ = 0;
    size_t bios_window_bytes_;
    uint32_t irq_status_ = 0;
    uint32_t irq_mask_ = 0;
    uint32_t expansion_base_ = 0x1f000000;
    uint32_t expansion_config_ = 0;
    uint32_t rom_config_ = 0x00130000;
    uint32_t biu_ = 0;
    std::array<uint8_t, 0x70> internal_ram_{};
    std::array<uint32_t, 256> icache_tags_{};
    std::array<uint32_t, 1024> icache_{};
    std::array<uint32_t, 256> dcache_{};
    const PsxBus *board_bus_ = nullptr;
    mamerecomp::runtime::AddressBus board_memory_bus_{};
    PsxCpuState *cpu_state_ = nullptr;
};

struct RunCounters {
    uint64_t aot_instructions = 0;
    uint64_t fallback_instructions = 0;
    uint64_t idle_loop_instructions = 0;
    uint64_t missing_fetches = 0;
};

// Executes one instruction using a generated block when one starts at PC;
// otherwise fetches and executes one instruction through the interpreter.
// This is the common coverage/fallback policy requested for all CPU cores.
PsxAotExit execute_one(PsxCpuState *state, const PsxBus *bus,
                       const PsxAotEntry *entries, size_t entry_count,
                       RunCounters *counters);
PsxAotExit execute_until(PsxCpuState *state, const PsxBus *bus,
                         const PsxAotEntry *entries, size_t entry_count,
                         RunCounters *counters, uint64_t target_cycle);

} // namespace mamerecomp::psx
