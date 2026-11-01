#pragma once

#include "adsp2105.hpp"

#include <algorithm>

namespace mamerecomp::adsp2105 {

inline constexpr std::uint16_t mstat_reverse = 0x0002;

// MAME's ADSP mask table is a 14-bit circular-window mask. For non-power-of-
// two lengths, the next power-of-two boundary is used, as in its lookup table.
inline std::uint16_t dag_length_mask(std::uint16_t length) noexcept {
    length &= pc_mask;
    if (!length) return pc_mask;
    if (length > 0x2000) return 0;
    std::uint32_t window = 1;
    while (window < length) window <<= 1;
    return static_cast<std::uint16_t>(pc_mask & ~(window - 1));
}

inline void dag_write_i(State& state, unsigned index,
                        std::uint16_t value) noexcept {
    if (index >= state.i.size()) return;
    state.i[index] = value & pc_mask;
    state.base[index] = state.i[index] & state.lmask[index];
}

inline void dag_write_l(State& state, unsigned index,
                        std::uint16_t value) noexcept {
    if (index >= state.l.size()) return;
    state.l[index] = value & pc_mask;
    state.lmask[index] = dag_length_mask(state.l[index]);
    state.base[index] = state.i[index] & state.lmask[index];
}

inline std::uint16_t dag_reverse14(std::uint16_t value) noexcept {
    value &= pc_mask;
    std::uint16_t reversed = 0;
    for (unsigned bit = 0; bit < 14; ++bit) {
        reversed = static_cast<std::uint16_t>((reversed << 1) | (value & 1));
        value >>= 1;
    }
    return reversed;
}

inline std::uint16_t dag_advance(State& state, unsigned ireg,
                                unsigned mreg) noexcept {
    if (ireg >= state.i.size() || mreg >= state.m.size()) return 0;
    std::uint32_t address = (state.i[ireg] + state.m[mreg]) & pc_mask;
    const std::uint32_t base = state.base[ireg];
    const std::uint32_t length = state.l[ireg];
    if (address < base) address += length;
    else if (address >= base + length) address -= length;
    state.i[ireg] = static_cast<std::uint16_t>(address & pc_mask);
    return static_cast<std::uint16_t>(state.i[ireg]);
}

inline bool dag_read_data(State& state, Bus& bus, unsigned ireg,
                          unsigned mreg, bool bit_reverse,
                          std::uint16_t& value) noexcept {
    if (!bus.read_data16 || ireg >= 8 || mreg >= 8) return false;
    const auto address = bit_reverse && (state.mstat & mstat_reverse)
        ? dag_reverse14(state.i[ireg]) : state.i[ireg];
    value = bus.read_data16(bus.opaque, address);
    dag_advance(state, ireg, mreg);
    return true;
}

inline bool dag_write_data(State& state, Bus& bus, unsigned ireg,
                           unsigned mreg, bool bit_reverse,
                           std::uint16_t value) noexcept {
    if (!bus.write_data16 || ireg >= 8 || mreg >= 8) return false;
    const auto address = bit_reverse && (state.mstat & mstat_reverse)
        ? dag_reverse14(state.i[ireg]) : state.i[ireg];
    bus.write_data16(bus.opaque, address, value);
    dag_advance(state, ireg, mreg);
    return true;
}

inline bool dag_read_program(State& state, Bus& bus, unsigned ireg,
                             unsigned mreg, std::uint16_t& value) noexcept {
    if (!bus.read_program24 || ireg >= 8 || mreg >= 8) return false;
    const std::uint32_t opcode = bus.read_program24(bus.opaque, state.i[ireg]) &
                                 opcode_mask;
    state.px = static_cast<std::uint8_t>(opcode);
    value = static_cast<std::uint16_t>(opcode >> 8);
    dag_advance(state, ireg, mreg);
    return true;
}

inline bool dag_write_program(State& state, Bus& bus, unsigned ireg,
                              unsigned mreg, std::uint16_t value) noexcept {
    if (!bus.write_program24 || ireg >= 8 || mreg >= 8) return false;
    bus.write_program24(bus.opaque, state.i[ireg],
        ((static_cast<std::uint32_t>(value) << 8) | state.px) & opcode_mask);
    dag_advance(state, ireg, mreg);
    return true;
}

// The ADSP instruction fields select I/M pairs with these bit slices. DAG1
// supports bit-reversed data addresses; DAG2 is used for data/program space.
inline bool dag1_read(State& state, Bus& bus, std::uint32_t opcode,
                      std::uint16_t& value) noexcept {
    return dag_read_data(state, bus, (opcode >> 2) & 3, opcode & 3,
                         true, value);
}
inline bool dag1_write(State& state, Bus& bus, std::uint32_t opcode,
                       std::uint16_t value) noexcept {
    return dag_write_data(state, bus, (opcode >> 2) & 3, opcode & 3,
                          true, value);
}
inline bool dag2_read(State& state, Bus& bus, std::uint32_t opcode,
                      std::uint16_t& value) noexcept {
    return dag_read_data(state, bus, 4 + ((opcode >> 2) & 3),
                         4 + (opcode & 3), false, value);
}
inline bool dag2_write(State& state, Bus& bus, std::uint32_t opcode,
                       std::uint16_t value) noexcept {
    return dag_write_data(state, bus, 4 + ((opcode >> 2) & 3),
                          4 + (opcode & 3), false, value);
}
inline bool dag2_program_read(State& state, Bus& bus, std::uint32_t opcode,
                              std::uint16_t& value) noexcept {
    return dag_read_program(state, bus, 4 + ((opcode >> 2) & 3),
                            4 + (opcode & 3), value);
}
inline bool dag2_program_write(State& state, Bus& bus, std::uint32_t opcode,
                               std::uint16_t value) noexcept {
    return dag_write_program(state, bus, 4 + ((opcode >> 2) & 3),
                             4 + (opcode & 3), value);
}

} // namespace mamerecomp::adsp2105
