// AICA DSP interpreter adapted from Flycast's
// core/third_party/flycast-aica/{dsp_interp.cpp,dsp.cpp,aica_mem.cpp}.
// The upstream Flycast source and GPL notice are retained in
// core/third_party/flycast-aica/.

#include "naomi_aica_dsp.hpp"

#include <algorithm>
#include <cstddef>

namespace mamerecomp::naomi {
namespace {

constexpr std::uint32_t kMproStart = 0x3400u;
constexpr std::uint32_t kMproEnd = 0x3c00u;
constexpr std::uint32_t kWaveRamMask = 0x007fffffu;

std::uint16_t read16(const std::array<std::uint8_t, 0x8000>& registers,
                     std::uint32_t offset) {
    offset &= 0x7fffu;
    return static_cast<std::uint16_t>(registers[offset] |
        (static_cast<std::uint16_t>(registers[(offset + 1u) & 0x7fffu]) << 8u));
}

std::uint16_t pack_dsp(std::int32_t value) {
    const int sign = (value >> 23) & 1;
    std::uint32_t temp = (static_cast<std::uint32_t>(value) ^
                          (static_cast<std::uint32_t>(value) << 1u)) & 0x00ffffffu;
    int exponent = 0;
    for (int bit = 0; bit < 12; ++bit) {
        if (temp & 0x00800000u) break;
        temp <<= 1u;
        ++exponent;
    }
    if (exponent < 12) value = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(value) << exponent);
    else value = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(value) << 11u);
    value = (value >> 11) & 0x7ff;
    value |= sign << 15;
    value |= exponent << 11;
    return static_cast<std::uint16_t>(value);
}

std::int32_t unpack_dsp(std::uint16_t value) {
    const int sign = (value >> 15) & 1;
    int exponent = (value >> 11) & 0xf;
    std::int32_t result = static_cast<std::int32_t>(value & 0x7ffu) << 11;
    result |= sign << 22;
    if (exponent > 11) exponent = 11;
    else result ^= 1 << 22;
    result |= sign << 23;
    result = static_cast<std::int32_t>(static_cast<std::uint32_t>(result) << 8u) >> 8;
    return result >> exponent;
}

std::int32_t wrap_shift_left(std::int32_t value, unsigned amount) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) << amount);
}

} // namespace

NaomiAicaDsp::NaomiAicaDsp() = default;

bool NaomiAicaDsp::ownsDataRegister(std::uint32_t offset) {
    offset &= 0x7fffu;
    return offset >= 0x4000u && offset < 0x4580u;
}

std::uint32_t NaomiAicaDsp::read32(
    const std::array<std::uint8_t, 0x8000>& registers,
    std::uint32_t offset) {
    offset &= 0x7fffu;
    return static_cast<std::uint32_t>(registers[offset]) |
        (static_cast<std::uint32_t>(registers[(offset + 1u) & 0x7fffu]) << 8u) |
        (static_cast<std::uint32_t>(registers[(offset + 2u) & 0x7fffu]) << 16u) |
        (static_cast<std::uint32_t>(registers[(offset + 3u) & 0x7fffu]) << 24u);
}

void NaomiAicaDsp::write32(std::array<std::uint8_t, 0x8000>& registers,
                           std::uint32_t offset, std::uint32_t value) {
    offset &= 0x7fffu;
    for (unsigned byte = 0; byte < 4; ++byte)
        registers[(offset + byte) & 0x7fffu] =
            static_cast<std::uint8_t>(value >> (byte * 8u));
}

std::uint32_t NaomiAicaDsp::readDataRegister(
    std::uint32_t offset, unsigned width,
    const std::array<std::uint8_t, 0x8000>& registers) const {
    offset &= 0x7fffu;
    if (!ownsDataRegister(offset) || (offset & 2u)) return 0;
    std::uint32_t value = 0;
    if (offset < 0x4400u)
        value = static_cast<std::uint32_t>(m_temp[(offset - 0x4000u) / 8u]);
    if (offset >= 0x4400u && offset < 0x4500u)
        value = static_cast<std::uint32_t>(m_mems[(offset - 0x4400u) / 8u]);
    else if (offset >= 0x4500u)
        value = static_cast<std::uint32_t>(m_mixs[(offset - 0x4500u) / 8u]);

    if (offset >= 0x4500u) {
        value = (offset & 4u) ? ((value >> 4u) & 0xffffu) : (value & 0xfu);
    } else {
        value = (offset & 4u) ? ((value >> 8u) & 0xffffu) : (value & 0xffu);
    }
    if (width == 1) {
        if (offset & 1u) value >>= 8u;
        else value &= 0xffu;
    }
    (void)registers;
    return value;
}

