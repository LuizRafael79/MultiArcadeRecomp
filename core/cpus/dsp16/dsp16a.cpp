// license:BSD-3-Clause
// copyright-holders:Vas Crabb, MameRecomp contributors
// DSP16A interpreter semantics ported from MAME's BSD-3-Clause DSP16 core.

#include "dsp16a.hpp"
#include "dsp16a_dau_math.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mamerecomp::dsp16 {
namespace {
constexpr std::uint8_t flag_old = 0x02;
constexpr std::uint8_t sio_flags_none = 0;
constexpr std::uint16_t reverse_bits(std::uint16_t value)
{
    value = static_cast<std::uint16_t>(((value & 0x5555U) << 1) | ((value >> 1) & 0x5555U));
    value = static_cast<std::uint16_t>(((value & 0x3333U) << 2) | ((value >> 2) & 0x3333U));
    value = static_cast<std::uint16_t>(((value & 0x0f0fU) << 4) | ((value >> 4) & 0x0f0fU));
    return static_cast<std::uint16_t>((value << 8) | (value >> 8));
}
}

Dsp16A::Dsp16A(Dsp16ABus bus) : bus_(std::move(bus)) { reset(); }

void Dsp16A::set_firmware(std::span<const std::uint16_t> words)
{
    firmware_.fill(0);
    std::copy_n(words.begin(), std::min(words.size(), firmware_.size()), firmware_.begin());
    reset();
}

void Dsp16A::reset()
{
    state_ = {};
    phase_ = Phase::Purge;
    cache_mode_ = CacheMode::None;
    cache_.fill(0);
    cache_ptr_ = cache_limit_ = cache_iterations_ = 0;
    cache_pcbase_ = instruction_pc_ = 0;
    op_ = rom_data_ = 0;
    flags_ = 0;
    int_enable_.fill(0);
    irq_in_ = false;
    iack_out_ = true;
    sioc_ = sio_obuf_ = sio_osr_ = sio_ofsr_ = 0;
    sio_clk_ = sio_load_ = true;
    do_out_ = ose_out_ = ock_out_ = old_out_ = true;
    psel_out_ = false;
    sio_clk_div_ = sio_clk_res_ = sio_ld_div_ = 0;
    sio_flags_ = sio_flags_none;
    pioc_ = 0x0008;
    pdx_in_ = pdx_out_ = 0;
    pids_count_ = pods_count_ = 0;
    pids_out_ = pods_out_ = true;
    cycles_ = instructions_ = 0;
    half_clock_ = 0;
    do_cache_.fill(0);
    if (bus_.interrupt_ack) bus_.interrupt_ack(true);
    if (bus_.serial_clock_out) bus_.serial_clock_out(true);
    if (bus_.serial_data_out) bus_.serial_data_out(true);
    if (bus_.serial_load_out) bus_.serial_load_out(true);
    if (bus_.serial_enable_out) bus_.serial_enable_out(true);
}

void Dsp16A::set_irq(bool asserted) { irq_in_ = asserted; }

std::uint64_t Dsp16A::run_clocks(std::uint64_t clocks)
{
    // MAME models two DSP clocks per machine cycle. Carry an odd clock to the
    // next call so callers can synchronize against independent devices.
    const std::uint64_t total = clocks + half_clock_;
    const std::uint64_t cycles = total / 2;
    half_clock_ = static_cast<std::uint8_t>(total & 1U);
    return run_cycles(cycles) * 2;
}

std::uint64_t Dsp16A::run_cycles(std::uint64_t cycles)
{
    const auto start = cycles_;
    while (cycles_ - start < cycles) execute_cycle();
    return cycles_ - start;
}

std::uint16_t Dsp16A::read_program(std::uint16_t address)
{
    if (address < internal_program_words) return firmware_[address];
    return bus_.program_read ? bus_.program_read(address) : 0xffff;
}
std::uint16_t Dsp16A::read_data(std::uint16_t address)
{
    return bus_.data_read ? bus_.data_read(address) : data_ram_[address & 0x07ffU];
}
void Dsp16A::write_data(std::uint16_t address, std::uint16_t value)
{
    if (bus_.data_write) bus_.data_write(address, value);
    else data_ram_[address & 0x07ffU] = value;
}

std::uint16_t& Dsp16A::set_pc_offset(std::uint16_t offset)
{
    state_.pc = (state_.pc & 0xf000U) | (offset & 0x0fffU);
    if (iack_out_) state_.pi = state_.pc;
    return state_.pc;
}

