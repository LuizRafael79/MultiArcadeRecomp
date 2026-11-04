#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <functional>
#include <cstddef>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../core/cpus/sh4/flycast_cycles.hpp"

namespace mamerecomp::naomi {

// Device-facing memory seam for the single SH-4 used by MvC2. A future board
// map routes these callbacks to cartridge ROM, system RAM, Holly and PVR2.
struct Sh4PendingInterrupt {
    unsigned priority = 0;
    std::uint32_t event = 0;
};

struct Sh4Bus {
    std::function<std::uint8_t(std::uint32_t)> read8;
    std::function<std::uint16_t(std::uint32_t)> read16;
    std::function<std::uint32_t(std::uint32_t)> read32;
    std::function<void(std::uint32_t, std::uint8_t, std::uint32_t)> write8;
    std::function<void(std::uint32_t, std::uint16_t, std::uint32_t)> write16;
    std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> write32;
    std::function<std::optional<Sh4PendingInterrupt>()> pending_internal_interrupt;
};

struct Sh4Context;
using Sh4TranslatedFunction = void (*)(Sh4Context&);

struct Sh4TlbEntry {
    std::uint32_t pteh = 0;
    std::uint32_t ptel = 0;
    std::uint32_t ptea = 0;
};

// Internal control-flow signal: an SH-4 memory access raised an architectural
// exception and must unwind the current instruction before its side effects.
struct Sh4MemoryFault {};

// Register ABI consumed by the Initial-D-derived C++ emitter. This is the
// execution state; instruction dispatch, exception entry, MMU/cache, timing
// and Naomi address decoding remain separate layers.
struct Sh4Context {
    std::array<std::uint32_t, 16> r{};
    std::array<std::uint32_t, 8> r_bank{};
    // Flycast SH-4 interpreter's LDTLB writes the current PTE registers into
    // the UTLB slot selected by MMUCR.URC. Memory accesses use this state for
    // address translation and protection checks.
    std::array<Sh4TlbEntry, 64> utlb{};
    std::array<Sh4TlbEntry, 4> itlb{};
    // The SH-4 exposes two 32-byte write-through queues in the E area. PREF
    // submits the selected queue to the addressed bus target.
    std::array<std::uint8_t, 64> store_queue{};
    std::array<std::uint32_t, 16> fr{};
    std::array<float, 16> frf{};
    std::array<float, 16> xf{};

    std::uint32_t pc = 0;
    std::uint32_t control_target = 0; // sampled SH-4 branch target across the delay slot
    std::uint32_t pr = 0;
    std::uint32_t gbr = 0;
    std::uint32_t vbr = 0;
    std::uint32_t mach = 0;
    std::uint32_t macl = 0;
    std::uint32_t fpul = 0;
    std::uint32_t fpscr = 0;
    std::uint32_t sr = 0;
    std::uint32_t ssr = 0;
    std::uint32_t spc = 0;
    std::uint32_t sgr = 0;
    std::uint32_t dbr = 0;
    bool fpscr_pr = false;
    bool t = false;
    bool q = false;
    bool m_bit = false;
    bool fpu_bank_x = false;
    bool delay_slot = false;
    bool interpreter_only = false;
    bool sleeping = false;
    std::uint64_t instruction_budget = 0;
    // Flycast samples SH-4 external interrupts at its 448-cycle scheduler
    // slice boundary, not on every guest instruction. Both AOT and interpreted
    // operations retire through the shared Flycast opcode-cycle model below.
    unsigned cycles_until_irq_poll = 448;
    mamerecomp::cpus::sh4::flycast::CycleState cycle_state{};
    std::uint64_t instructions_executed = 0;
    std::uint64_t native_instructions = 0;
    std::uint64_t interpreted_instructions = 0;
    std::uint32_t unresolved_target = 0;
    std::uint32_t last_indirect_target = 0;
    std::uint64_t indirect_calls = 0;
    std::array<std::uint32_t, 32> recent_indirect_targets{};
    // Guest ICBI operations evict translated entries covering the same
    // physical 32-byte SH-4 I-cache line. Those PCs then execute interpreted.
    std::unordered_set<std::uint32_t> invalidated_aot_lines;
    // Per physical I-cache line write epochs let the dispatcher cache a live
    // code guard, then re-check only after a possible self-modifying write.
    std::unordered_map<std::uint32_t, std::uint64_t> aot_code_write_epochs;
    std::uint32_t exception_code = 0;
    // SH-4 synchronous-exception control state. `exception_code` remains the
    // public stop/diagnostic field; EXPEVT/TRA/TEA are guest-visible state.
    std::uint32_t expevt = 0;
    std::uint32_t tra = 0;
    std::uint32_t tea = 0;
    bool execution_stopped = false;
    bool interrupt_taken = false;
    unsigned irln = 15; // SH-4 IRLn level encoding; 15 means no interrupt.
    std::uint32_t intevt = 0;
    std::uint64_t interrupts_accepted = 0;
    std::function<void(unsigned)> cycle_sink;

