#pragma once

#include <array>
#include <cstdint>

namespace multiarcaderecomp::adsp2105 {

// ADSP-2105 uses a 14-bit, word-addressed program PC and 24-bit instructions.
// Program and data spaces are separate; callbacks use word addresses, not byte
// addresses. DCS-2K's banked data ROM and host latches belong to the board bus.
struct CoreRegisters {
    std::array<std::int16_t, 2> ax{};
    std::array<std::int16_t, 2> ay{};
    std::array<std::int16_t, 2> mx{};
    std::array<std::int16_t, 2> my{};
    std::int16_t ar{};
    std::int16_t af{};
    std::int16_t mf{};
    std::int16_t si{};
    std::int8_t se{};
    std::int8_t sb{};
    std::array<std::uint16_t, 2> sr{};
    std::int64_t mr{}; // 40-bit MAC accumulator, sign-extended in storage.
};

struct State {
    std::uint16_t pc{};
    std::uint16_t previous_pc{};
    std::uint16_t loop{0xffff};
    std::uint16_t loop_condition{};
    std::uint32_t counter{};

    std::uint16_t astat{};
    std::uint16_t sstat{0x55};
    std::uint16_t mstat{};
    std::uint16_t imask{};
    std::uint8_t icntl{};
    std::uint16_t interrupt_flags{};
    std::uint16_t interrupt_clear{};
    std::uint8_t px{};
    std::uint8_t flag_in{};
    std::uint8_t flag_out{};
    std::array<std::uint8_t, 3> fl{};

    std::array<std::uint16_t, 8> i{};
    std::array<std::int16_t, 8> m{};
    std::array<std::uint16_t, 8> l{};
    std::array<std::uint16_t, 8> lmask{};
    std::array<std::uint16_t, 8> base{};
    std::array<std::uint16_t, 0x200> internal_data{};
    std::array<std::uint16_t, 0x20> control{};

    std::array<std::uint16_t, 16> pc_stack{};
    std::array<std::uint32_t, 4> counter_stack{};
    std::array<std::uint32_t, 4> loop_stack{};
    std::array<std::array<std::uint16_t, 3>, 4> status_stack{};
    std::uint8_t pc_stack_pointer{};
    std::uint8_t counter_stack_pointer{};
    std::uint8_t loop_stack_pointer{};
    std::uint8_t status_stack_pointer{};
    std::int32_t timer_scale_counter{};
    std::int32_t tx_index_register{};
    std::int32_t tx_modifier_register{};
    std::uint16_t tx_base{};
    std::uint16_t tx_size{};
    std::int32_t tx_period{320};
    std::int32_t tx_counter{};

    CoreRegisters core{};
    CoreRegisters alternate{};
    std::uint64_t cycles{};
    std::uint64_t native_instructions{};
    std::uint64_t interpreted_instructions{};
    bool idle{};
    bool halted{};
    bool tx_enabled{};
    std::array<bool, 6> irq{};
    std::array<bool, 6> irq_latched{};
};

struct Bus {
    void* opaque{};
    std::uint32_t (*read_program24)(void*, std::uint16_t word_address){};
    void (*write_program24)(void*, std::uint16_t word_address,
                            std::uint32_t value){};
    std::uint16_t (*read_data16)(void*, std::uint16_t word_address){};
    void (*write_data16)(void*, std::uint16_t word_address,
                         std::uint16_t value){};
    std::uint16_t (*read_io16)(void*, std::uint16_t word_address){};
    void (*write_io16)(void*, std::uint16_t word_address,
                       std::uint16_t value){};
    void (*sport_tx)(void*, unsigned port, std::uint32_t value){};
    std::uint32_t (*sport_rx)(void*, unsigned port){};
    void (*soft_reset)(void*){};
    bool (*is_mutable_program)(void*, std::uint16_t word_address){};
    bool core_internal_data_memory{};
    bool external_sport_timing{};
};

std::uint16_t data_read(State& state, Bus& bus, std::uint16_t address) noexcept;
void data_write(State& state, Bus& bus, std::uint16_t address,
                std::uint16_t value) noexcept;
void advance_clock(State& state, Bus& bus, std::uint64_t cycles = 1) noexcept;

enum class BlockExit : std::uint8_t {
    Dispatch,
    Budget,
    Idle,
    NeedsInterpreter,
    BusFault,
};

using Block = BlockExit (*)(State&, Bus&, std::uint64_t stop_cycle);
Block lookup(std::uint16_t pc) noexcept;
Block lookup(std::uint16_t pc, std::uint32_t opcode) noexcept;
std::uint16_t entry_pc() noexcept;

inline constexpr std::uint16_t pc_mask = 0x3fff;
inline constexpr std::uint32_t opcode_mask = 0x00ffffff;

} // namespace multiarcaderecomp::adsp2105
