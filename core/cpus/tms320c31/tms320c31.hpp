#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mamerecomp::tms320c31 {

// The C31 has a 24-bit, word-addressed program/data space. Every access in
// this interface is a 32-bit DSP word; board adapters translate the address
// into the board's address map.
struct Bus {
    void* opaque{};
    std::uint32_t (*read32)(void*, std::uint32_t word_address) noexcept{};
    void (*write32)(void*, std::uint32_t word_address,
                    std::uint32_t value) noexcept{};
    // MAME's memory_access_cache resolves direct ROM/RAM reads once and then
    // fetches opcodes through that cached pointer. Return a pointer to a
    // contiguous, directly readable page; unmapped/handler-backed pages use
    // read32 instead. The pointer must remain valid while the board mapping
    // is unchanged; writes to its backing memory remain immediately visible.
    const std::uint32_t* (*map_code_page)(void*,
        std::uint32_t page_word_address) noexcept{};
};

struct Register {
    std::uint32_t i32[2]{}; // MAME C3x format: mantissa/integer, exponent.
    std::uint32_t integer() const noexcept { return i32[0]; }
    std::int32_t mantissa() const noexcept {
        return static_cast<std::int32_t>(i32[0]);
    }
    std::int8_t exponent() const noexcept {
        return static_cast<std::int8_t>(i32[1]);
    }
    void set_mantissa(std::int32_t value) noexcept { i32[0] = value; }
    void set_exponent(std::int8_t value) noexcept {
        i32[1] = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
    }
    float as_float() const noexcept;
    double as_double() const noexcept;
    void from_double(double value) noexcept;
};

enum class StopReason : std::uint8_t {
    None, Budget, Idle, IllegalOpcode, MameUnimplemented, BusFault
};

struct State {
    struct HotPc { std::uint32_t pc{}; std::uint64_t count{}; };
    std::uint32_t pc{};
    std::array<Register, 36> r{};
    std::uint32_t bkmask{};
    std::uint32_t primary_bus_control{0x000010f8};
    std::uint16_t irq_state{};
    bool delayed{};
    bool irq_pending{};
    bool idling{};
    bool low_power{};
    bool hold_state{};
    std::uint64_t cycles{};
    std::uint64_t instructions{};
    std::uint64_t native_instructions{};
    std::uint64_t interpreted_instructions{};
    std::uint64_t aot_opcode_mismatches{};
    std::uint64_t uncovered_entry_instructions{};
    std::uint32_t last_aot_mismatch_pc{};
    std::uint32_t last_aot_mismatch_expected{};
    std::uint32_t last_aot_mismatch_live{};
    std::uint32_t last_uncovered_pc{};
    std::uint32_t last_uncovered_opcode{};
    std::array<HotPc, 1024> interpreted_hot_pcs{};
    std::uint32_t last_opcode{};
    std::uint32_t last_pc{};
    StopReason stop_reason{StopReason::None};
};

enum class Exit : std::uint8_t { Budget, Idle, Stopped, BusFault };

class Interpreter;
using AotBlock = bool (*)(Interpreter&, std::uint32_t offset);
struct AotEntry {
    AotBlock block{};
    std::uint32_t offset{};
};
AotEntry lookup_aot(std::uint32_t pc) noexcept;

class Interpreter {
public:
    explicit Interpreter(State& state, Bus bus);
    void reset() noexcept;
    void set_irq(unsigned line, bool asserted) noexcept;
    Exit run(std::uint64_t cycle_budget) noexcept;
    // Reads the live program word through the same direct-page cache used by
    // opcode fetches, without changing PC, cycle, or instruction counters.
    // Runtime code discovery/recompilation can use this as its code reader.
    bool read_code_word(std::uint32_t address,
                        std::uint32_t& value) noexcept;
    void invalidate_code_cache() noexcept;
    void invalidate_code_word(std::uint32_t address) noexcept;
    void set_aot_enabled(bool enabled) noexcept { m_aot_enabled = enabled; }
    // Unlike State::cycles, this also reflects cycles already consumed inside
    // the current run slice. Bus callbacks use it to synchronize peer devices.
    std::uint64_t current_cycles() const noexcept;
    void consume_cycles(std::uint32_t cycles) noexcept { burn_cycle(static_cast<int>(cycles)); }
    bool aot_begin(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept;
    bool aot_end(std::uint32_t expected_pc) noexcept;
    State& state() noexcept { return m_s; }
    const State& state() const noexcept { return m_s; }

    #include "mame_aot_handler_declarations.inc"

private:
    using tmsreg = Register;
    void execute_one();
    bool try_aot_delay_slot() noexcept;
    void check_irqs();
    std::uint32_t ROPCODE(std::uint32_t address) noexcept;
    std::uint32_t RMEM(std::uint32_t address) noexcept;
    void WMEM(std::uint32_t address, std::uint32_t value) noexcept;
    void mark_stop(StopReason reason, std::uint32_t opcode) noexcept;

    State& m_s;
    Bus m_bus;
    static constexpr std::uint32_t kCodePageShift = 12;
    static constexpr std::uint32_t kCodePageWords = 1u << kCodePageShift;
    static constexpr std::uint32_t kCodePageMask = kCodePageWords - 1;
    static constexpr std::size_t kCodePageCount = 1u << (24 - kCodePageShift);
    std::array<const std::uint32_t*, kCodePageCount> m_code_pages{};
    std::array<bool, kCodePageCount> m_code_page_resolved{};
    // Once an AOT PC has been validated, writes to that exact word clear its
    // bit through the board bus. This preserves self-modifying-code safety
    // while avoiding a full bus read for every execution of stable code.
    std::vector<std::uint64_t> m_aot_word_validated;
    std::int64_t m_icount{};
    std::uint64_t m_run_cycle_base{};
    std::uint64_t m_run_cycle_budget{};
    bool m_running{};
    bool m_bus_fault{};
    bool m_stopped{};
    bool m_recording_instruction{};
    bool m_aot_enabled{true};
    bool m_next_instruction_aot_mismatch{};
    bool m_next_instruction_uncovered{};
    static constexpr std::uint32_t CHIP_TYPE_TMS320C31 = 1;
    std::uint32_t m_chip_type{CHIP_TYPE_TMS320C31};
    std::function<void(std::uint32_t, int)> m_xf0_cb = [](auto, auto) {};
    std::function<void(std::uint32_t, int)> m_xf1_cb = [](auto, auto) {};
    std::function<void(std::uint32_t, int)> m_iack_cb = [](auto, auto) {};
    void debug_break() noexcept { mark_stop(StopReason::IllegalOpcode, m_s.last_opcode); }

    // The MAME-derived opcode handlers are included below the class body in
    // mame_interpreter.cpp. Their declarations are generated from the same
    // vendored MAME opcode source so no handler is silently omitted.
    void illegal(std::uint32_t op);
    void unimplemented(std::uint32_t op);
    void update_special(int dreg);
    void burn_cycle(int cycle);
    bool condition(int which);
    void int2float(tmsreg& srcdst);
    void float2int(tmsreg& srcdst, bool setflags);
    void negf(tmsreg& dst, tmsreg& src);
    void addf(tmsreg& dst, tmsreg& src1, tmsreg& src2);
    void subf(tmsreg& dst, tmsreg& src1, tmsreg& src2);
    void mpyf(tmsreg& dst, tmsreg& src1, tmsreg& src2);
    void norm(tmsreg& dst, tmsreg& src);

    // Generated handler declarations and dispatch tables.
    #include "mame_handler_declarations.inc"
};

} // namespace mamerecomp::tms320c31