std::uint16_t& Dsp16A::yaau_postmodify_r(std::uint16_t op)
{
    auto& r = state_.r[(op >> 2) & 3U];
    switch (op & 3U) {
    case 1: r = (state_.re && state_.re == r) ? state_.rb : std::uint16_t(r + 1); break;
    case 2: --r; break;
    case 3: r = std::uint16_t(r + state_.j); break;
    default: break;
    }
    return r;
}

std::int16_t Dsp16A::yaau_read(std::uint16_t op)
{
    const auto address = state_.r[(op >> 2) & 3U];
    const auto result = static_cast<std::int16_t>(read_data(address));
    yaau_postmodify_r(op);
    return result;
}
void Dsp16A::yaau_write(std::uint16_t op, std::int16_t value)
{
    write_data(state_.r[(op >> 2) & 3U], static_cast<std::uint16_t>(value));
    yaau_postmodify_r(op);
}
void Dsp16A::yaau_write_z(std::uint16_t op)
{
    auto& r = state_.r[(op >> 2) & 3U];
    write_data(r, static_cast<std::uint16_t>(state_.temp));
    switch (op & 3U) {
    case 0: r = (state_.re && state_.re == r) ? state_.rb : std::uint16_t(r + 1); break;
    case 2: r = std::uint16_t(r + 2); break;
    case 3: r = std::uint16_t(r + state_.k); break;
    default: break;
    }
}

void Dsp16A::yaau_short_immediate_load(std::uint16_t op)
{
    const unsigned r = (op >> 9) & 7U;
    const std::uint16_t m = op & 0x01ffU;
    switch (r) {
    case 0: state_.j = static_cast<std::int16_t>(m | ((m & 0x0100U) ? 0xfe00U : 0)); break;
    case 1: state_.k = static_cast<std::int16_t>(m | ((m & 0x0100U) ? 0xfe00U : 0)); break;
    case 2: state_.rb = m; break;
    case 3: state_.re = m; break;
    default: state_.r[r & 3U] = m; break;
    }
}

void Dsp16A::dau_set_y(std::uint16_t op, std::int16_t value)
{
    std::uint32_t bits = std::bit_cast<std::uint32_t>(state_.y);
    if (op_x(op)) {
        // Loading YH also clears YL unless AUC[6] explicitly preserves it.
        // DL-1425's sample loop uses AUC=0x02; stale YL would contaminate
        // subsequent full-width Y arithmetic. Match MAME's dau_set_y(s16).
        const std::uint32_t low = (state_.auc & (1U << 6)) ? (bits & 0xffffU) : 0U;
        bits = (std::uint32_t(std::uint16_t(value)) << 16) | low;
    } else {
        bits = (bits & 0xffff0000U) | std::uint16_t(value);
    }
    state_.y = std::bit_cast<std::int32_t>(bits);
}

std::int16_t Dsp16A::get_r(std::uint16_t op)
{
    switch (op_r(op)) {
    case 0x00: case 0x01: case 0x02: case 0x03: return static_cast<std::int16_t>(state_.r[op_r(op)]);
    case 0x04: return state_.j; case 0x05: return state_.k;
    case 0x06: return static_cast<std::int16_t>(state_.rb); case 0x07: return static_cast<std::int16_t>(state_.re);
    case 0x08: return static_cast<std::int16_t>(state_.pt); case 0x09: return static_cast<std::int16_t>(state_.pr);
    case 0x0a: return static_cast<std::int16_t>(state_.pi); case 0x0b: return state_.i;
    case 0x10: return state_.x; case 0x11: return static_cast<std::int16_t>(state_.y >> 16);
    case 0x12: return static_cast<std::int16_t>(state_.y); case 0x13: return state_.auc;
    case 0x14: return static_cast<std::int16_t>((state_.psw & 0xfe10U) | ((state_.a[0] >> 32) & 0x000fU) | ((state_.a[1] >> 27) & 0x01e0U));
    case 0x15: case 0x16: case 0x17: return state_.c[op_r(op) - 0x15];
    case 0x18: return sioc_; case 0x1c: return pioc_;
    case 0x1d: return static_cast<std::int16_t>(pio_pdx_read((op_r(op) >> 1) & 1U));
    case 0x1e: return static_cast<std::int16_t>(pio_pdx_read((op_r(op) >> 1) & 1U));
    case 0x19: case 0x1a: case 0x1b: return 0;
    default: throw std::runtime_error("DSP16A invalid R register");
    }
}

