#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace mamerecomp::naomi {

// Device-facing memory seam for the single SH-4 used by MvC2. A future board
// map routes these callbacks to cartridge ROM, system RAM, Holly and PVR2.
struct Sh4Bus {
    std::function<std::uint8_t(std::uint32_t)> read8;
    std::function<std::uint16_t(std::uint32_t)> read16;
    std::function<std::uint32_t(std::uint32_t)> read32;
    std::function<void(std::uint32_t, std::uint8_t, std::uint32_t)> write8;
    std::function<void(std::uint32_t, std::uint16_t, std::uint32_t)> write16;
    std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> write32;
};

struct Sh4Context;
using Sh4TranslatedFunction = void (*)(Sh4Context&);

// Register ABI consumed by the Initial-D-derived C++ emitter. This is the
// execution state only; instruction dispatch, exception entry, MMU/cache,
// timing and Naomi address decoding are separate layers.
struct Sh4Context {
    std::array<std::uint32_t, 16> r{};
    std::array<std::uint32_t, 8> r_bank{};
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
    std::uint64_t instruction_budget = 0;
    std::uint64_t instructions_executed = 0;
    std::uint32_t unresolved_target = 0;
    std::uint32_t last_indirect_target = 0;
    std::uint64_t indirect_calls = 0;
    std::array<std::uint32_t, 32> recent_indirect_targets{};
    std::uint32_t exception_code = 0;
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

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) const;
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) const;
    [[nodiscard]] std::uint32_t read32(std::uint32_t address) const;
    void write8(std::uint32_t address, std::uint8_t value) const;
    void write16(std::uint32_t address, std::uint16_t value) const;
    void write32(std::uint32_t address, std::uint32_t value) const;
    void dispatch_indirect(std::uint32_t target);
    bool consume_instruction(unsigned cycles = 1);
    void setIrlnInput(unsigned encoded_level);
    bool acceptInterrupt();
};

inline void host_indirect_call(Sh4Context& context, std::uint32_t target) {
    context.dispatch_indirect(target);
}

struct Sh4RunResult {
    std::uint64_t instructions_executed = 0;
    std::uint32_t unresolved_target = 0;
    bool stopped_by_budget = false;
};

class Sh4Dispatcher {
public:
    void add(std::uint32_t guest_address, Sh4TranslatedFunction function);
    [[nodiscard]] bool contains(std::uint32_t guest_address) const;
    Sh4RunResult run(Sh4Context& context, std::uint32_t entry,
                     std::uint64_t instruction_budget) const;

private:
    void call(Sh4Context& context, std::uint32_t target) const;
    std::unordered_map<std::uint32_t, Sh4TranslatedFunction> m_functions;
};

} // namespace mamerecomp::naomi
