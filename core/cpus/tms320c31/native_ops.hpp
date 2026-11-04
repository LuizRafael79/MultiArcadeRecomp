#pragma once

#include "tms320c31.hpp"

#include <bit>
#include <cstdint>

namespace mamerecomp::tms320c31::native_ops {

inline constexpr std::uint32_t cflag = 0x01u;
inline constexpr std::uint32_t vflag = 0x02u;
inline constexpr std::uint32_t zflag = 0x04u;
inline constexpr std::uint32_t nflag = 0x08u;
inline constexpr std::uint32_t ufflag = 0x10u;
inline constexpr std::uint32_t lvflag = 0x20u;
inline constexpr std::uint32_t lufflag = 0x40u;
inline constexpr std::uint32_t ovmflag = 0x80u;

inline void clear_float_flags(State& state) noexcept {
    state.r[21].i32[0] &= ~(nflag | zflag | vflag | ufflag);
}

inline void set_float_nz(State& state, const Register& value) noexcept {
    state.r[21].i32[0] |=
        (static_cast<std::uint32_t>(value.mantissa()) >> 28 & nflag) |
        (value.exponent() == -128 ? zflag : 0u);
}

inline void short_to_float(Register& out, std::uint16_t value) noexcept {
    const auto signed_value = static_cast<std::int16_t>(value);
    if (value == 0x8000u) {
        out.set_mantissa(0);
        out.set_exponent(-128);
        return;
    }
    out.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << 20));
    out.set_exponent(signed_value < 0
        ? static_cast<int>((static_cast<std::int32_t>(signed_value) - 0xfff) / 0x1000)
        : signed_value / 0x1000);
}

inline void int_to_float(State& state, Register& value) noexcept {
    std::uint32_t man = static_cast<std::uint32_t>(value.mantissa());
    int exp, cnt;
    clear_float_flags(state);
    if (man == 0) { man = 0x80000000u; exp = -128; }
    else if (man == 0xffffffffu) { man = 0; exp = -1; }
    else if (static_cast<std::int32_t>(man) > 0) {
        cnt = std::countl_zero(man); man <<= cnt; exp = 31 - cnt;
    } else {
        cnt = std::countl_one(man); man <<= cnt; exp = 31 - cnt;
    }
    value.set_mantissa(static_cast<std::int32_t>(man ^ 0x80000000u));
    value.set_exponent(exp);
    set_float_nz(state, value);
}

inline void float_to_int(State& state, Register& value, bool set_flags) noexcept {
    const auto man = value.mantissa();
    const int shift = 31 - value.exponent();
    if (set_flags) clear_float_flags(state);
    if (shift <= 0) {
        value.set_mantissa(man >= 0 ? 0x7fffffff : static_cast<std::int32_t>(0x80000000u));
        if (set_flags) state.r[21].i32[0] |= vflag | lvflag;
    } else if (shift > 31) {
        value.set_mantissa(man >> 31);
    } else {
        const auto shifted = static_cast<std::int32_t>(man) >> shift;
        value.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(shifted) ^
                                                     (1u << (31 - shift))));
    }
    if (set_flags) {
        const auto result = static_cast<std::uint32_t>(value.mantissa());
        state.r[21].i32[0] |= ((result >> 28) & nflag) | (result == 0 ? zflag : 0u);
    }
}

inline void absf(State& state, Register& dst, const Register& src) noexcept {
    const auto man = src.mantissa();
    const auto exp = src.exponent();
    clear_float_flags(state);
    dst = src;
    if (exp == -128) {
        dst.set_mantissa(0);
    } else if (man < 0) {
        if (man != INT32_MIN) {
            dst.set_mantissa(-man);
        } else if (exp == 127) {
            state.r[21].i32[0] |= vflag | lvflag;
            dst.set_mantissa(0x7fffffff);
        } else {
            dst.set_exponent(exp + 1);
            dst.set_mantissa(0);
        }
    }
    set_float_nz(state, dst);
}

inline void negf(State& state, Register& dst, const Register& src) noexcept {
    const auto man = src.mantissa();
    clear_float_flags(state);
    if (src.exponent() == -128) {
        dst.set_mantissa(0);
        dst.set_exponent(-128);
    } else if ((static_cast<std::uint32_t>(man) & 0x7fffffffu) != 0) {
        dst.set_mantissa(-man);
        dst.set_exponent(src.exponent());
    } else {
        dst.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(man) ^ 0x80000000u));
        dst.set_exponent(man == 0 ? src.exponent() - 1 : src.exponent() + 1);
    }
    set_float_nz(state, dst);
}