void Dsp16A::set_r(std::uint16_t op, std::int16_t value)
{
    switch (op_r(op)) {
    case 0x00: case 0x01: case 0x02: case 0x03: state_.r[op_r(op)] = static_cast<std::uint16_t>(value); break;
    case 0x04: state_.j = value; break; case 0x05: state_.k = value; break;
    case 0x06: state_.rb = static_cast<std::uint16_t>(value); break; case 0x07: state_.re = static_cast<std::uint16_t>(value); break;
    case 0x08: state_.pt = static_cast<std::uint16_t>(value); break; case 0x09: state_.pr = static_cast<std::uint16_t>(value); break;
    case 0x0a: if (!iack_out_) state_.pi = static_cast<std::uint16_t>(value); break;
    case 0x0b: state_.i = static_cast<std::int16_t>((value & 0x0fff) | ((value & 0x0800) ? 0xf000 : 0)); break;
    case 0x10: state_.x = value; break;
    case 0x11: {
        const std::uint32_t low = (state_.auc & 0x40U) ? std::uint16_t(state_.y) : 0U;
        state_.y = std::bit_cast<std::int32_t>((std::uint32_t(std::uint16_t(value)) << 16) | low);
        break;
    }
    case 0x12: dau_set_y(0, value); break;
    case 0x13: state_.auc = std::uint8_t(value & 0x7f); break;
    case 0x14: state_.psw = std::uint16_t(value); state_.a[0] = (std::uint64_t(std::uint32_t(state_.a[0])) | (std::uint64_t(state_.psw & 0x000fU) << 32) | ((state_.psw & 8U) ? ~dau_a_mask_ : 0)); state_.a[1] = (std::uint64_t(std::uint32_t(state_.a[1])) | (std::uint64_t(state_.psw & 0x01e0U) << 27) | ((state_.psw & 0x0100U) ? ~dau_a_mask_ : 0)); break;
    case 0x15: case 0x16: case 0x17: state_.c[op_r(op) - 0x15] = static_cast<std::int8_t>(value); break;
    case 0x18: sio_sioc_write(static_cast<std::uint16_t>(value)); break;
    case 0x1a:
        sio_obuf_ = static_cast<std::uint16_t>(value);
        ++sdx_writes_;
        if (sio_obuf_) ++nonzero_sdx_writes_;
        last_sdx_pc_ = instruction_pc_;
        last_sdx_opcode_ = op_;
        last_sdx_a0_ = state_.a[0];
        last_sdx_a1_ = state_.a[1];
        last_sdx_x_ = state_.x;
        last_sdx_y_ = state_.y;
        last_sdx_p_ = state_.p;
        pioc_ &= ~(1U << 3);
        break;
    case 0x1c: pio_pioc_write(static_cast<std::uint16_t>(value)); break;
    case 0x1d: pio_pdx_write((op_r(op) >> 1) & 1U, static_cast<std::uint16_t>(value)); break;
    case 0x1e: pio_pdx_write((op_r(op) >> 1) & 1U, static_cast<std::uint16_t>(value)); break;
    default: break;
    }
}

