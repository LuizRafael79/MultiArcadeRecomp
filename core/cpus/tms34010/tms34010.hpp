#pragma once

#include <cstdint>

namespace multiarcaderecomp::tms34010 {

// The TMS34010 addresses memory in bits. Keep PC and memory-facing addresses
// in that form; converting them to byte addresses belongs in a board bus.
struct State {
    std::uint32_t a[15]{};
    std::uint32_t b[15]{};
    std::uint32_t sp{};
    std::uint32_t pc{};
    std::uint32_t st{};
    std::uint64_t cycles{};
};

enum class Bank : std::uint8_t { A, B };

enum class BlockExit : std::uint8_t {
    Dispatch,          // PC changed or block ended; look up the next block.
    Budget,            // Cycle deadline reached between instructions.
    NeedsInterpreter,  // No native translation; PC still points at this op.
    BusFault,          // Required bus callback is missing.
};

struct Bus {
    void* opaque{};
    std::uint16_t (*read16)(void*, std::uint32_t bit_address){};
    void (*write16)(void*, std::uint32_t bit_address, std::uint16_t value){};
    // Pixel callbacks belong to the board: PSIZE, raster operations,
    // transparency and shift-register routing are controlled by its I/O regs.
    std::uint32_t (*read_pixel)(void*, std::uint32_t bit_address){};
    bool (*write_pixel)(void*, std::uint32_t bit_address, std::uint32_t value){};
    // Convert XY coordinates with source or destination pixel pitch.
    bool (*convert_src_xy)(void*, std::uint32_t packed_xy,
                           std::uint32_t offset, std::uint32_t* linear_address){};
    bool (*convert_dst_xy)(void*, std::uint32_t packed_xy,
                           std::uint32_t offset, std::uint32_t* linear_address){};
    std::uint8_t (*window_checking)(void*){};
    // True when instructions may be fetched from this writable address.
    bool (*is_mutable_code)(void*, std::uint32_t bit_address){};
    // Raw internal interrupt latches for the CPU's boundary check. This
    // bypasses guest-visible read quirks such as MAME's early-DI polling bit.
    void (*read_interrupt_state)(void*, std::uint16_t* host_control,
                                 std::uint16_t* pending,
                                 std::uint16_t* enabled){};
};

// Execute a TMS34010 WPIXEL operation using the board's CONTROL/PSIZE state,
// including the selected raster operation and zero-result transparency.
bool write_pixel_operation(Bus& bus, std::uint32_t bit_address,
                           std::uint32_t source) noexcept;

// Execute the TMS34010 PIXBLT B,XY operation through the same board callbacks
// as generated code. The helper advances PC/cycles on success.
bool execute_pixblt_b_xy_native(State& state, Bus& bus) noexcept;

using Block = BlockExit (*)(State&, Bus&, std::uint64_t stop_cycle);

// Generated game code supplies these definitions and the block lookup table.
Block lookup(std::uint32_t bit_pc) noexcept;
std::uint32_t entry_pc() noexcept;

inline std::uint32_t& reg(State& state, Bank bank, unsigned index) noexcept {
    if (index >= 15) return state.sp;
    return bank == Bank::A ? state.a[index] : state.b[index];
}

inline constexpr std::uint32_t flag_n = 1u << 31;
inline constexpr std::uint32_t flag_c = 1u << 30;
inline constexpr std::uint32_t flag_z = 1u << 29;
inline constexpr std::uint32_t flag_v = 1u << 28;

inline void set_nz(State& state, std::uint32_t value) noexcept {
    state.st &= ~(flag_n | flag_z);
    if (value == 0) state.st |= flag_z;
    if (value & 0x80000000u) state.st |= flag_n;
}

inline void set_logic_z(State& state, std::uint32_t value) noexcept {
    state.st &= ~flag_z;
    if (value == 0) state.st |= flag_z;
}

inline void set_add_flags(State& state, std::uint32_t lhs,
                          std::uint32_t rhs, std::uint32_t result) noexcept {
    state.st &= ~(flag_n | flag_c | flag_z | flag_v);
    if (result == 0) state.st |= flag_z;
    if (result & 0x80000000u) state.st |= flag_n;
    if ((~(lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) state.st |= flag_v;
    if (static_cast<std::uint64_t>(lhs) + rhs > 0xffffffffull) state.st |= flag_c;
}

inline void set_sub_flags(State& state, std::uint32_t lhs,
                          std::uint32_t rhs, std::uint32_t result) noexcept {
    state.st &= ~(flag_n | flag_c | flag_z | flag_v);
    if (result == 0) state.st |= flag_z;
    if (result & 0x80000000u) state.st |= flag_n;
    if (((lhs ^ rhs) & (lhs ^ result)) & 0x80000000u) state.st |= flag_v;
    // The TMS34010 C flag represents borrow for subtraction.
    if (rhs > lhs) state.st |= flag_c;
}

inline bool condition(std::uint32_t st, unsigned code) noexcept {
    const bool n = (st & flag_n) != 0;
    const bool c = (st & flag_c) != 0;
    const bool z = (st & flag_z) != 0;
    const bool v = (st & flag_v) != 0;
    switch (code & 0x0f) {
    case 0x0: return true;                 // UC
    case 0x1: return !n && !z;             // P
    case 0x2: return c || z;               // LS
    case 0x3: return !c && !z;             // HI
    case 0x4: return n != v;               // LT
    case 0x5: return n == v;               // GE
    case 0x6: return z || (n != v);        // LE
    case 0x7: return !z && (n == v);       // GT
    case 0x8: return c;
    case 0x9: return !c;
    case 0xa: return z;
    case 0xb: return !z;
    case 0xc: return v;
    case 0xd: return !v;
    case 0xe: return n;
    default:  return !n;
    }
}

inline bool write_field(Bus& bus, std::uint32_t bit_address,
                        std::uint32_t value, std::uint32_t st,
                        unsigned field) noexcept {
    if (!bus.read16 || !bus.write16) return false;
    unsigned width = (st >> (field ? 6 : 0)) & 0x1f;
    if (!width) width = 32;
    unsigned consumed = 0;
    while (consumed < width) {
        const std::uint32_t address = (bit_address + consumed) & 0xfffffff0u;
        const unsigned shift = (bit_address + consumed) & 0x0f;
        const unsigned count = (width - consumed < 16 - shift)
            ? width - consumed : 16 - shift;
        const std::uint32_t field_mask = (1u << count) - 1u;
        const std::uint16_t old_word = bus.read16(bus.opaque, address);
        const std::uint32_t mask = field_mask << shift;
        const std::uint32_t bits = ((value >> consumed) & field_mask) << shift;
        const std::uint16_t result = static_cast<std::uint16_t>(
            (old_word & ~mask) | bits);
        bus.write16(bus.opaque, address, result);
        consumed += count;
    }
    return true;
}

inline bool read_field(Bus& bus, std::uint32_t bit_address,
                       std::uint32_t st, unsigned field,
                       std::uint32_t& value) noexcept {
    if (!bus.read16) return false;
    unsigned width = (st >> (field ? 6 : 0)) & 0x1f;
    if (!width) width = 32;
    value = 0;
    unsigned consumed = 0;
    while (consumed < width) {
        const std::uint32_t address = (bit_address + consumed) & 0xfffffff0u;
        const unsigned shift = (bit_address + consumed) & 0x0f;
        const unsigned count = (width - consumed < 16 - shift)
            ? width - consumed : 16 - shift;
        const std::uint32_t field_mask = (1u << count) - 1u;
        const std::uint32_t bits =
            (bus.read16(bus.opaque, address) >> shift) & field_mask;
        value |= bits << consumed;
        consumed += count;
    }
    const unsigned extend_bit = field ? 11 : 5;
    if (width < 32 && (st & (1u << extend_bit)) &&
        (value & (1u << (width - 1)))) {
        value |= ~((1u << width) - 1u);
    }
    return true;
}

inline unsigned field_increment(std::uint32_t st, unsigned field) noexcept {
    const unsigned width = (st >> (field ? 6 : 0)) & 0x1f;
    return width ? width : 32;
}

inline bool write_long(Bus& bus, std::uint32_t bit_address,
                       std::uint32_t value) noexcept {
    if (!bus.read16 || !bus.write16) return false;
    unsigned consumed = 0;
    constexpr unsigned width = 32;
    while (consumed < width) {
        const std::uint32_t address = (bit_address + consumed) & 0xfffffff0u;
        const unsigned shift = (bit_address + consumed) & 0x0f;
        const unsigned count = (width - consumed < 16 - shift)
            ? width - consumed : 16 - shift;
        const std::uint32_t field_mask = (1u << count) - 1u;
        const std::uint16_t old_word = bus.read16(bus.opaque, address);
        const std::uint32_t mask = field_mask << shift;
        const std::uint32_t bits = ((value >> consumed) & field_mask) << shift;
        const std::uint16_t result = static_cast<std::uint16_t>(
            (old_word & ~mask) | bits);
        bus.write16(bus.opaque, address, result);
        consumed += count;
    }
    return true;
}

inline bool read_long(Bus& bus, std::uint32_t bit_address,
                      std::uint32_t& value) noexcept {
    if (!bus.read16) return false;
    const unsigned shift = bit_address & 0x0f;
    const std::uint32_t aligned = bit_address & 0xfffffff0u;
    const std::uint32_t low = static_cast<std::uint32_t>(bus.read16(bus.opaque, aligned)) |
        (static_cast<std::uint32_t>(bus.read16(bus.opaque, aligned + 16u)) << 16);
    if (!shift) {
        value = low;
    } else {
        const std::uint32_t high = static_cast<std::uint32_t>(bus.read16(bus.opaque, aligned + 32u)) |
            (static_cast<std::uint32_t>(bus.read16(bus.opaque, aligned + 48u)) << 16);
        value = (low >> shift) | (high << (32 - shift));
    }
    return true;
}

inline bool read_byte(Bus& bus, std::uint32_t bit_address,
                      std::uint8_t& value) noexcept {
    if (!bus.read16) return false;
    const std::uint32_t aligned = bit_address & 0xfffffff0u;
    const unsigned shift = bit_address & 0x0f;
    std::uint32_t bits = bus.read16(bus.opaque, aligned);
    if (shift > 8)
        bits |= static_cast<std::uint32_t>(bus.read16(bus.opaque, aligned + 16u)) << 16;
    value = static_cast<std::uint8_t>(bits >> shift);
    return true;
}

inline bool write_byte(Bus& bus, std::uint32_t bit_address,
                       std::uint8_t value) noexcept {
    if (!bus.read16 || !bus.write16) return false;
    unsigned consumed = 0;
    while (consumed < 8) {
        const std::uint32_t address = (bit_address + consumed) & 0xfffffff0u;
        const unsigned shift = (bit_address + consumed) & 0x0f;
        const unsigned count = (8 - consumed < 16 - shift) ? 8 - consumed : 16 - shift;
        const std::uint32_t field_mask = (1u << count) - 1u;
        const std::uint32_t mask = field_mask << shift;
        const std::uint32_t bits = ((value >> consumed) & field_mask) << shift;
        const std::uint16_t old_word = bus.read16(bus.opaque, address);
        bus.write16(bus.opaque, address,
                    static_cast<std::uint16_t>((old_word & ~mask) | bits));
        consumed += count;
    }
    return true;
}

} // namespace multiarcaderecomp::tms34010
