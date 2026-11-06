// license:BSD-3-Clause
// copyright-holders:Vas Crabb, MameRecomp contributors
// DSP16A architecture and execution semantics ported from MAME's BSD-3-Clause
// WE/AT&T DSP16 series emulator by Vas Crabb.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace mamerecomp::dsp16 {

struct Dsp16AState;
enum class Dsp16ATracePhase : std::uint8_t { Before, After };

struct Dsp16ABus {
    // Program reads below 0x1000 normally come from the DSP's internal ROM;
    // external program/sample reads are delegated to the board device.
    std::function<std::uint16_t(std::uint16_t)> program_read;
    std::function<std::uint16_t(std::uint16_t)> data_read;
    std::function<void(std::uint16_t, std::uint16_t)> data_write;

    // Passive PIO peripheral contract. The selector is PSEL, words are PDX.
    std::function<std::uint16_t(std::uint16_t)> pio_read;
    std::function<void(std::uint16_t, std::uint16_t)> pio_write;
    // Scheduler barrier after a completed active PIO read. Return true to
    // yield the DSP run slice so peer devices can run before the next cycle.
    std::function<bool()> synchronize;

    // DSP serial pins. Callbacks are emitted on signal transitions.
    std::function<void(bool)> serial_clock_out; // OCK
    std::function<void(bool)> serial_data_out;  // DO
    std::function<void(bool)> serial_load_out;  // OLD
    std::function<void(bool)> serial_enable_out;// OSE
    std::function<void(bool)> serial_clock_in_enable; // ICK enable/output
    std::function<void(bool)> serial_load_in_enable;  // ILD enable/output

    // Interrupt acknowledge output; INT input is set with set_irq().
    std::function<void(bool)> interrupt_ack;

    // Optional instruction-boundary observer for deterministic core-vs-core
    // traces. Leave unset during normal emulation.
    std::function<void(Dsp16ATracePhase, std::uint16_t, std::uint16_t,
                       const Dsp16AState&)> instruction_trace;
};

struct Dsp16AState {
    std::uint16_t pc = 0, pt = 0, pr = 0, pi = 0;
    std::int16_t i = 0;
    std::array<std::uint16_t, 4> r{};
    std::uint16_t rb = 0, re = 0;
    std::int16_t j = 0, k = 0;
    std::int16_t x = 0;
    std::int32_t y = 0, p = 0;
    std::array<std::int64_t, 2> a{};
    std::array<std::int8_t, 3> c{};
    std::uint8_t auc = 0;
    std::uint16_t psw = 0;
    std::int16_t temp = 0;
};

class Dsp16A {
public:
    static constexpr std::size_t internal_program_words = 0x1000;
    // DSP16A's 2K-word internal data RAM is mirrored over the 16-bit bus in
    // MAME's address map. A bus callback can override it for board mappings.
    static constexpr std::size_t data_words = 0x0800;

    explicit Dsp16A(Dsp16ABus bus = {});

    void reset();
    void set_firmware(std::span<const std::uint16_t> words);
    void set_irq(bool asserted);

    // DSP16A machine cycles are two input clocks. run_cycles executes no more
    // than the requested count, except that an instruction's second phase is
    // completed atomically when the caller splits a two-cycle instruction.
    std::uint64_t run_cycles(std::uint64_t cycles);
    std::uint64_t run_clocks(std::uint64_t clocks);