void NaomiAicaDsp::writeDataRegister(std::uint32_t offset, unsigned width,
                                    std::uint32_t value) {
    offset &= 0x7fffu;
    if (!ownsDataRegister(offset) || (offset & 2u)) return;
    const bool mixs = offset >= 0x4500u;
    const auto index = mixs ? (offset - 0x4500u) / 8u
                            : offset >= 0x4400u ? (offset - 0x4400u) / 8u
                                                : (offset - 0x4000u) / 8u;
    auto& target = mixs ? m_mixs[index]
                        : offset >= 0x4400u ? m_mems[index] : m_temp[index];
    const auto signed_value = static_cast<std::int32_t>(value);
    if (offset & 4u) {
        if (mixs) {
            if (width == 1) {
                if (offset & 1u)
                    target = (target & 0x00000fff) | ((signed_value << 24) >> 12);
                else
                    target = (target & 0xfffff00f) | ((value & 0xffu) << 4u);
            } else {
                target = (target & 0xf) | ((signed_value << 16) >> 12);
            }
        } else if (width == 1) {
            if (offset & 1u)
                target = (target & 0x0000ffff) | ((signed_value << 24) >> 8);
            else
                target = (target & 0xffff00ff) | ((value & 0xffu) << 8u);
        } else {
            target = (target & 0xff) | ((signed_value << 16) >> 8);
        }
    } else if (mixs) {
        if (width != 1 || (offset & 1u) == 0)
            target = (target & ~0xf) | static_cast<std::int32_t>(value & 0xfu);
    } else if (width != 1 || (offset & 1u) == 0) {
        target = (target & ~0xff) | static_cast<std::int32_t>(value & 0xffu);
    }
}

void NaomiAicaDsp::noteRegisterWrite(std::uint32_t offset, unsigned width) {
    const auto end = offset + width;
    if (offset < kMproEnd && end > kMproStart) m_program_dirty = true;
}

void NaomiAicaDsp::setVoiceMix(const std::array<std::int32_t, 16>& mixs) {
    m_mixs = mixs;
}

void NaomiAicaDsp::runSample(
    std::array<std::uint8_t, 0x8000>& registers,
    std::vector<std::uint8_t>& wave_ram) {
    const auto common = read16(registers, 0x2804u);
    m_rbp = ((common & 0x0fffu) * 2048u) & kWaveRamMask;
    m_rbl = (8192u << ((common >> 13u) & 3u)) - 1u;

    if (m_program_dirty) {
        m_program_dirty = false;
        m_stopped = true;
        for (auto offset = kMproStart; offset < kMproEnd; offset += 4u) {
            if (read32(registers, offset) != 0) {
                m_stopped = false;
                break;
            }
        }
    }
    if (!m_stopped && wave_ram.size() >= 0x800000u)
        runStep(registers, wave_ram);
}