inline void addf(State& state, Register& dst, const Register& src1,
                 const Register& src2) noexcept {
    clear_float_flags(state);
    if (src1.exponent() == -128) {
        dst = src2;
        set_float_nz(state, dst);
        return;
    }
    if (src2.exponent() == -128) {
        dst = src1;
        set_float_nz(state, dst);
        return;
    }

    std::int64_t m1 = static_cast<std::int64_t>(src1.mantissa()) ^ 0x80000000ll;
    std::int64_t m2 = static_cast<std::int64_t>(src2.mantissa()) ^ 0x80000000ll;
    int exp, cnt;
    if (src1.exponent() > src2.exponent()) {
        exp = src1.exponent();
        cnt = exp - src2.exponent();
        if (cnt >= 32) { dst = src1; set_float_nz(state, dst); return; }
        m2 >>= cnt;
    } else {
        exp = src2.exponent();
        cnt = exp - src1.exponent();
        if (cnt >= 32) { dst = src2; set_float_nz(state, dst); return; }
        m1 >>= cnt;
    }

    auto man = m1 + m2;
    if (man == 0 || exp == -128) {
        exp = -128;
        man = 0x80000000ll;
    } else if (man >= 0x100000000ll || man < -0x100000000ll) {
        man >>= 1;
        ++exp;
    } else if (man < 0x80000000ll && man >= -0x80000000ll) {
        if (man > 0) {
            cnt = std::countl_zero(static_cast<std::uint32_t>(man));
            man = static_cast<std::int64_t>(static_cast<std::uint64_t>(man) << cnt);
            exp -= cnt;
        } else {
            cnt = std::countl_one(static_cast<std::uint32_t>(man));
            man = static_cast<std::int64_t>(static_cast<std::uint64_t>(man) << cnt);
            exp -= cnt;
        }
    }
    if (exp <= -128) {
        man = 0x80000000ll; exp = -128;
        state.r[21].i32[0] |= ufflag | lufflag;
    } else if (exp > 127) {
        man = man < 0 ? 0 : 0xffffffffll; exp = 127;
        state.r[21].i32[0] |= vflag | lvflag;
    }
    dst.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(man) ^ 0x80000000u));
    dst.set_exponent(exp);
    set_float_nz(state, dst);
}

inline void subf(State& state, Register& dst, const Register& src1,
                 const Register& src2) noexcept {
    clear_float_flags(state);
    if (src2.exponent() == -128) { dst = src1; set_float_nz(state, dst); return; }

    std::int64_t m1 = static_cast<std::int64_t>(src1.mantissa()) ^ 0x80000000ll;
    std::int64_t m2 = static_cast<std::int64_t>(src2.mantissa()) ^ 0x80000000ll;
    int exp, cnt;
    if (src1.exponent() > src2.exponent()) {
        exp = src1.exponent(); cnt = exp - src2.exponent();
        if (cnt >= 32) { dst = src1; set_float_nz(state, dst); return; }
        m2 >>= cnt;
    } else {
        exp = src2.exponent(); cnt = exp - src1.exponent();
        if (cnt >= 32) { negf(state, dst, src2); return; }
        m1 >>= cnt;
    }
    auto man = m1 - m2;
    if (man == 0 || exp == -128) {
        exp = -128; man = 0x80000000ll;
    } else if (man >= 0x100000000ll || man < -0x100000000ll) {
        man >>= 1; ++exp;
    } else if (man < 0x80000000ll && man >= -0x80000000ll) {
        if (man > 0) {
            cnt = std::countl_zero(static_cast<std::uint32_t>(man));
            man = static_cast<std::int64_t>(static_cast<std::uint64_t>(man) << cnt);
            exp -= cnt;
        } else {
            cnt = std::countl_one(static_cast<std::uint32_t>(man));
            man = static_cast<std::int64_t>(static_cast<std::uint64_t>(man) << cnt);
            exp -= cnt;
        }
    }
    if (exp <= -128) {
        if (man != 0 || exp < -128) state.r[21].i32[0] |= ufflag | lufflag;
        man = 0x80000000ll; exp = -128;
    } else if (exp > 127) {
        man = man < 0 ? 0 : 0xffffffffll; exp = 127;
        state.r[21].i32[0] |= vflag | lvflag;
    }
    dst.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(man) ^ 0x80000000u));
    dst.set_exponent(exp);
    set_float_nz(state, dst);
}

inline void mpyf(State& state, Register& dst, const Register& src1,
                 const Register& src2) noexcept {
    clear_float_flags(state);
    if (src1.exponent() == -128 || src2.exponent() == -128) {
        dst.set_mantissa(0); dst.set_exponent(-128); set_float_nz(state, dst); return;
    }
    const std::int32_t m1 = (src1.mantissa() >> 8) ^ 0x800000;
    const std::int32_t m2 = (src2.mantissa() >> 8) ^ 0x800000;
    std::int64_t man = static_cast<std::int64_t>(m1) * m2;
    int exp = src1.exponent() + src2.exponent();
    man >>= 15;
    if (man == 0) {
        exp = -128; man = 0x80000000ll;
    } else if (man >= 0x100000000ll) {
        man >>= 1; ++exp;
        if (man >= 0x100000000ll) { man >>= 1; ++exp; }
    } else if (man < -0x100000000ll) {
        man >>= 1; ++exp;
    }
    if (exp <= -128) {
        man = 0x80000000ll; exp = -128;
        state.r[21].i32[0] |= ufflag | lufflag;
    } else if (exp > 127) {
        man = man < 0 ? 0 : 0xffffffffll; exp = 127;
        state.r[21].i32[0] |= vflag | lvflag;
    }
    dst.set_mantissa(static_cast<std::int32_t>(static_cast<std::uint32_t>(man) ^ 0x80000000u));
    dst.set_exponent(exp);
    set_float_nz(state, dst);
}

} // namespace mamerecomp::tms320c31::native_ops