    const Dsp16AState& state() const { return state_; }
    std::uint16_t pc() const { return state_.pc; }
    std::uint64_t cycles() const { return cycles_; }
    std::uint64_t instructions() const { return instructions_; }
    std::uint64_t ock_rising_edges() const { return ock_rising_edges_; }
    std::uint64_t do_transitions() const { return do_transitions_; }
    std::uint64_t ose_transitions() const { return ose_transitions_; }
    std::uint64_t old_transitions() const { return old_transitions_; }
    std::uint64_t serial_word_loads() const { return serial_word_loads_; }
    std::uint64_t sdx_writes() const { return sdx_writes_; }
    std::uint64_t nonzero_sdx_writes() const { return nonzero_sdx_writes_; }
    std::uint64_t nonzero_serial_word_loads() const { return nonzero_serial_word_loads_; }
    std::uint16_t last_sdx_value() const { return sio_obuf_; }
    std::uint16_t last_serial_word_loaded() const { return last_serial_word_loaded_; }
    std::uint16_t last_sdx_pc() const { return last_sdx_pc_; }
    std::uint16_t last_sdx_opcode() const { return last_sdx_opcode_; }
    std::int64_t last_sdx_a0() const { return last_sdx_a0_; }
    std::int64_t last_sdx_a1() const { return last_sdx_a1_; }
    std::int16_t last_sdx_x() const { return last_sdx_x_; }
    std::int32_t last_sdx_y() const { return last_sdx_y_; }
    std::int32_t last_sdx_p() const { return last_sdx_p_; }
    bool irq_acknowledged() const { return !iack_out_; }
    bool data_output() const { return do_out_; }
    bool output_enable() const { return ose_out_; }
    // MAME's public old_r() pin exposes SIO LD directly when OLD is active.
    // The DSP uses the inverted level internally for its own OSR load logic.
    bool output_load() const { return (sioc_ & 0x20U) ? old_out_ : true; }
    bool output_clock() const { return (sioc_ & 0x08U) ? ock_out_ : true; }
    bool selected_output_channel() const { return psel_out_; }

    std::uint16_t data_word(std::uint16_t address) const { return data_ram_[address & 0x07ffU]; }
    void set_data_word(std::uint16_t address, std::uint16_t value) { data_ram_[address & 0x07ffU] = value; }

private:
    enum class Phase : std::uint8_t { Purge, Op1, Op2, Prefetch };
    enum class CacheMode : std::uint8_t { None, Load, Execute };

    std::uint16_t read_program(std::uint16_t address);
    std::uint16_t read_data(std::uint16_t address);
    void write_data(std::uint16_t address, std::uint16_t value);
    void execute_cycle();
    bool execute_rom_cycle();
    bool execute_cache_cycle();
    unsigned execute_instruction(std::uint16_t op, bool from_cache);
    void complete_second_phase();
    void advance_cache_load();
    void advance_cache_execution();
    void overlap_cache_rom_read();
    void serial_step();
    void pio_step();
    void overlap_rom_data_read();
    void yaau_short_immediate_load(std::uint16_t op);
    void dau_set_y(std::uint16_t op, std::int16_t value);
    std::int16_t yaau_read(std::uint16_t op);
    void yaau_write(std::uint16_t op, std::int16_t value);
    void yaau_write_z(std::uint16_t op);
    std::int16_t get_r(std::uint16_t op);
    void set_r(std::uint16_t op, std::int16_t value);
    std::uint16_t& set_pc_offset(std::uint16_t offset);
    std::int64_t dau_saturate(unsigned accumulator) const;
    bool op_dau_con(std::uint16_t op, bool increment);
    bool check_predicate();
    void set_predicate(std::uint8_t value);
    void set_iack(std::uint8_t value);
    std::int64_t dau_get_p_aligned() const;
    std::int64_t dau_set_psw_flags(std::int64_t value);
    std::int64_t dau_f1(std::uint16_t op);
    void dau_f2(std::uint16_t op);
    std::int64_t& dau_set_atx(std::uint16_t op, std::int16_t value);
    std::int64_t& dau_set_at(std::uint16_t op, std::int16_t value);
    std::uint16_t& yaau_postmodify_r(std::uint16_t op);
    void sio_sioc_write(std::uint16_t value);
    void sio_ock_active_edge();
    void sio_step_ld_div();
    std::uint16_t pio_pdx_read(unsigned select);
    void pio_pdx_write(unsigned select, std::uint16_t value);
    void pio_pioc_write(std::uint16_t value);
    static bool op_interruptible(std::uint16_t op);