void NaomiAicaDsp::runStep(
    std::array<std::uint8_t, 0x8000>& registers,
    std::vector<std::uint8_t>& wave_ram) {
    std::int32_t acc = 0;
    std::int32_t shifted = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t b = 0;
    std::int32_t inputs = 0;
    std::array<std::int32_t, 4> memval{};
    std::int32_t frc_reg = 0;
    std::int32_t y_reg = 0;
    std::uint32_t address_reg = 0;

    for (std::uint32_t step = 0; step < 128; ++step) {
        const auto instruction = kMproStart + step * 16u;
        const std::uint32_t iptr[4] = {
            read32(registers, instruction), read32(registers, instruction + 4u),
            read32(registers, instruction + 8u), read32(registers, instruction + 12u)};
        if ((iptr[0] | iptr[1] | iptr[2] | iptr[3]) == 0) {
            x = m_temp[m_mdec_ct & 0x7fu];
            y = frc_reg;
            acc = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(x) * y >> 12u) + x);
            continue;
        }

        const auto tra = (iptr[0] >> 9u) & 0x7fu;
        const bool twt = (iptr[0] & 0x100u) != 0;
        const bool xsel = (iptr[1] & 0x8000u) != 0;
        const auto ysel = (iptr[1] >> 13u) & 3u;
        const auto ira = (iptr[1] >> 7u) & 0x3fu;
        const bool iwt = (iptr[1] & 0x40u) != 0;
        const bool ewt = (iptr[2] & 0x1000u) != 0;
        const bool adrl = (iptr[2] & 0x80u) != 0;
        const bool frcl = (iptr[2] & 0x40u) != 0;
        const auto shift = (iptr[2] >> 4u) & 3u;
        const bool yrl = (iptr[2] & 8u) != 0;
        const bool negb = (iptr[2] & 4u) != 0;
        const bool zero = (iptr[2] & 2u) != 0;
        const bool bsel = (iptr[2] & 1u) != 0;

        if (ira <= 0x1fu) inputs = m_mems[ira];
        else if (ira <= 0x2fu) inputs = wrap_shift_left(m_mixs[ira - 0x20u], 4u);
        else if (ira <= 0x31u)
            inputs = wrap_shift_left(static_cast<std::int32_t>(read32(
                registers, 0x45c0u + (ira - 0x30u) * 4u)), 8u);
        else inputs = 0;

        if (iwt) {
            const auto iwa = (iptr[1] >> 1u) & 0x1fu;
            m_mems[iwa] = memval[step & 3u];
        }

        if (!zero) {
            b = bsel ? acc : m_temp[(tra + m_mdec_ct) & 0x7fu];
            if (negb) b = -b;
        } else b = 0;

        x = xsel ? inputs : m_temp[(tra + m_mdec_ct) & 0x7fu];
        if (ysel == 0) y = frc_reg;
        else if (ysel == 1)
            y = static_cast<std::int16_t>(read32(registers, 0x3000u + step * 4u)) >> 3;
        else if (ysel == 2) y = y_reg >> 11;
        else y = (y_reg >> 4) & 0x0fffu;
        if (yrl) y_reg = inputs;

        if (shift == 0 || shift == 3) shifted = acc;
        else shifted = wrap_shift_left(acc, 1u);
        if (shift < 2u) shifted = std::clamp(shifted, -0x00800000, 0x007fffff);
        acc = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(x) * y >> 12u) + b);

        if (twt) {
            const auto twa = (iptr[0] >> 1u) & 0x7fu;
            m_temp[(twa + m_mdec_ct) & 0x7fu] = shifted;
        }
        if (frcl) frc_reg = shift == 3u ? (shifted & 0x0fffu) : (shifted >> 11u);

        if (step & 1u) {
            const bool mwt = (iptr[2] & 0x4000u) != 0;
            const bool mrd = (iptr[2] & 0x2000u) != 0;
            if (mrd || mwt) {
                const bool table = (iptr[2] & 0x8000u) != 0;
                const auto masa = (iptr[3] >> 9u) & 0x3fu;
                const bool adreb = (iptr[3] & 0x100u) != 0;
                const bool nxadr = (iptr[3] & 0x80u) != 0;
                auto wave_address = read32(registers, 0x3200u + masa * 4u);
                if (adreb) wave_address += address_reg & 0x0fffu;
                if (nxadr) ++wave_address;
                if (!table) wave_address = (wave_address + m_mdec_ct) & m_rbl;
                else wave_address &= 0xffffu;
                wave_address = (m_rbp + (wave_address << 1u)) &
                               static_cast<std::uint32_t>(wave_ram.size() - 1u);
                if (mrd) {
                    const auto packed = static_cast<std::uint16_t>(
                        wave_ram[wave_address] |
                        (static_cast<std::uint16_t>(wave_ram[(wave_address + 1u) & kWaveRamMask]) << 8u));
                    memval[(step + 2u) & 3u] = unpack_dsp(packed);
                }
                if (mwt) {
                    const auto packed = pack_dsp(shifted);
                    wave_ram[wave_address] = static_cast<std::uint8_t>(packed);
                    wave_ram[(wave_address + 1u) & kWaveRamMask] =
                        static_cast<std::uint8_t>(packed >> 8u);
                }
            }
        }

        if (adrl) address_reg = shift == 3u
            ? static_cast<std::uint32_t>(shifted >> 12u)
            : static_cast<std::uint32_t>(inputs >> 16u);
        if (ewt) {
            const auto ewa = (iptr[2] >> 8u) & 0x0fu;
            write32(registers, 0x4580u + ewa * 4u,
                    static_cast<std::uint32_t>(shifted >> 8u));
        }
    }

    if (--m_mdec_ct == 0) m_mdec_ct = m_rbl + 1u;
}

} // namespace mamerecomp::naomi