std::int64_t Dsp16A::dau_get_p_aligned() const
{
    return detail::align_product(state_.p, state_.auc);
}
std::int64_t Dsp16A::dau_set_psw_flags(std::int64_t value)
{
    state_.psw &= 0x0fffU;
    const bool negative = (std::uint64_t(value) & dau_a_sign_) != 0;
    if (negative) state_.psw |= 0x8000U;
    if (!(std::uint64_t(value) & dau_a_mask_)) state_.psw |= 0x4000U;
    if ((value >> 36) != (negative ? -1 : 0)) state_.psw |= 0x2000U;
    if (((value >> 32) & 0xf) != (((value >> 31) & 1) ? 15 : 0)) state_.psw |= 0x1000U;
    return negative ? (value | static_cast<std::int64_t>(dau_a_ext_)) : (value & static_cast<std::int64_t>(dau_a_mask_));
}
std::int64_t Dsp16A::dau_saturate(unsigned a) const
{
    if (state_.auc & (1U << (2 + a))) return state_.a[a];
    return std::clamp<std::int64_t>(state_.a[a], std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
}
std::int64_t& Dsp16A::dau_set_atx(std::uint16_t op, std::int16_t value)
{
    auto& a = state_.a[op_d(~op)];
    if (op_x(op)) {
        const bool preserve_low = state_.auc & (1U << (4 + op_d(~op)));
        a = detail::load_accumulator_high(a, value, preserve_low);
    } else a = (a & ~std::int64_t(0xffff)) | std::uint16_t(value);
    return a;
}
std::int64_t& Dsp16A::dau_set_at(std::uint16_t op, std::int16_t value)
{
    auto& a = state_.a[op_d(~op)];
    const bool preserve_low = state_.auc & (1U << (4 + op_d(~op)));
    a = detail::load_accumulator_high(a, value, preserve_low);
    return a;
}

std::int64_t Dsp16A::dau_f1(std::uint16_t op)
{
    const auto& s = state_.a[op_s(op)];
    std::int64_t d = 0;
    switch (op_f1(op)) {
    case 0: d = dau_get_p_aligned(); state_.p = std::int32_t(state_.x * (state_.y >> 16)); break;
    case 1: d = s + dau_get_p_aligned(); state_.p = std::int32_t(state_.x * (state_.y >> 16)); break;
    case 2: state_.p = std::int32_t(state_.x * (state_.y >> 16)); return state_.a[op_d(op)];
    case 3: d = s - dau_get_p_aligned(); state_.p = std::int32_t(state_.x * (state_.y >> 16)); break;
    case 4: d = dau_get_p_aligned(); break; case 5: d = s + dau_get_p_aligned(); break;
    case 6: return state_.a[op_d(op)]; case 7: d = s - dau_get_p_aligned(); break;
    case 8: d = s | state_.y; break; case 9: d = s ^ state_.y; break;
    case 10: dau_set_psw_flags(s & state_.y); return state_.a[op_d(op)];
    case 11: dau_set_psw_flags(s - state_.y); return state_.a[op_d(op)];
    case 12: d = state_.y; break; case 13: d = s + state_.y; break;
    case 14: d = s & state_.y; break; case 15: d = s - state_.y; break;
    }
    return dau_set_psw_flags(d);
}
void Dsp16A::dau_f2(std::uint16_t op)
{
    const auto s = state_.a[op_s(op)];
    auto& d = state_.a[op_d(op)];
    switch (op_f2(op)) {
    case 0: d = s >> 1; break; case 1: d = std::int32_t(std::uint32_t(s) << 1); break;
    case 2: d = s >> 4; break; case 3: d = std::int32_t(std::uint32_t(s) << 4); break;
    case 4: d = s >> 8; break; case 5: d = std::int32_t(std::uint32_t(s) << 8); break;
    case 6: d = s >> 16; break; case 7: d = std::int32_t(std::uint32_t(s) << 16); break;
    case 8: d = dau_get_p_aligned(); break;
    case 9: d = std::int64_t(std::int32_t(std::uint32_t(s) & ~0xffffU)) + (std::int32_t(1) << 16); if (state_.auc & (1U << (op_s(op) + 4))) d |= state_.a[op_d(op)] & 0xffff; break;
    case 10: throw std::runtime_error("DSP16A reserved F2 opcode");
    case 11: d = detail::round_accumulator_f2_11(s); break;
    case 12: d = state_.y; break; case 13: d = s + 1; break; case 14: d = s; break; case 15: d = -s; break;
    }
    d = dau_set_psw_flags(d);
}

bool Dsp16A::op_dau_con(std::uint16_t op, bool increment)
{
    const unsigned con = op_con(op);
    bool result = false;
    switch (con >> 1) {
    case 0: result = bool(state_.psw & 0x8000U); break;
    case 1: result = bool(state_.psw & 0x4000U); break;
    case 2: result = bool(state_.psw & 0x2000U); break;
    case 3: result = bool(state_.psw & 0x1000U); break;
    case 4: throw std::runtime_error("DSP16A random condition unsupported");
    case 5: case 6: { auto& c = state_.c[(con >> 1) - 5]; result = c >= 0; if (increment) ++c; break; }
    case 7: result = true; break;
    case 8: result = !(state_.psw & 0x8000U) && !(state_.psw & 0x4000U); break;
    default: throw std::runtime_error("DSP16A reserved condition");
    }
    return (con & 1U) ? !result : result;
}

bool Dsp16A::check_predicate()
{
    const bool result = (flags_ & pred_mask_) != pred_false_;
    set_predicate(pred_none_);
    return result;
}
void Dsp16A::set_predicate(std::uint8_t value) { flags_ = (flags_ & ~pred_mask_) | (value & pred_mask_); }
void Dsp16A::set_iack(std::uint8_t value) { flags_ = (flags_ & ~iack_mask_) | (value & iack_mask_); }

void Dsp16A::sio_sioc_write(std::uint16_t value)
{
    const bool old_ild = (sioc_ & 0x10U), old_ock = (sioc_ & 0x08U), old_ick = (sioc_ & 0x04U), old_old = (sioc_ & 0x20U);
    sioc_ = value & 0x03ffU;
    static constexpr unsigned preload[4] = {0, 2, 3, 4};
    sio_clk_res_ = preload[(sioc_ >> 7) & 3U];
    if (bus_.serial_load_in_enable && old_ild != bool(sioc_ & 0x10U) && !sio_load_) bus_.serial_load_in_enable(!(sioc_ & 0x10U));
    if (bus_.serial_load_out && old_old != bool(sioc_ & 0x20U) && !sio_load_) bus_.serial_load_out(!(sioc_ & 0x20U));
    if (bus_.serial_clock_in_enable && old_ick != bool(sioc_ & 0x04U) && !sio_clk_) bus_.serial_clock_in_enable(!(sioc_ & 0x04U));
    if (bus_.serial_clock_out && old_ock != bool(sioc_ & 0x08U) && !sio_clk_) bus_.serial_clock_out(!(sioc_ & 0x08U));
}
void Dsp16A::sio_ock_active_edge()
{
    const bool old = (sioc_ & 0x20U) ? !sio_load_ : false;
    if (old) {
        if (!(sio_flags_ & flag_old) && !sio_ofsr_ && !(pioc_ & 0x0008U)) {
            const bool eight = bool(sioc_ & 2U);
            last_serial_word_loaded_ = sio_obuf_;
            sio_osr_ = (sioc_ & 0x40U) ? reverse_bits(sio_obuf_) : sio_obuf_;
            sio_ofsr_ = 0xffffU;
            if (eight) { sio_osr_ |= 0xff00U; sio_ofsr_ >>= 8; }
            pioc_ |= 0x0008U;
            ++serial_word_loads_;
            if (sio_obuf_) ++nonzero_serial_word_loads_;
        }
        sio_flags_ |= flag_old;
    } else sio_flags_ &= ~flag_old;
    const bool bit = bool(sio_osr_ & 1U), enable = bool((~sio_ofsr_) & 1U);
    if (bit != do_out_) {
        do_out_ = bit;
        ++do_transitions_;
        if (bus_.serial_data_out) bus_.serial_data_out(do_out_);
    }
    if (enable != ose_out_) {
        ose_out_ = enable;
        ++ose_transitions_;
        if (bus_.serial_enable_out) bus_.serial_enable_out(ose_out_);
    }
    sio_osr_ = (sio_osr_ >> 1) | 0x8000U;
    sio_ofsr_ >>= 1;
    if (sioc_ & 0x0200U) sio_step_ld_div();
}
void Dsp16A::sio_step_ld_div()
{
    if (sio_ld_div_) --sio_ld_div_;
    else {
        sio_load_ = !sio_load_;
        sio_ld_div_ = 7;
        if ((sioc_ & 0x10U) && bus_.serial_load_in_enable)
            bus_.serial_load_in_enable(sio_load_);
        if ((sioc_ & 0x20U) && bus_.serial_load_out)
            bus_.serial_load_out(sio_load_);
        if (old_out_ != sio_load_) {
            old_out_ = sio_load_;
            ++old_transitions_;
        }
    }
}
void Dsp16A::serial_step()
{
    if (sio_clk_div_) --sio_clk_div_;
    else {
        const bool active = !sio_clk_;
        sio_clk_ = active;
        if (sioc_ & 0x04U) { if (active) {} if (bus_.serial_clock_in_enable) bus_.serial_clock_in_enable(sio_clk_); }
        if (sioc_ & 0x08U) {
            if (bus_.serial_clock_out) bus_.serial_clock_out(sio_clk_);
            if (active) {
                ++ock_rising_edges_;
                sio_ock_active_edge();
            }
        }
        sio_clk_div_ = sio_clk_res_;
    }
}
void Dsp16A::pio_step()
{
    if (pids_count_ && !--pids_count_) {
        if (bus_.pio_read) pdx_in_ = bus_.pio_read(psel_out_ ? 1U : 0U);
        pids_out_ = true;
        if (bus_.synchronize) bus_.synchronize();
    }
    if (pods_count_ && !--pods_count_) {
        if (bus_.pio_write) bus_.pio_write(psel_out_ ? 1U : 0U, pdx_out_);
        pods_out_ = true;
    }
}

std::uint16_t Dsp16A::pio_pdx_read(unsigned select)
{
    const bool active = (pioc_ & (1U << 11)) != 0;
    pioc_ &= ~(1U << 2);
    psel_out_ = select != 0;
    if (active && !pids_count_) {
        pids_out_ = false;
        pids_count_ = static_cast<std::uint8_t>(((pioc_ >> 13) & 3U) + 2U);
    }
    return pdx_in_;
}

void Dsp16A::pio_pdx_write(unsigned select, std::uint16_t value)
{
    const bool active = (pioc_ & (1U << 12)) != 0;
    pioc_ &= ~(1U << 1);
    pdx_out_ = value;
    psel_out_ = select != 0;
    if (active && !pods_count_) {
        pods_out_ = false;
        pods_count_ = static_cast<std::uint8_t>(((pioc_ >> 13) & 3U) + 2U);
    }
}

void Dsp16A::pio_pioc_write(std::uint16_t value)
{
    pioc_ = (pioc_ & 0x801fU) | (value & 0x7fe0U);
    int_enable_[1] = (value >> 5) & 0x001fU;
    if (!(pioc_ & (1U << 11))) { pids_count_ = 0; pids_out_ = true; }
    if (!(pioc_ & (1U << 12))) { pods_count_ = 0; pods_out_ = true; }
}

bool Dsp16A::op_interruptible(std::uint16_t op)
{
    switch (op >> 11) {
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
    case 0x0f: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
    case 0x17: case 0x19: case 0x1b: case 0x1c: case 0x1d: case 0x1f:
        return true;
    default:
        return false;
    }
}

void Dsp16A::execute_cycle()
{
    if (cache_mode_ == CacheMode::Execute) execute_cache_cycle();
    else execute_rom_cycle();
    serial_step();
    pio_step();
    ++cycles_;
}

bool Dsp16A::execute_rom_cycle()
{
    if (phase_ == Phase::Purge) {
        instruction_pc_ = state_.pc;
        op_ = read_program(state_.pc);
        phase_ = Phase::Op1;
        return true;
    }
    if (phase_ == Phase::Op2) {
        // Complete the actual second half of the instruction before the next
        // program fetch. MAME performs these transfers in OP2, with peripheral
        // stepping at the end of this same machine cycle.
        complete_second_phase();
        if (cache_mode_ == CacheMode::Load) {
            advance_cache_load();
            return true;
        }
        if (phase_ == Phase::Purge) return true;
        instruction_pc_ = state_.pc;
        op_ = read_program(state_.pc);
        phase_ = Phase::Op1;
        return true;
    }

    const auto current_op = op_;
    instruction_pc_ = state_.pc;

    switch (flags_ & iack_mask_) {
    case iack_set_:
        if (iack_out_) {
            iack_out_ = false;
            if (bus_.interrupt_ack) bus_.interrupt_ack(false);
        }
        break;
    case iack_clear_:
        if (!iack_out_) {
            iack_out_ = true;
            if (bus_.interrupt_ack) bus_.interrupt_ack(true);
            pioc_ &= ~1U;
        }
        break;
    default: break;
    }
    set_iack(iack_none_);

    if (iack_out_ && op_interruptible(current_op) &&
        ((pioc_ & int_enable_[0] & 0x001eU) || ((int_enable_[0] & 1U) && irq_in_))) {
        set_iack(iack_set_);
        int_enable_[0] = int_enable_[1];
        state_.pc = 1;
        phase_ = Phase::Purge;
        return true;
    }
    int_enable_[0] = int_enable_[1];
    set_pc_offset(state_.pc + 1U);
    ++instructions_;
    const unsigned cost = execute_instruction(current_op, false);
    if ((current_op >> 11) == 0x0eU && cache_mode_ == CacheMode::Load) {
        // The DO instruction prefetches the first body word into cache[1].
        op_ = read_program(state_.pc);
        do_cache_[1] = op_;
        phase_ = Phase::Op1;
        return true;
    }
    if ((current_op >> 11) == 0x0eU && phase_ == Phase::Prefetch) return true;
    if (cache_mode_ == CacheMode::Load && cost == 1) {
        advance_cache_load();
        return true;
    }
    if (phase_ != Phase::Purge) {
        if (cost > 1) phase_ = Phase::Op2;
        else {
            instruction_pc_ = state_.pc;
            op_ = read_program(state_.pc);
            phase_ = Phase::Op1;
        }
    }
    return true;
}

bool Dsp16A::execute_cache_cycle()
{
    if (phase_ == Phase::Prefetch) {
        phase_ = Phase::Op1;
        overlap_cache_rom_read();
        return true;
    }
    if (phase_ == Phase::Op2) {
        complete_second_phase();
        advance_cache_execution();
        return true;
    }
    op_ = do_cache_[cache_ptr_ & 15U];
    instruction_pc_ = static_cast<std::uint16_t>(cache_pcbase_ + cache_ptr_ - 1U);
    ++instructions_;
    const unsigned cost = execute_instruction(op_, true);
    if (cost > 1) phase_ = Phase::Op2;
    else advance_cache_execution();
    return true;
}

void Dsp16A::advance_cache_load()
{
    if (cache_ptr_ >= cache_limit_) {
        cache_mode_ = CacheMode::Execute;
        cache_ptr_ = 1;
        if (cache_iterations_) --cache_iterations_; // first iteration ran while loading
        phase_ = Phase::Prefetch;
    } else {
        ++cache_ptr_;
        op_ = read_program(state_.pc);
        do_cache_[cache_ptr_ & 15U] = op_;
        phase_ = Phase::Op1;
    }
}

void Dsp16A::advance_cache_execution()
{
    phase_ = Phase::Op1;
    if (cache_ptr_ == cache_limit_) {
        if (cache_iterations_ > 1) {
            --cache_iterations_;
            cache_ptr_ = 1;
            overlap_cache_rom_read();
        } else {
            cache_mode_ = CacheMode::None;
            op_ = read_program(state_.pc);
            phase_ = Phase::Op1;
        }
    } else {
        ++cache_ptr_;
        overlap_cache_rom_read();
    }
}

void Dsp16A::overlap_cache_rom_read()
{
    const unsigned group = do_cache_[cache_ptr_ & 15U] >> 11;
    if (group == 0x19 || group == 0x1b || group == 0x1f)
        rom_data_ = read_program(state_.pt);
}

void Dsp16A::complete_second_phase()
{
    switch (op_ >> 11) {
    case 0x04: case 0x1c:
        yaau_write(op_, static_cast<std::int16_t>(
                dau_saturate((~op_ >> 14) & 1U) >> (op_x(op_) ? 16 : 0)));
        state_.a[op_d(op_)] = dau_f1(op_);
        break;
    case 0x05: case 0x0d: case 0x15:
        yaau_write_z(op_);
        break;
    case 0x0a:
        rom_data_ = read_program(state_.pc);
        set_pc_offset(state_.pc + 1U);
        set_r(op_, static_cast<std::int16_t>(rom_data_));
        break;
    case 0x0c:
        yaau_write(op_, get_r(op_));
        break;
    case 0x14:
        yaau_write(op_, static_cast<std::int16_t>(op_x(op_) ? state_.y >> 16 : state_.y));
        break;
    case 0x19: case 0x1b: case 0x1f:
        state_.pt = static_cast<std::uint16_t>(state_.pt + (op_x(op_) ? state_.i : 1));
        state_.x = static_cast<std::int16_t>(rom_data_);
        break;
    case 0x1a:
        if (check_predicate()) { set_iack(iack_set_); state_.pc = 2; }
        phase_ = Phase::Purge;
        break;
    case 0x1d:
        state_.pt = static_cast<std::uint16_t>(state_.pt + (op_x(op_) ? state_.i : 1));
        state_.x = static_cast<std::int16_t>(rom_data_);
        yaau_write_z(op_);
        break;
    default:
        break;
    }
}

unsigned Dsp16A::execute_instruction(std::uint16_t op, bool from_cache)
{
    if (bus_.instruction_trace)
        bus_.instruction_trace(Dsp16ATracePhase::Before, instruction_pc_, op, state_);
    const unsigned group = op >> 11;
    unsigned cost = 1;
    switch (group) {
    case 0x00: case 0x01: case 0x10: case 0x11:
        if (check_predicate()) {
            if (op & 0x8000U) state_.pr = state_.pc;
            set_pc_offset(op_ja(op));
        }
        phase_ = Phase::Purge;
        break;
    case 0x02: case 0x03: yaau_short_immediate_load(op); break;
    case 0x04: case 0x1c: {
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x05: {
        const auto d = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(dau_saturate(op_d(~op)) >> (op_x(op) ? 16 : 0));
        state_.a[op_d(op)] = d;
        dau_set_atx(op, yaau_read(op));
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x06:
        state_.a[op_d(op)] = dau_f1(op);
        (void)yaau_read(op);
        break;
    case 0x07:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_atx(op, yaau_read(op));
        break;
    case 0x08:
        dau_set_at(op, get_r(op));
        cost = 2;
        break;
    case 0x09: case 0x0b:
        set_r(op, static_cast<std::int16_t>(dau_saturate((op >> 12) & 1U) >> 16));
        cost = 2;
        break;
    case 0x0a:
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x0c:
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x0d: {
        const auto old = get_r(op);
        state_.temp = old;
        const auto value = yaau_read(op);
        set_r(op, value);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x0e: {
        const unsigned ni = op_ni(op);
        const unsigned iterations = op_k(op);
        if (ni) {
            cache_pcbase_ = state_.pc;
            cache_limit_ = static_cast<std::uint16_t>(ni);
            cache_iterations_ = static_cast<std::uint16_t>(iterations);
            cache_ptr_ = 1;
            cache_mode_ = CacheMode::Load;
        } else {
            // MAME accepts the hardware's REDO form even before an explicit
            // DO load. The cache contents are then whatever the DSP cache
            // currently contains (zeroed here on reset), not an illegal-op
            // condition. Do not reject valid firmware at reset.
            cache_ptr_ = 1;
            cache_iterations_ = static_cast<std::uint16_t>(iterations);
            cache_mode_ = CacheMode::Execute;
            phase_ = Phase::Prefetch;
        }
        break;
    }
    case 0x0f: set_r(op, yaau_read(op)); cost = 2; break;
    case 0x12: {
        const bool con = op_dau_con(op, false);
        ++state_.c[1];
        if (con) { dau_f2(op); state_.c[2] = state_.c[1]; }
        break;
    }
    case 0x13: if (op_dau_con(op, true)) dau_f2(op); break;
    case 0x14:
        state_.a[op_d(op)] = dau_f1(op);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x15:
        state_.a[op_d(op)] = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(op_x(op) ? state_.y >> 16 : state_.y);
        dau_set_y(op, yaau_read(op));
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x16:
        state_.a[op_d(op)] = dau_f1(op);
        state_.x = yaau_read(op);
        break;
    case 0x17:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_y(op, yaau_read(op));
        break;
    case 0x18:
        switch (op_b(op)) {
        case 0: if (check_predicate()) state_.pc = state_.pr; break;
        case 1: set_iack(iack_clear_); state_.pc = state_.pi; break;
        case 2: if (check_predicate()) state_.pc = state_.pt; break;
        case 3: if (check_predicate()) { state_.pr = state_.pc; state_.pc = state_.pt; } break;
        default: throw std::runtime_error("DSP16A reserved branch operation");
        }
        if (iack_out_) state_.pi = state_.pc;
        if (!from_cache) phase_ = Phase::Purge;
        break;
    case 0x19: case 0x1b: {
        const unsigned src = group == 0x19 ? 0U : 1U;
        state_.a[op_d(op)] = dau_f1(op);
        state_.y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(state_.a[src]));
        if (from_cache && !(cache_ptr_ == cache_limit_ && cache_iterations_ == 1)) {
            state_.pt = static_cast<std::uint16_t>(state_.pt + (op_x(op) ? state_.i : 1));
            state_.x = static_cast<std::int16_t>(rom_data_);
        } else {
            rom_data_ = read_program(state_.pt);
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    }
    case 0x1a: {
        const bool condition = op_dau_con(op, true);
        set_predicate(condition ? pred_true_ : pred_false_);
        if (op_d(op)) {
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    }
    case 0x1d:
        state_.a[op_d(op)] = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(state_.y >> 16);
        dau_set_y(op, yaau_read(op));
        rom_data_ = read_program(state_.pt);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x1e: throw std::runtime_error("DSP16A reserved opcode group");
    case 0x1f:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_y(op, yaau_read(op));
        if (from_cache && !(cache_ptr_ == cache_limit_ && cache_iterations_ == 1)) {
            state_.pt = static_cast<std::uint16_t>(state_.pt + (op_x(op) ? state_.i : 1));
            state_.x = static_cast<std::int16_t>(rom_data_);
        } else {
            rom_data_ = read_program(state_.pt);
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    default: throw std::runtime_error("DSP16A invalid opcode group");
    }
    if (flags_ & pred_mask_) {
        // Predicates are consumed by branch/conditional instructions only.
        if (group != 0x1a) set_predicate(pred_none_);
    }
    if (from_cache && (phase_ == Phase::Purge)) phase_ = Phase::Op1;
    if (bus_.instruction_trace)
        bus_.instruction_trace(Dsp16ATracePhase::After, instruction_pc_, op, state_);
    return cost;
}

} // namespace mamerecomp::dsp16