    Sh4Bus* bus = nullptr;
    std::function<void(Sh4Context&, std::uint32_t)> indirect_call;
    std::function<void(Sh4Context&, std::uint32_t)> trap_handler;
    std::function<void(Sh4Context&, std::uint32_t)> store_queue_handler;
    // Cache operations can affect code/data visibility. The CPU core supplies
    // the operation/address; the board/runtime owns cache and AOT invalidation.
    std::function<bool(Sh4Context&, std::uint16_t, std::uint32_t)> cache_operation_handler;
    // Executes one guest instruction at the current PC. The implementation
    // must use consume_instruction(cycles, true) before applying side effects.
    // The dispatcher uses this only when the live PC has no matching AOT entry.
    std::function<bool(Sh4Context&)> interpreter_step;

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) const;
    [[nodiscard]] bool fetchInstruction16(std::uint32_t address,
                                          std::uint16_t& instruction) const;
    void write8(std::uint32_t address, std::uint8_t value) const;
    void write16(std::uint32_t address, std::uint16_t value) const;
    void write32(std::uint32_t address, std::uint32_t value) const;
    void noteAotCodeWrite(std::uint32_t address, unsigned width) const;
    void dispatch_indirect(std::uint32_t target);
    bool consume_instruction(unsigned cycles = 1, bool interpreted = false);
    bool retire_opcode(std::uint16_t opcode, bool interpreted = false);
    void setIrlnInput(unsigned encoded_level);
    bool acceptInterrupt();
};

inline void host_indirect_call(Sh4Context& context, std::uint32_t target) {
    context.dispatch_indirect(target);
}

struct Sh4RunResult {
    std::uint64_t instructions_executed = 0;
    std::uint64_t native_instructions = 0;
    std::uint64_t interpreted_instructions = 0;
    std::uint32_t unresolved_target = 0;
    bool stopped_by_budget = false;
};

class Sh4Dispatcher {
public:
    void add(std::uint32_t guest_address, Sh4TranslatedFunction function);
    void add(std::uint32_t guest_address, Sh4TranslatedFunction function,
             std::uint32_t code_base, std::size_t expected_word_count,
             std::uint64_t expected_hash);
    [[nodiscard]] bool contains(std::uint32_t guest_address) const;
    Sh4RunResult run(Sh4Context& context, std::uint32_t entry,
                     std::uint64_t instruction_budget) const;

private:
    void call(Sh4Context& context, std::uint32_t target) const;
    std::unordered_map<std::uint32_t, Sh4TranslatedFunction> m_functions;
    struct CodeGuard {
        std::uint32_t base = 0;
        std::size_t word_count = 0;
        std::uint64_t hash = 0;
    };
    struct GuardCache {
        bool initialized = false;
        bool matches = false;
        std::vector<std::pair<std::uint32_t, std::uint64_t>> line_epochs;
    };
    std::unordered_map<Sh4TranslatedFunction, CodeGuard> m_code_guards;
    mutable std::unordered_map<Sh4TranslatedFunction, GuardCache> m_guard_cache;
};

} // namespace mamerecomp::naomi