    static constexpr std::uint16_t op_ja(std::uint16_t op) { return op & 0x0fffU; }
    static constexpr std::uint16_t op_b(std::uint16_t op) { return (op >> 8) & 7U; }
    static constexpr unsigned op_d(std::uint16_t op) { return (op >> 10) & 1U; }
    static constexpr unsigned op_s(std::uint16_t op) { return (op >> 9) & 1U; }
    static constexpr unsigned op_f1(std::uint16_t op) { return (op >> 5) & 15U; }
    static constexpr unsigned op_f2(std::uint16_t op) { return (op >> 5) & 15U; }
    static constexpr unsigned op_r(std::uint16_t op) { return (op >> 4) & 0x3fU; }
    static constexpr unsigned op_x(std::uint16_t op) { return (op >> 4) & 1U; }
    static constexpr unsigned op_con(std::uint16_t op) { return op & 0x1fU; }
    static constexpr unsigned op_ni(std::uint16_t op) { return (op >> 7) & 15U; }
    static constexpr unsigned op_k(std::uint16_t op) { return op & 0x7fU; }

    std::uint16_t op_ = 0;
    std::uint16_t rom_data_ = 0;
    std::array<std::uint16_t, 16> cache_{};
    std::array<std::uint16_t, internal_program_words> firmware_{};
    std::array<std::uint16_t, data_words> data_ram_{};
    Dsp16ABus bus_;
    Dsp16AState state_{};
    Phase phase_ = Phase::Purge;
    CacheMode cache_mode_ = CacheMode::None;
    std::uint16_t cache_ptr_ = 0, cache_limit_ = 0, cache_iterations_ = 0;
    std::uint16_t cache_pcbase_ = 0, instruction_pc_ = 0;
    std::uint8_t flags_ = 0;
    std::array<std::uint16_t, 2> int_enable_{};
    bool irq_in_ = false, iack_out_ = true;

    std::uint16_t sioc_ = 0, sio_obuf_ = 0, sio_osr_ = 0, sio_ofsr_ = 0;
    bool sio_clk_ = true, sio_load_ = true, do_out_ = true, ose_out_ = true;
    bool ock_out_ = true, old_out_ = true, psel_out_ = false;
    std::uint16_t sio_clk_div_ = 0, sio_clk_res_ = 0, sio_ld_div_ = 0;
    std::uint8_t sio_flags_ = 0;
    std::uint16_t pioc_ = 0x0008, pdx_in_ = 0, pdx_out_ = 0;
    std::uint8_t pids_count_ = 0, pods_count_ = 0;
    bool pids_out_ = true, pods_out_ = true;
    std::uint64_t cycles_ = 0, instructions_ = 0;
    std::uint64_t pending_clocks_ = 0;
    bool scheduler_yield_requested_ = false;
    std::uint64_t ock_rising_edges_ = 0, do_transitions_ = 0;
    std::uint64_t ose_transitions_ = 0, old_transitions_ = 0, serial_word_loads_ = 0;
    std::uint64_t sdx_writes_ = 0, nonzero_sdx_writes_ = 0, nonzero_serial_word_loads_ = 0;
    std::uint16_t last_serial_word_loaded_ = 0;
    std::uint16_t last_sdx_pc_ = 0, last_sdx_opcode_ = 0;
    std::int64_t last_sdx_a0_ = 0, last_sdx_a1_ = 0;
    std::int16_t last_sdx_x_ = 0;
    std::int32_t last_sdx_y_ = 0, last_sdx_p_ = 0;
    std::array<std::uint16_t, 16> do_cache_{};
    static constexpr std::uint16_t xaau_i_sign_ = 0x0800;
    static constexpr std::uint16_t xaau_i_mask_ = 0x0fff;
    static constexpr std::uint16_t xaau_i_ext_ = 0xf000;
    static constexpr std::uint64_t dau_a_sign_ = std::uint64_t(1) << 35;
    static constexpr std::uint64_t dau_a_mask_ = (std::uint64_t(1) << 36) - 1;
    static constexpr std::uint64_t dau_a_ext_ = ~dau_a_mask_;
    static constexpr std::uint8_t pred_none_ = 0x00, pred_true_ = 0x01,
        pred_false_ = 0x02, pred_mask_ = 0x03;
    static constexpr std::uint8_t iack_none_ = 0x00, iack_set_ = 0x04,
        iack_clear_ = 0x08, iack_mask_ = 0x0c;
};

} // namespace mamerecomp::dsp16
