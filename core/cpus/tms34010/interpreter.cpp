// Incremental TMS34010 interpreter fallback. The PIXBLT L,XY path is adapted
// from MAME's tms34010/34010gfx.hxx; timing is approximate until the CPU/beam
// scheduler is connected. Unimplemented instructions remain explicit stops.
#include "interpreter.hpp"
#include "mame_interpreter.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace mamerecomp::tms34010 {
namespace {
constexpr std::uint32_t kIoBase = 0xc0000000u;
constexpr unsigned kControl = 11;
constexpr unsigned kIntpend = 18;
constexpr unsigned kConvsp = 19;
constexpr unsigned kConvdp = 20;
constexpr std::uint32_t kFlagV = 1u << 28;
constexpr std::uint32_t kWindowViolation = 0x0800;

bool is_exception_opcode(std::uint16_t opcode) noexcept;
bool is_unimplemented_opcode(std::uint16_t opcode) noexcept;
unsigned graphics_pixel_width(std::uint16_t psize) noexcept;
std::uint32_t raster_op(unsigned operation, std::uint32_t dst,
                        std::uint32_t mask, std::uint32_t src) noexcept;

bool read_io(Bus& bus, unsigned index, std::uint16_t& value) noexcept {
    if (!bus.read16) return false;
    value = bus.read16(bus.opaque, kIoBase + index * 16u);
    return true;
}

bool write_graphics_pixel(Bus& bus, std::uint32_t address,
                          std::uint32_t source) noexcept {
    return write_pixel_operation(bus, address, source);
}

bool read_graphics_pixel(Bus& bus, std::uint32_t address,
                         std::uint32_t& value) noexcept {
    if (!bus.read_pixel) return false;
    std::uint16_t psize = 0;
    if (!read_io(bus, 21, psize)) return false;
    const unsigned bpp = graphics_pixel_width(psize);
    const std::uint32_t mask = bpp == 32
        ? 0xffffffffu : (1u << bpp) - 1u;
    value = bus.read_pixel(bus.opaque, address) & mask;
    return true;
}

bool xy_pixel_is_enabled(State& state, Bus& bus,
                         std::uint32_t packed_xy) noexcept {
    if (!bus.window_checking) return true;
    const unsigned mode = bus.window_checking(bus.opaque);
    if (!mode) return true;
    const auto x = static_cast<std::int16_t>(packed_xy);
    const auto y = static_cast<std::int16_t>(packed_xy >> 16);
    const auto left = static_cast<std::int16_t>(state.b[5]);
    const auto top = static_cast<std::int16_t>(state.b[5] >> 16);
    const auto right = static_cast<std::int16_t>(state.b[6]);
    const auto bottom = static_cast<std::int16_t>(state.b[6] >> 16);
    state.st &= ~flag_v;
    if (x < left || x > right || y < top || y > bottom) {
        state.st |= flag_v;
        return false;
    }
    // MAME's window-check mode 1 raises V and suppresses even in-window writes.
    return mode != 1;
}

bool pixt_register_xy(State& state, Bus& bus, unsigned bank,
                      unsigned source, unsigned destination) noexcept {
    if (!bus.convert_dst_xy) return false;
    const std::uint32_t xy = reg(state, bank ? Bank::B : Bank::A, destination);
    if (!xy_pixel_is_enabled(state, bus, xy)) {
        state.cycles += 4;
        return true;
    }
    std::uint32_t address = 0;
    if (!bus.convert_dst_xy(bus.opaque, xy, state.b[4], &address)) return false;
    const auto pixel = reg(state, bank ? Bank::B : Bank::A, source);
    if (!write_graphics_pixel(bus, address, pixel)) return false;
    state.cycles += 4;
    return true;
}

bool pixt_xy_register(State& state, Bus& bus, unsigned bank,
                      unsigned source, unsigned destination) noexcept {
    if (!bus.convert_src_xy) return false;
    const std::uint32_t xy = reg(state, bank ? Bank::B : Bank::A, source);
    std::uint32_t address = 0;
    if (!bus.convert_src_xy(bus.opaque, xy, state.b[4], &address)) return false;
    std::uint32_t pixel = 0;
    if (!read_graphics_pixel(bus, address, pixel)) return false;
    reg(state, bank ? Bank::B : Bank::A, destination) = pixel;
    state.st &= ~flag_v;
    if (pixel) state.st |= flag_v;
    state.cycles += 6;
    return true;
}

bool pixt_xy_xy(State& state, Bus& bus, unsigned bank,
                unsigned source, unsigned destination) noexcept {
    if (!bus.convert_src_xy || !bus.convert_dst_xy) return false;
    const std::uint32_t src_xy = reg(state, bank ? Bank::B : Bank::A, source);
    const std::uint32_t dst_xy = reg(state, bank ? Bank::B : Bank::A, destination);
    if (!xy_pixel_is_enabled(state, bus, dst_xy)) {
        state.cycles += 7;
        return true;
    }
    std::uint32_t src_address = 0;
    std::uint32_t dst_address = 0;
    if (!bus.convert_src_xy(bus.opaque, src_xy, state.b[4], &src_address) ||
        !bus.convert_dst_xy(bus.opaque, dst_xy, state.b[4], &dst_address))
        return false;
    std::uint32_t pixel = 0;
    if (!read_graphics_pixel(bus, src_address, pixel) ||
        !write_graphics_pixel(bus, dst_address, pixel))
        return false;
    state.cycles += 7;
    return true;
}

bool drav(State& state, Bus& bus, unsigned bank,
          unsigned source, unsigned destination) noexcept {
    if (!bus.convert_dst_xy || !bus.read_pixel || !bus.write_pixel)
        return false;

    auto& dst = reg(state, bank ? Bank::B : Bank::A, destination);
    const auto& src = reg(state, bank ? Bank::B : Bank::A, source);
    const std::uint32_t xy = dst;
    if (bus.window_checking && bus.window_checking(bus.opaque) != 0) {
        state.st &= ~flag_v;
        const auto x = static_cast<std::int16_t>(xy);
        const auto y = static_cast<std::int16_t>(xy >> 16);
        const auto left = static_cast<std::int16_t>(state.b[5]);
        const auto top = static_cast<std::int16_t>(state.b[5] >> 16);
        const auto right = static_cast<std::int16_t>(state.b[6]);
        const auto bottom = static_cast<std::int16_t>(state.b[6] >> 16);
        if (x < left || x > right || y < top || y > bottom) {
            state.st |= flag_v;
        } else if (bus.window_checking(bus.opaque) != 1) {
            std::uint32_t address = 0;
            if (!bus.convert_dst_xy(bus.opaque, xy, state.b[4], &address) ||
                !write_graphics_pixel(bus, address, state.b[9]))
                return false;
        }
    } else {
        std::uint32_t address = 0;
        if (!bus.convert_dst_xy(bus.opaque, xy, state.b[4], &address) ||
            !write_graphics_pixel(bus, address, state.b[9]))
            return false;
    }

    const auto x = static_cast<std::int16_t>(dst);
    const auto y = static_cast<std::int16_t>(dst >> 16);
    const auto dx = static_cast<std::int16_t>(src);
    const auto dy = static_cast<std::int16_t>(src >> 16);
    dst = static_cast<std::uint16_t>(static_cast<std::int16_t>(x + dx)) |
          (static_cast<std::uint32_t>(static_cast<std::uint16_t>(
               static_cast<std::int16_t>(y + dy))) << 16);
    state.cycles += 4;
    return true;
}

bool pixt_linear(State& state, Bus& bus, unsigned bank, std::uint16_t family,
                 unsigned source, unsigned destination) noexcept {
    const Bank selected = bank ? Bank::B : Bank::A;
    if (family == 0xf800) {
        if (!write_graphics_pixel(bus, reg(state, selected, destination),
                                  reg(state, selected, source)))
            return false;
        state.cycles += 2;
    } else if (family == 0xfa00) {
        std::uint32_t pixel = 0;
        if (!read_graphics_pixel(bus, reg(state, selected, source), pixel))
            return false;
        reg(state, selected, destination) = pixel;
        state.st &= ~flag_v;
        if (pixel) state.st |= flag_v;
        state.cycles += 4;
    } else if (family == 0xfc00) {
        std::uint32_t pixel = 0;
        if (!read_graphics_pixel(bus, reg(state, selected, source), pixel) ||
            !write_graphics_pixel(bus, reg(state, selected, destination), pixel))
            return false;
        state.cycles += 4;
    } else {
        return false;
    }
    return true;
}

bool pixt(State& state, Bus& bus, std::uint16_t opcode) noexcept {
    const unsigned bank = (opcode & 0x10) ? 1u : 0u;
    const unsigned source = (opcode >> 5) & 15;
    const unsigned destination = opcode & 15;
    const std::uint16_t family = opcode & 0xfe00;
    bool handled = false;
    if (family == 0xf000)
        handled = pixt_register_xy(state, bus, bank, source, destination);
    else if (family == 0xf200)
        handled = pixt_xy_register(state, bus, bank, source, destination);
    else if (family == 0xf400)
        handled = pixt_xy_xy(state, bus, bank, source, destination);
    else
        handled = pixt_linear(state, bus, bank, family, source, destination);
    if (handled) state.pc += 16;
    return handled;
}

std::uint32_t conv_value(std::uint16_t value) noexcept {
    return 1u << ((~value) & 31);
}

unsigned graphics_pixel_width(std::uint16_t psize) noexcept {
    if (psize == 0x20) return 32;
    // MAME indexes pixelsize_lookup with PSIZE & 0x1f. Each table entry
    // selects 1, 2, 4, 8, or 16 bits per pixel; the register encodings are
    // ranges, not just the five canonical values.
    const unsigned code = psize & 0x1f;
    if (code < 2) return 1;
    if (code < 4) return 2;
    if (code < 8) return 4;
    if (code < 16) return 8;
    return 16;
}

std::uint32_t raster_op(unsigned operation, std::uint32_t dst,
                        std::uint32_t mask, std::uint32_t src) noexcept {
    switch (operation & 31) {
    case 0: return src;
    case 1: return src & dst;
    case 2: return src & ~dst;
    case 3: return 0;
    case 4: return (src | ~dst) & mask;
    case 5: return ~(src ^ dst) & mask;
    case 6: return ~dst & mask;
    case 7: return ~(src | dst) & mask;
    case 8: return (src | dst) & mask;
    case 9: return dst & mask;
    case 10: return (src ^ dst) & mask;
    case 11: case 13: return (~src & dst) & mask;
    case 12: return mask;
    case 14: return ~(src & dst) & mask;
    case 15: return src ^ mask;
    case 16: return (src + dst) & mask;
    case 17: {
        const std::uint32_t sum = src + (dst & mask);
        return sum > mask ? mask : sum;
    }
    case 18: return (dst - src) & mask;
    case 19: return src > (dst & mask) ? src - (dst & mask) : 0;
    case 20: return std::max(src, dst & mask);
    case 21: return std::min(src, dst & mask);
    default: return src;
    }
}

std::uint32_t pack_xy(std::int16_t x, std::int16_t y) noexcept {
    return static_cast<std::uint16_t>(x) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16);
}

bool pixblt_l_xy(State& state, Bus& bus) noexcept {
    // MAME's opcode slot 0xf02 maps to PIXBLT L,XY. It uses B0/SADDR,
    // B1/SPTCH, B2/DADDR, B4/OFFSET, B5/B6 window bounds, B7/DYDX and
    // CONTROL/PSIZE I/O registers.
    if (!bus.read16 || !bus.write16 || !bus.read_pixel || !bus.write_pixel ||
        !bus.convert_dst_xy || !bus.window_checking)
        return false;

    std::uint16_t control = 0;
    std::uint16_t psize = 0;
    std::uint16_t convsp_reg = 0;
    std::uint16_t convdp_reg = 0;
    if (!read_io(bus, kControl, control) || !read_io(bus, 21, psize) ||
        !read_io(bus, kConvsp, convsp_reg) || !read_io(bus, kConvdp, convdp_reg))
        return false;
    const unsigned bpp = graphics_pixel_width(psize);

    std::int32_t width = static_cast<std::int16_t>(state.b[7] & 0xffff);
    std::int32_t height = static_cast<std::int16_t>(state.b[7] >> 16);
    std::int32_t x = static_cast<std::int16_t>(state.b[2] & 0xffff);
    std::int32_t y = static_cast<std::int16_t>(state.b[2] >> 16);
    if (width <= 0 || height <= 0) {
        state.pc += 16;
        state.cycles += 7;
        return true;
    }

    const std::uint32_t source_pitch = state.b[1];
    const std::uint32_t source_row_pitch = conv_value(convsp_reg);
    const std::uint32_t destination_offset = state.b[4];
    const unsigned window_mode = bus.window_checking(bus.opaque);
    const std::int32_t original_x = x;
    const std::int32_t original_y = y;
    const std::int32_t original_width = width;
    const std::int32_t original_height = height;

    if (window_mode) {
        const std::int32_t left = static_cast<std::int16_t>(state.b[5] & 0xffff);
        const std::int32_t top = static_cast<std::int16_t>(state.b[5] >> 16);
        const std::int32_t right = static_cast<std::int16_t>(state.b[6] & 0xffff);
        const std::int32_t bottom = static_cast<std::int16_t>(state.b[6] >> 16);
        const std::int32_t clipped_x = std::max(x, left);
        const std::int32_t clipped_y = std::max(y, top);
        const std::int32_t clipped_right = std::min(x + width - 1, right);
        const std::int32_t clipped_bottom = std::min(y + height - 1, bottom);
        if (clipped_x != x || clipped_y != y || clipped_right != x + width - 1 ||
            clipped_bottom != y + height - 1)
            state.st |= kFlagV;
        x = clipped_x;
        y = clipped_y;
        width = clipped_right - clipped_x + 1;
        height = clipped_bottom - clipped_y + 1;
        if (width <= 0 || height <= 0) {
            state.pc += 16;
            state.cycles += 9;
            return true;
        }
        state.b[2] = pack_xy(static_cast<std::int16_t>(x),
                             static_cast<std::int16_t>(y));
        state.b[7] = static_cast<std::uint16_t>(width) |
                     (static_cast<std::uint32_t>(static_cast<std::uint16_t>(height)) << 16);
    }

    if (window_mode == 1) {
        std::uint16_t pending = 0;
        if (read_io(bus, kIntpend, pending))
            bus.write16(bus.opaque, kIoBase + kIntpend * 16u,
                        static_cast<std::uint16_t>(pending | kWindowViolation));
        state.pc += 16;
        state.cycles += 9;
        return true;
    }
    const std::uint32_t source_crop =
        static_cast<std::uint32_t>(y - original_y) * source_row_pitch +
        static_cast<std::uint32_t>(x - original_x) * bpp;
    const std::uint32_t source_base = state.b[0] + source_crop;
    std::uint32_t destination_base = 0;
    const std::uint32_t xy = pack_xy(static_cast<std::int16_t>(x),
                                     static_cast<std::int16_t>(y));
    if (!bus.convert_dst_xy(bus.opaque, xy, destination_offset,
                            &destination_base))
        return false;

    const unsigned rop = (control >> 10) & 31;
    const bool transparent = (control & 0x0020) != 0;
    const bool y_reverse = (control & 0x0200) != 0;
    const std::uint32_t pixel_mask = (1u << bpp) - 1u;
    const std::uint32_t convdp = conv_value(convdp_reg);
    std::uint32_t pixels = 0;
    for (std::int32_t row = 0; row < height; ++row) {
        const std::int32_t source_row = y_reverse ? (height - 1 - row) : row;
        const std::uint32_t src_row = source_base +
            static_cast<std::uint32_t>(source_row) * source_pitch;
        const std::uint32_t dst_row = destination_base +
            static_cast<std::uint32_t>(y_reverse ? -row : row) * convdp;
        for (std::int32_t col = 0; col < width; ++col) {
            const std::uint32_t source_pixel =
                bus.read_pixel(bus.opaque, src_row + static_cast<std::uint32_t>(col) * bpp) &
                pixel_mask;
            const std::uint32_t dst_address = dst_row +
                static_cast<std::uint32_t>(col) * bpp;
            const std::uint32_t aligned = dst_address & ~15u;
            const unsigned shift = dst_address & 15u;
            const std::uint32_t mask = pixel_mask << shift;
            const std::uint32_t old_word = bus.read16(bus.opaque, aligned);
            std::uint32_t source_word = source_pixel << shift;
            source_word = raster_op(rop, old_word, mask, source_word);
            if (!transparent || source_word != 0) {
                const std::uint16_t combined = static_cast<std::uint16_t>(
                    (old_word & ~mask) | source_word);
                bus.write16(bus.opaque, aligned, combined);
            }
            ++pixels;
        }
    }

    (void)original_width;
    (void)original_height;
    state.st &= ~(1u << 25); // P: operation completed synchronously here.
    state.b[0] += static_cast<std::uint32_t>(height) * source_pitch;
    const std::int16_t next_y = static_cast<std::int16_t>(
        static_cast<std::int16_t>(state.b[2] >> 16) + height);
    state.b[2] = (state.b[2] & 0xffffu) |
                 (static_cast<std::uint32_t>(static_cast<std::uint16_t>(next_y)) << 16);
    state.pc += 16;
    state.cycles += 16u + pixels * 6u;
    return true;
}

bool pixblt_b_xy(State& state, Bus& bus) noexcept {
    if (!bus.read16 || !bus.read_pixel || !bus.write_pixel ||
        !bus.convert_dst_xy)
        return false;
    std::uint16_t control = 0;
    std::uint16_t psize = 0;
    if (!read_io(bus, kControl, control) || !read_io(bus, 21, psize))
        return false;
    const unsigned bpp = graphics_pixel_width(psize);
    std::int32_t width = static_cast<std::int16_t>(state.b[7]);
    std::int32_t height = static_cast<std::int16_t>(state.b[7] >> 16);
    std::int32_t x = static_cast<std::int16_t>(state.b[2]);
    std::int32_t y = static_cast<std::int16_t>(state.b[2] >> 16);
    if (width <= 0 || height <= 0) {
        state.pc += 16;
        ++state.cycles;
        return true;
    }

    std::uint32_t destination = 0;
    if (!bus.convert_dst_xy(bus.opaque, state.b[2], state.b[4],
                            &destination))
        return false;
    const std::uint32_t pitch = state.b[3];
    const std::uint32_t source_pitch = state.b[1];
    const std::uint32_t mask = bpp == 32 ? 0xffffffffu : (1u << bpp) - 1u;
    const std::uint32_t color0 = state.b[8] & mask;
    const std::uint32_t color1 = state.b[9] & mask;
    const unsigned rop = (control >> 10) & 31;
    const bool transparent = (control & 0x20) != 0;

    std::uint64_t pixels = 0;
    for (std::int32_t row = 0; row < height; ++row) {
        std::uint32_t source = state.b[0] +
            static_cast<std::uint32_t>(row) * source_pitch;
        std::uint32_t destination_row = destination +
            static_cast<std::uint32_t>(row) * pitch;
        for (std::int32_t column = 0; column < width; ++column) {
            const std::uint32_t source_word_address = source & 0xfffffff0u;
            const unsigned source_bit = source & 15;
            const bool bit = (bus.read16(bus.opaque, source_word_address) >>
                              source_bit) & 1u;
            const std::uint32_t source_pixel = bit ? color1 : color0;
            const std::uint32_t address = destination_row +
                static_cast<std::uint32_t>(column) * bpp;
            const std::uint32_t old_pixel = bus.read_pixel(bus.opaque, address);
            const std::uint32_t result = raster_op(rop, old_pixel, mask,
                                                   source_pixel) & mask;
            if (!transparent || result != 0) {
                if (!bus.write_pixel(bus.opaque, address, result)) return false;
            }
            ++source;
            ++pixels;
        }
    }

    (void)x;
    (void)y;
    state.b[0] += static_cast<std::uint32_t>(height) * source_pitch;
    const std::int16_t next_y = static_cast<std::int16_t>(
        static_cast<std::int16_t>(state.b[2] >> 16) + height);
    state.b[2] = (state.b[2] & 0xffffu) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(next_y)) << 16);
    state.pc += 16;
    state.cycles += 4u + pixels * 4u;
    return true;
}

bool fill_xy(State& state, Bus& bus) noexcept {
    if (!bus.read16 || !bus.read_pixel || !bus.write_pixel ||
        !bus.convert_dst_xy)
        return false;
    std::uint16_t control = 0;
    std::uint16_t psize = 0;
    if (!read_io(bus, kControl, control) || !read_io(bus, 21, psize))
        return false;
    const unsigned bpp = graphics_pixel_width(psize);
    const std::int32_t width = static_cast<std::int16_t>(state.b[7]);
    const std::int32_t height = static_cast<std::int16_t>(state.b[7] >> 16);
    if (width <= 0 || height <= 0) {
        state.pc += 16;
        ++state.cycles;
        return true;
    }
    std::uint32_t destination = 0;
    if (!bus.convert_dst_xy(bus.opaque, state.b[2], state.b[4],
                            &destination))
        return false;
    const std::uint32_t pitch = state.b[3];
    const std::uint32_t mask = bpp == 32 ? 0xffffffffu : (1u << bpp) - 1u;
    const std::uint32_t color = state.b[9] & mask;
    const unsigned rop = (control >> 10) & 31;
    const bool transparent = (control & 0x20) != 0;
    std::uint64_t pixels = 0;
    for (std::int32_t row = 0; row < height; ++row) {
        for (std::int32_t column = 0; column < width; ++column) {
            const std::uint32_t address = destination +
                static_cast<std::uint32_t>(row) * pitch +
                static_cast<std::uint32_t>(column) * bpp;
            const std::uint32_t old_pixel = bus.read_pixel(bus.opaque, address);
            const std::uint32_t result = raster_op(rop, old_pixel, mask,
                                                   color) & mask;
            if (!transparent || result != 0) {
                if (!bus.write_pixel(bus.opaque, address, result)) return false;
            }
            ++pixels;
        }
    }
    const std::int16_t next_y = static_cast<std::int16_t>(
        static_cast<std::int16_t>(state.b[2] >> 16) + height);
    state.b[2] = (state.b[2] & 0xffffu) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(next_y)) << 16);
    state.pc += 16;
    state.cycles += 4u + pixels * 3u;
    return true;
}
} // namespace

bool write_pixel_operation(Bus& bus, std::uint32_t bit_address,
                           std::uint32_t source) noexcept {
    if (!bus.read_pixel || !bus.write_pixel) return false;
    std::uint16_t control = 0;
    std::uint16_t psize = 0;
    if (!read_io(bus, kControl, control) || !read_io(bus, 21, psize))
        return false;
    const unsigned bpp = graphics_pixel_width(psize);
    const std::uint32_t pixel_mask = bpp == 32
        ? 0xffffffffu : (1u << bpp) - 1u;
    const std::uint32_t old_pixel =
        bus.read_pixel(bus.opaque, bit_address) & pixel_mask;
    const std::uint32_t result = raster_op((control >> 10) & 31,
        old_pixel, pixel_mask, source & pixel_mask) & pixel_mask;
    if ((control & 0x20) && result == 0) return true;
    return bus.write_pixel(bus.opaque, bit_address, result);
}

static BlockExit interpreter_step_partial(State& state, Bus& bus,
                                          std::uint64_t) noexcept {
    if (!bus.read16) return BlockExit::BusFault;
    const std::uint16_t opcode = bus.read16(bus.opaque, state.pc);
    const unsigned bank = (opcode & 0x10) ? 1u : 0u;
    const unsigned reg_index = opcode & 15;
    const std::uint32_t next_pc = state.pc + 16u;
    const std::uint16_t sub = opcode & 0x01e0;

    // Match MAME's distinction: illop enters vector 30, while reserved
    // unimpl slots consume one cycle and continue without trapping.
    if (is_exception_opcode(opcode)) {
        state.sp -= 32u;
        if (!write_long(bus, state.sp, next_pc)) return BlockExit::BusFault;
        state.sp -= 32u;
        if (!write_long(bus, state.sp, state.st)) return BlockExit::BusFault;
        state.st = 0x10u;
        if (!read_long(bus, 0xfffffc20u, state.pc)) return BlockExit::BusFault;
        state.pc &= 0xfffffff0u;
        state.cycles += 16;
        return BlockExit::Dispatch;
    }
    if (is_unimplemented_opcode(opcode)) {
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xffe0) == 0x0900) {
        const unsigned trap = opcode & 0x1f;
        if (trap) {
            state.sp -= 32u;
            if (!write_long(bus, state.sp, next_pc)) return BlockExit::BusFault;
            state.sp -= 32u;
            if (!write_long(bus, state.sp, state.st)) return BlockExit::BusFault;
        }
        state.st = 0x10u;
        if (!read_long(bus, 0xffffffe0u - (trap << 5), state.pc))
            return BlockExit::BusFault;
        state.pc &= 0xfffffff0u;
        state.cycles += 16;
        return BlockExit::Dispatch;
    }

    if ((opcode & 0xfff0) == 0x0320) { // CLRC
        state.st &= ~flag_c;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0360) { // DINT
        state.st &= ~(1u << 21);
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0d60) { // EINT
        state.st |= 1u << 21;
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0de0) { // SETC
        state.st |= flag_c;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xffe0) == 0x0920) { // CALL A/B register
        const std::uint32_t target = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        state.sp -= 32u;
        if (!write_long(bus, state.sp, next_pc)) return BlockExit::BusFault;
        state.pc = target & 0xfffffff0u;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xffe0) == 0x0120) { // EXGPC A/B
        auto& target = reg(state, bank ? Bank::B : Bank::A, reg_index);
        const std::uint32_t old_pc = target;
        target = next_pc;
        state.pc = old_pc & 0xfffffff0u;
        state.cycles += 2;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xffe0) == 0x0160) { // JUMP A/B
        state.pc = reg(state, bank ? Bank::B : Bank::A, reg_index) &
                   0xfffffff0u;
        state.cycles += 2;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0100) { // EMU is a six-cycle NOP in RUN state
        state.pc = next_pc;
        state.cycles += 6;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x01c0) { // POPST
        std::uint32_t value = 0;
        if (!read_long(bus, state.sp, value)) return BlockExit::BusFault;
        state.sp += 32u;
        state.st = value;
        state.pc = next_pc;
        state.cycles += 8;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x01e0) { // PUSHST
        state.sp -= 32u;
        if (!write_long(bus, state.sp, state.st)) return BlockExit::BusFault;
        state.pc = next_pc;
        state.cycles += 2;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0x0000) {
        if (sub == 0x0140) { // GETPC
            reg(state, bank ? Bank::B : Bank::A, reg_index) = next_pc;
            state.pc = next_pc;
            ++state.cycles;
            return BlockExit::Dispatch;
        }
        if (sub == 0x0180) { // GETST
            reg(state, bank ? Bank::B : Bank::A, reg_index) = state.st;
            state.pc = next_pc;
            ++state.cycles;
            return BlockExit::Dispatch;
        }
        if (sub == 0x01a0) { // PUTST
            state.st = reg(state, bank ? Bank::B : Bank::A, reg_index);
            state.pc = next_pc;
            state.cycles += 3;
            return BlockExit::Dispatch;
        }
        if ((opcode & 0xffe0) == 0x0020) { // REV
            reg(state, bank ? Bank::B : Bank::A, reg_index) = 8;
            state.pc = next_pc;
            ++state.cycles;
            return BlockExit::Dispatch;
        }
    }

    if ((opcode & 0xfe00) == 0x0200) {
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                        reg_index);
        if (sub == 0x0180) { // ABS
            const std::uint32_t negated = 0u - destination;
            state.st &= ~(flag_n | flag_z | flag_v);
            if (static_cast<std::int32_t>(negated) > 0)
                destination = negated;
            set_nz(state, negated);
            if (negated == 0x80000000u) state.st |= flag_v;
            state.pc = next_pc;
            ++state.cycles;
            return BlockExit::Dispatch;
        }
        if (sub == 0x01a0 || sub == 0x01c0) { // NEG / NEGB
            const std::uint32_t original = destination;
            const std::uint32_t operand = sub == 0x01c0
                ? original + ((state.st & flag_c) ? 1u : 0u)
                : original;
            destination = 0u - operand;
            set_sub_flags(state, 0, operand, destination);
            state.pc = next_pc;
            ++state.cycles;
            return BlockExit::Dispatch;
        }
    }
    if ((opcode & 0xfe00) == 0x0400 || (opcode & 0xfe00) == 0x0600) {
        const unsigned field = (opcode & 0xfe00) == 0x0600 ? 1u : 0u;
        const unsigned width = (state.st >> (field ? 6 : 0)) & 31u;
        if (sub == 0x0000 || sub == 0x0020) {
            std::uint32_t& destination = reg(state,
                bank ? Bank::B : Bank::A, reg_index);
            if (width && width < 32) {
                const std::uint32_t mask = (1u << width) - 1u;
                if (sub == 0x0000) {
                    const std::uint32_t sign = 1u << (width - 1u);
                    destination = (destination & mask) & sign
                        ? (destination | ~mask) : (destination & mask);
                } else {
                    destination &= mask;
                }
            }
            state.st &= ~(flag_n | flag_z);
            set_nz(state, destination);
            state.pc = next_pc;
            state.cycles += 3;
            return BlockExit::Dispatch;
        }
    }

    if ((opcode & 0xfff0) == 0x0300) {
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x03e0) {
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                        reg_index);
        destination = ~destination;
        set_logic_z(state, destination);
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // Signed and unsigned multiply/divide/modulo families mirror MAME's
    // register pairing rules. Even destination registers use a 64-bit pair;
    // odd destinations use a single 32-bit operand where the instruction
    // definition permits it.
    if ((opcode & 0xfe00) == 0x5c00 || (opcode & 0xfe00) == 0x5e00) {
        const unsigned source_index = (opcode >> 5) & 15;
        const unsigned destination_index = opcode & 15;
        const bool signed_multiply = (opcode & 0xfe00) == 0x5c00;
        std::uint32_t source = reg(state, bank ? Bank::B : Bank::A,
                                   source_index);
        const unsigned source_width = (state.st >> 6) & 31;
        if (signed_multiply && source_width && source_width < 32 &&
            (source & (1u << (source_width - 1))))
            source |= ~((1u << source_width) - 1u);
        else if (!signed_multiply && source_width && source_width < 32)
            source &= (1u << source_width) - 1u;
        const std::uint32_t destination = reg(state, bank ? Bank::B : Bank::A,
                                              destination_index);
        const std::uint64_t product = signed_multiply
            ? static_cast<std::uint64_t>(static_cast<std::int64_t>(
                  static_cast<std::int32_t>(source)) *
                  static_cast<std::int64_t>(static_cast<std::int32_t>(destination)))
            : static_cast<std::uint64_t>(source) * destination;
        state.st &= signed_multiply ? ~(flag_n | flag_z) : ~flag_z;
        if (!product) state.st |= flag_z;
        if (signed_multiply && (product >> 63)) state.st |= flag_n;
        reg(state, bank ? Bank::B : Bank::A, destination_index) =
            static_cast<std::uint32_t>(product >> 32);
        reg(state, bank ? Bank::B : Bank::A,
            destination_index | 1u) = static_cast<std::uint32_t>(product);
        state.pc = next_pc;
        state.cycles += signed_multiply ? 20u : 21u;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0x5800 || (opcode & 0xfe00) == 0x5a00) {
        const unsigned source_index = (opcode >> 5) & 15;
        const unsigned destination_index = opcode & 15;
        const bool signed_divide = (opcode & 0xfe00) == 0x5800;
        const std::uint32_t divisor_bits =
            reg(state, bank ? Bank::B : Bank::A, source_index);
        std::uint32_t& quotient_reg = reg(state, bank ? Bank::B : Bank::A,
                                           destination_index);
        state.st &= signed_divide ? ~(flag_n | flag_z | flag_v)
                                  : ~(flag_z | flag_v);
        if (!divisor_bits) {
            state.st |= flag_v;
        } else if (destination_index & 1u) {
            if (signed_divide) {
                const auto dividend = static_cast<std::int32_t>(quotient_reg);
                const auto divisor = static_cast<std::int32_t>(divisor_bits);
                const std::int64_t result = static_cast<std::int64_t>(dividend) /
                                            static_cast<std::int64_t>(divisor);
                if (result < std::numeric_limits<std::int32_t>::min() ||
                    result > std::numeric_limits<std::int32_t>::max())
                    state.st |= flag_v;
                else {
                    quotient_reg = static_cast<std::uint32_t>(result);
                    set_nz(state, quotient_reg);
                }
            } else {
                quotient_reg /= divisor_bits;
                set_logic_z(state, quotient_reg);
            }
        } else {
            std::uint32_t& remainder_reg = reg(state,
                bank ? Bank::B : Bank::A, destination_index + 1u);
            const std::uint64_t pair =
                (static_cast<std::uint64_t>(quotient_reg) << 32) | remainder_reg;
            if (signed_divide) {
                const std::int64_t dividend = std::bit_cast<std::int64_t>(pair);
                const std::int64_t divisor = static_cast<std::int32_t>(divisor_bits);
                if (dividend == std::numeric_limits<std::int64_t>::min() &&
                    divisor == -1) {
                    state.st |= flag_v;
                    state.pc = next_pc;
                    state.cycles += 40;
                    return BlockExit::Dispatch;
                }
                const std::int64_t result = dividend / divisor;
                if (result < std::numeric_limits<std::int32_t>::min() ||
                    result > std::numeric_limits<std::int32_t>::max())
                    state.st |= flag_v;
                else {
                    remainder_reg = static_cast<std::uint32_t>(dividend % divisor);
                    quotient_reg = static_cast<std::uint32_t>(result);
                    set_nz(state, quotient_reg);
                }
            } else {
                const std::uint64_t result = pair / divisor_bits;
                if (result > std::numeric_limits<std::uint32_t>::max())
                    state.st |= flag_v;
                else {
                    remainder_reg = static_cast<std::uint32_t>(pair % divisor_bits);
                    quotient_reg = static_cast<std::uint32_t>(result);
                    set_logic_z(state, quotient_reg);
                }
            }
        }
        state.pc = next_pc;
        state.cycles += signed_divide
            ? ((destination_index & 1u) ? 39u : 40u) : 37u;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0x6c00 || (opcode & 0xfe00) == 0x6e00) {
        const unsigned source_index = (opcode >> 5) & 15;
        const unsigned destination_index = opcode & 15;
        const bool signed_modulo = (opcode & 0xfe00) == 0x6c00;
        const std::uint32_t source = reg(state, bank ? Bank::B : Bank::A,
                                         source_index);
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                          destination_index);
        state.st &= signed_modulo ? ~(flag_n | flag_z | flag_v)
                                  : ~(flag_z | flag_v);
        if (!source) state.st |= flag_v;
        else if (signed_modulo) {
            const auto lhs = static_cast<std::int32_t>(destination);
            const auto rhs = static_cast<std::int32_t>(source);
            const std::int64_t result =
                (lhs == std::numeric_limits<std::int32_t>::min() && rhs == -1)
                ? 0 : static_cast<std::int64_t>(lhs) % rhs;
            destination = static_cast<std::uint32_t>(result);
            set_nz(state, destination);
        } else {
            destination %= source;
            set_logic_z(state, destination);
        }
        state.pc = next_pc;
        state.cycles += signed_modulo ? 40u : 35u;
        return BlockExit::Dispatch;
    }

    // ADDC/SUBB consume the incoming carry flag as an extra operand.
    if ((opcode & 0xfe00) == 0x4200 || (opcode & 0xfe00) == 0x4600) {
        const unsigned source_index = (opcode >> 5) & 15;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t source = reg(state, bank ? Bank::B : Bank::A,
                                         source_index);
        const std::uint32_t carry = (state.st & flag_c) ? 1u : 0u;
        state.st &= ~(flag_n | flag_c | flag_z | flag_v);
        if ((opcode & 0xfe00) == 0x4200) {
            const std::uint32_t lhs = destination;
            destination += source + carry;
            set_add_flags(state, lhs, source, destination);
        } else {
            const std::uint32_t lhs = destination;
            destination -= source + carry;
            set_sub_flags(state, lhs, source, destination);
        }
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // Rotate-left through the register value; K=0 is a no-op with Z updated.
    if ((opcode & 0xfc00) == 0x3000) {
        const unsigned amount = (opcode >> 5) & 31;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        state.st &= ~(flag_c | flag_z);
        if (amount) {
            const std::uint32_t carry_source = destination << (amount - 1);
            if (carry_source & 0x80000000u) state.st |= flag_c;
            destination = (destination << amount) |
                         (destination >> ((32u - amount) & 31u));
        }
        if (!destination) state.st |= flag_z;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // RETI pops the saved status word and PC as 32-bit fields from the bit
    // addressed stack. The field widths configured in ST do not apply here.
    if ((opcode & 0xfff0) == 0x0940) {
        std::uint32_t restored_st = 0;
        std::uint32_t restored_pc = 0;
        const std::uint32_t full_width_st = state.st & ~0x1fu;
        if (!read_field(bus, state.sp, full_width_st, 0, restored_st) ||
            !read_field(bus, state.sp + 32u, full_width_st, 0, restored_pc))
            return BlockExit::BusFault;
        state.sp += 64u;
        state.st = restored_st;
        state.pc = restored_pc & 0xfffffff0u;
        state.cycles += 11;
        return BlockExit::Dispatch;
    }

    // MOVE0/1 (Rs)+,Rd. This is the TMS34010's 0x9400-0x97ff family;
    // 0x9404, which initially looked like RETI, is this instruction.
    if ((opcode & 0xfc00) == 0x9400) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const unsigned field = (opcode & 0x0200) ? 1u : 0u;
        std::uint32_t& address = reg(state, bank ? Bank::B : Bank::A, source);
        std::uint32_t value = 0;
        if (!read_field(bus, address, state.st, field, value))
            return BlockExit::BusFault;
        address += field_increment(state.st, field);
        auto& target = reg(state, bank ? Bank::B : Bank::A, destination);
        state.st &= ~(flag_n | flag_z | flag_v);
        target = value;
        set_nz(state, target);
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }

    // MOVE0/1 (Rs),Rd. Load a field from the address held in Rs without
    // changing that address register (MAME's MOVE_NR handlers).
    if ((opcode & 0xfc00) == 0x8400) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const unsigned field = (opcode & 0x0200) ? 1u : 0u;
        const std::uint32_t address = reg(state, bank ? Bank::B : Bank::A,
                                          source);
        std::uint32_t value = 0;
        if (!read_field(bus, address, state.st, field, value))
            return BlockExit::BusFault;
        auto& target = reg(state, bank ? Bank::B : Bank::A, destination);
        state.st &= ~(flag_n | flag_z | flag_v);
        target = value;
        set_nz(state, target);
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }

    // MOVB (Rs),Rd. The byte address remains bit-addressed in TMS34010
    // space; the loaded signed byte is extended to 32 bits and updates N/Z/V.
    // MAME maps both A/B bank variants across opcode range 0x8e00-0x8fff.
    if ((opcode & 0xfe00) == 0x8e00) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        std::uint8_t byte = 0;
        const auto address = reg(state, bank ? Bank::B : Bank::A, source);
        if (!read_byte(bus, address, byte)) return BlockExit::BusFault;
        auto& target = reg(state, bank ? Bank::B : Bank::A, destination);
        target = static_cast<std::uint32_t>(static_cast<std::int32_t>(
            static_cast<std::int8_t>(byte)));
        state.st &= ~(flag_n | flag_z | flag_v);
        set_nz(state, target);
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }

    // ADDK/SUBK use a compact five-bit bit-count (zero encodes 32).
    const std::uint16_t compact_alu = opcode & 0xfe00;
    if (compact_alu == 0x1000 || compact_alu == 0x1200 ||
        compact_alu == 0x1400 || compact_alu == 0x1600) {
        unsigned amount = (opcode >> 5) & 31;
        if (!amount) amount = 32;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t lhs = destination;
        const std::uint32_t rhs = amount;
        if (compact_alu == 0x1000 || compact_alu == 0x1200) {
            destination += rhs;
            set_add_flags(state, lhs, rhs, destination);
        } else {
            destination -= rhs;
            set_sub_flags(state, lhs, rhs, destination);
        }
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // SLLK is encoded by the 0x2400/0x2500 slots; unlike ADDK, K=0
    // means a zero-bit shift.
    if ((opcode & 0xfc00) == 0x2400) {
        const unsigned amount = (opcode >> 5) & 31;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t original = destination;
        state.st &= ~(flag_c | flag_z);
        if (amount) {
            if ((original >> (32u - amount)) & 1u) state.st |= flag_c;
            destination = original << amount;
        }
        if (!destination) state.st |= flag_z;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // Complete the documented immediate shift families (SLA/SRA). The K
    // operand is unsigned for left shifts and negated for right shifts.
    if ((opcode & 0xfc00) == 0x2000 || (opcode & 0xfc00) == 0x2800) {
        const bool arithmetic_left = (opcode & 0xfc00) == 0x2000;
        const unsigned encoded = (opcode >> 5) & 31;
        const unsigned amount = arithmetic_left
            ? encoded : ((-static_cast<int>(encoded)) & 31);
        auto& destination = reg(state, bank ? Bank::B : Bank::A, reg_index);
        std::uint32_t result = destination;
        state.st &= arithmetic_left ? ~(flag_n | flag_c | flag_z | flag_v)
                                    : ~(flag_n | flag_c | flag_z);
        if (amount) {
            if (arithmetic_left) {
                const std::uint32_t mask =
                    (0xffffffffu << (31u - amount)) & 0x7fffffffu;
                const std::uint32_t sign_adjusted =
                    (result & 0x80000000u) ? result ^ mask : result;
                if (sign_adjusted & mask) state.st |= flag_v;
                if (result & (1u << (32u - amount))) state.st |= flag_c;
                result <<= amount;
            } else {
                if ((result >> (amount - 1u)) & 1u) state.st |= flag_c;
                const bool negative = (result & 0x80000000u) != 0;
                result >>= amount;
                if (negative) result |= 0xffffffffu << (32u - amount);
            }
            destination = result;
        }
        set_nz(state, result);
        state.pc = next_pc;
        state.cycles += arithmetic_left ? 3u : 1u;
        return BlockExit::Dispatch;
    }

    // SRLK uses the negated five-bit operand as its right-shift count.
    if ((opcode & 0xfc00) == 0x2c00) {
        const unsigned encoded = (opcode >> 5) & 31;
        const unsigned amount = (-static_cast<int>(encoded)) & 31;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t original = destination;
        state.st &= ~(flag_c | flag_z);
        if (amount) {
            if ((original >> (amount - 1)) & 1u) state.st |= flag_c;
            destination = original >> amount;
        }
        if (!destination) state.st |= flag_z;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // SLL R shifts Rd left by the low five bits of Rs.
    if ((opcode & 0xfe00) == 0x6200) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned amount = reg(state, bank ? Bank::B : Bank::A, source) & 31;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t original = destination;
        state.st &= ~(flag_c | flag_z);
        if (amount) {
            if ((original >> (32u - amount)) & 1u) state.st |= flag_c;
            destination = original << amount;
        }
        if (!destination) state.st |= flag_z;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    if ((opcode & 0xfe00) == 0x6000 || (opcode & 0xfe00) == 0x6400 ||
        (opcode & 0xfe00) == 0x6600 || (opcode & 0xfe00) == 0x6800) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned amount = (opcode & 0xfe00) == 0x6000
            ? reg(state, bank ? Bank::B : Bank::A, source) & 31u
            : (opcode & 0xfe00) == 0x6800
                ? reg(state, bank ? Bank::B : Bank::A, source) & 31u
                : (-static_cast<std::int32_t>(
                    reg(state, bank ? Bank::B : Bank::A, source))) & 31u;
        auto& destination = reg(state, bank ? Bank::B : Bank::A, reg_index);
        std::uint32_t result = destination;
        const auto family = opcode & 0xfe00;
        if (family == 0x6000)
            state.st &= ~(flag_n | flag_c | flag_z | flag_v);
        else if (family == 0x6400)
            state.st &= ~(flag_n | flag_c | flag_z);
        else
            state.st &= ~(flag_c | flag_z);
        if (amount) {
            if (family == 0x6000) { // SLA R
                const std::uint32_t mask =
                    (0xffffffffu << (31u - amount)) & 0x7fffffffu;
                const std::uint32_t sign_adjusted =
                    (result & 0x80000000u) ? result ^ mask : result;
                if (sign_adjusted & mask) state.st |= flag_v;
                if (result & (1u << (32u - amount))) state.st |= flag_c;
                result <<= amount;
            } else if (family == 0x6200) { // SLL R
                if ((result >> (32u - amount)) & 1u) state.st |= flag_c;
                result <<= amount;
            } else if (family == 0x6400) { // SRA R
                if ((result >> (amount - 1u)) & 1u) state.st |= flag_c;
                const bool negative = (result & 0x80000000u) != 0;
                result >>= amount;
                if (negative) result |= 0xffffffffu << (32u - amount);
            } else if (family == 0x6600) { // SRL R
                if ((result >> (amount - 1u)) & 1u) state.st |= flag_c;
                result >>= amount;
            } else { // RL R
                if ((result >> (32u - amount)) & 1u) state.st |= flag_c;
                result = (result << amount) | (result >> (32u - amount));
            }
            destination = result;
        }
        if (family == 0x6000 || family == 0x6400) {
            if (result & 0x80000000u) state.st |= flag_n;
        }
        if (!result) state.st |= flag_z;
        state.pc = next_pc;
        state.cycles += family == 0x6000 ? 3u : 1u;
        return BlockExit::Dispatch;
    }

    // BTST R tests the selected destination bit and changes only Z.
    if ((opcode & 0xfe00) == 0x4a00) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned bit = reg(state, bank ? Bank::B : Bank::A, source) & 31;
        const std::uint32_t destination =
            reg(state, bank ? Bank::B : Bank::A, reg_index);
        state.st &= ~flag_z;
        if (!(destination & (1u << bit))) state.st |= flag_z;
        state.pc = next_pc;
        state.cycles += 2;
        return BlockExit::Dispatch;
    }

    // XY register arithmetic operates on two independent signed 16-bit
    // coordinates packed into each 32-bit register. The flag behavior below
    // follows the corresponding MAME ADD_XY/SUB_XY/CMP_XY macros.
    if ((opcode & 0xfe00) == 0xe000 ||
        (opcode & 0xfe00) == 0xe200 ||
        (opcode & 0xfe00) == 0xe400) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const auto src = reg(state, bank ? Bank::B : Bank::A, source);
        auto& dst = reg(state, bank ? Bank::B : Bank::A, destination);
        const auto sx = static_cast<std::int16_t>(src);
        const auto sy = static_cast<std::int16_t>(src >> 16);
        const auto dx = static_cast<std::int16_t>(dst);
        const auto dy = static_cast<std::int16_t>(dst >> 16);
        const auto rx = static_cast<std::int16_t>(
            (opcode & 0xfe00) == 0xe000 ? dx + sx : dx - sx);
        const auto ry = static_cast<std::int16_t>(
            (opcode & 0xfe00) == 0xe000 ? dy + sy : dy - sy);
        state.st &= ~(flag_n | flag_c | flag_z | flag_v);
        if ((opcode & 0xfe00) == 0xe000) {
            if (rx == 0) state.st |= flag_n;
            if (static_cast<std::uint16_t>(ry) & 0x8000u) state.st |= flag_c;
            if (ry == 0) state.st |= flag_z;
            if (static_cast<std::uint16_t>(rx) & 0x8000u) state.st |= flag_v;
            dst = static_cast<std::uint16_t>(rx) |
                  (static_cast<std::uint32_t>(static_cast<std::uint16_t>(ry)) << 16);
        } else if ((opcode & 0xfe00) == 0xe200) {
            if (sx == dx) state.st |= flag_n;
            if (sy > dy) state.st |= flag_c;
            if (sy == dy) state.st |= flag_z;
            if (sx > dx) state.st |= flag_v;
            dst = static_cast<std::uint16_t>(dx - sx) |
                  (static_cast<std::uint32_t>(static_cast<std::uint16_t>(dy - sy)) << 16);
        } else {
            const auto diff_x = static_cast<std::int16_t>(dx - sx);
            const auto diff_y = static_cast<std::int16_t>(dy - sy);
            if (diff_x == 0) state.st |= flag_n;
            if (static_cast<std::uint16_t>(diff_y) & 0x8000u) state.st |= flag_c;
            if (diff_y == 0) state.st |= flag_z;
            if (static_cast<std::uint16_t>(diff_x) & 0x8000u) state.st |= flag_v;
        }
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // XY register operations and coordinate conversion. These opcodes are
    // implemented by the TMS34010 core in MAME's e600/e800/ec00/ee00 rows.
    if ((opcode & 0xfe00) == 0xe600) { // CPW Rs,Rd
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const auto xy = reg(state, bank ? Bank::B : Bank::A, source);
        const auto x = static_cast<std::int16_t>(xy);
        const auto y = static_cast<std::int16_t>(xy >> 16);
        const auto left = static_cast<std::int16_t>(state.b[5]);
        const auto top_edge = static_cast<std::int16_t>(state.b[5] >> 16);
        const auto right = static_cast<std::int16_t>(state.b[6]);
        const auto bottom = static_cast<std::int16_t>(state.b[6] >> 16);
        std::uint32_t result = 0;
        if (x < left) result |= 0x20;
        if (x > right) result |= 0x40;
        if (y < top_edge) result |= 0x80;
        if (y > bottom) result |= 0x100;
        reg(state, bank ? Bank::B : Bank::A, destination) = result;
        state.st &= ~flag_v;
        if (result) state.st |= flag_v;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0xe800) { // CVXYL Rs,Rd
        if (!bus.convert_dst_xy) return BlockExit::BusFault;
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        std::uint32_t linear = 0;
        if (!bus.convert_dst_xy(bus.opaque,
                reg(state, bank ? Bank::B : Bank::A, source), state.b[4],
                &linear))
            return BlockExit::BusFault;
        reg(state, bank ? Bank::B : Bank::A, destination) = linear;
        state.pc = next_pc;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0xec00) { // MOVX Rs,Rd
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        auto& dst = reg(state, bank ? Bank::B : Bank::A, destination);
        const auto src = reg(state, bank ? Bank::B : Bank::A, source);
        dst = (dst & 0xffff0000u) | (src & 0xffffu);
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0xee00) { // MOVY Rs,Rd
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        auto& dst = reg(state, bank ? Bank::B : Bank::A, destination);
        const auto src = reg(state, bank ? Bank::B : Bank::A, source);
        dst = (src & 0xffff0000u) | (dst & 0xffffu);
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    // DIVU handles a single register for odd Rd and an Rd:Rd+1 dividend for
    // even Rd. On divide-by-zero/overflow, MAME preserves the operands and
    // sets V; this is the operation used at 0x5b6d in the current trace.
    if ((opcode & 0xfe00) == 0x5a00) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination_index = reg_index;
        const std::uint32_t divisor =
            reg(state, bank ? Bank::B : Bank::A, source);
        std::uint32_t& quotient = reg(state, bank ? Bank::B : Bank::A,
                                      destination_index);
        state.st &= ~(flag_z | flag_v);
        if (!divisor) {
            state.st |= flag_v;
        } else if (destination_index & 1u) {
            quotient /= divisor;
            if (!quotient) state.st |= flag_z;
        } else {
            std::uint32_t& remainder = reg(state, bank ? Bank::B : Bank::A,
                                            (destination_index + 1) & 15);
            const std::uint64_t dividend =
                (static_cast<std::uint64_t>(quotient) << 32) | remainder;
            const std::uint64_t result = dividend / divisor;
            if (result > 0xffffffffu) {
                state.st |= flag_v;
            } else {
                remainder = static_cast<std::uint32_t>(dividend % divisor);
                quotient = static_cast<std::uint32_t>(result);
                if (!quotient) state.st |= flag_z;
            }
        }
        state.pc = next_pc;
        state.cycles += 37;
        return BlockExit::Dispatch;
    }

    // DSJS decrements Rd and, while nonzero, skips K words forward or back.
    if ((opcode & 0xf800) == 0x3800) {
        std::uint32_t& counter = reg(state, bank ? Bank::B : Bank::A,
                                     reg_index);
        --counter;
        const unsigned displacement = (opcode >> 5) & 31;
        const std::int32_t signed_words = (opcode & 0x0400)
            ? -static_cast<std::int32_t>(displacement)
            : static_cast<std::int32_t>(displacement);
        if (counter) {
            state.pc = next_pc + static_cast<std::uint32_t>(signed_words * 16);
            state.cycles += 2;
        } else {
            state.pc = next_pc;
            state.cycles += 3;
        }
        return BlockExit::Dispatch;
    }

    // CALLA pushes the address after its 32-bit target operand and transfers
    // to that bit address. Its immediate target is read without advancing PC.
    if ((opcode & 0xfff0) == 0x0d50) {
        std::uint32_t target = 0;
        if (!read_long(bus, next_pc, target)) return BlockExit::BusFault;
        state.sp -= 32u;
        if (!write_long(bus, state.sp, state.pc + 48u))
            return BlockExit::BusFault;
        state.pc = target & 0xfffffff0u;
        state.cycles += 4;
        return BlockExit::Dispatch;
    }

    // CALLR fetches a signed word displacement without advancing the operand
    // PC, pushes the following instruction address, then branches relative.
    if ((opcode & 0xfff0) == 0x0d30) {
        const auto displacement = static_cast<std::int16_t>(
            bus.read16(bus.opaque, next_pc));
        state.sp -= 32u;
        if (!write_long(bus, state.sp, next_pc + 16u))
            return BlockExit::BusFault;
        state.pc = (next_pc + 16u +
                    static_cast<std::int32_t>(displacement) * 16) &
                   0xfffffff0u;
        state.cycles += 3;
        return BlockExit::Dispatch;
    }

    // MMTM transfers the selected register mask to descending memory and
    // updates the address register (MAME's 0x0980/0x0990 slots).
    if ((opcode & 0xffe0) == 0x0980) {
        const std::uint16_t mask = bus.read16(bus.opaque, next_pc);
        std::uint32_t& address = reg(state, bank ? Bank::B : Bank::A,
                                     reg_index);
        unsigned selected = 0;
        for (unsigned i = 0; i < 16; ++i) {
            if (mask & (0x8000u >> i)) {
                address -= 32u;
                if (!write_long(bus, address,
                                reg(state, bank ? Bank::B : Bank::A, i)))
                    return BlockExit::BusFault;
                ++selected;
            }
        }
        state.pc += 32u;
        state.cycles += 2u + 4u * selected;
        return BlockExit::Dispatch;
    }

    // MMFM is the matching ascending-memory load form (0x09a0/0x09b0).
    if ((opcode & 0xffe0) == 0x09a0) {
        const std::uint16_t mask = bus.read16(bus.opaque, next_pc);
        std::uint32_t& address = reg(state, bank ? Bank::B : Bank::A,
                                     reg_index);
        unsigned selected = 0;
        for (int i = 15; i >= 0; --i) {
            if (mask & (0x8000u >> (15 - i))) {
                std::uint32_t value = 0;
                if (!read_long(bus, address, value)) return BlockExit::BusFault;
                reg(state, bank ? Bank::B : Bank::A,
                    static_cast<unsigned>(i)) = value;
                address += 32u;
                ++selected;
            }
        }
        state.pc += 32u;
        state.cycles += 3u + 4u * selected;
        return BlockExit::Dispatch;
    }

    // RETS pops a return PC and discards the encoded stack-frame bit count.
    if ((opcode & 0xffe0) == 0x0960) {
        std::uint32_t target = 0;
        if (!read_long(bus, state.sp, target)) return BlockExit::BusFault;
        state.sp += 32u + ((opcode & 0x1fu) << 4);
        state.pc = target & 0xfffffff0u;
        state.cycles += 7;
        return BlockExit::Dispatch;
    }

    // MOVE0/1 Rs,-(Rd) stores a field after predecrementing Rd.
    if ((opcode & 0xfc00) == 0xa000) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const unsigned field = (opcode & 0x0200) ? 1u : 0u;
        std::uint32_t& address = reg(state, bank ? Bank::B : Bank::A,
                                     destination);
        address -= field_increment(state.st, field);
        const std::uint32_t value = reg(state, bank ? Bank::B : Bank::A,
                                        source);
        if (!write_field(bus, address, value, state.st, field))
            return BlockExit::BusFault;
        state.pc = next_pc;
        state.cycles += 2;
        return BlockExit::Dispatch;
    }

    // SETF0/SETF1 occupy slots 0x52-0x57 and 0x72-0x77. Their immediate
    // field widths are encoded in the low six bits.
    const std::uint16_t setf_group = opcode & 0xff80;
    if ((setf_group == 0x0500 || setf_group == 0x0700) &&
        ((opcode >> 4) & 7) >= 2 &&
        ((opcode >> 4) & 7) <= 7) {
        const unsigned field_shift = setf_group == 0x0700 ? 6u : 0u;
        const std::uint32_t field_mask = 0x3fu << field_shift;
        state.st = (state.st & ~field_mask) |
                   ((opcode & 0x3fu) << field_shift);
        state.cycles += field_shift ? 2u : 1u;
        state.pc = next_pc;
        return BlockExit::Dispatch;
    }

    if ((opcode & 0xfe00) == 0x0800 &&
        (sub == 0x01c0 || sub == 0x01e0)) {
        std::uint32_t value = 0;
        if (sub == 0x01c0) {
            const auto word = static_cast<std::int16_t>(
                bus.read16(bus.opaque, next_pc));
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(word));
            state.pc += 32;
            state.cycles += 2;
        } else {
            if (!read_long(bus, next_pc, value)) return BlockExit::BusFault;
            state.pc += 48;
            state.cycles += 3;
        }
        reg(state, bank ? Bank::B : Bank::A, reg_index) = value;
        state.st &= ~(flag_n | flag_z | flag_v);
        set_nz(state, value);
        return BlockExit::Dispatch;
    }
    if ((opcode & 0xfe00) == 0x1800 ||
        (opcode & 0xfe00) == 0x1a00) {
        unsigned immediate = (opcode >> 5) & 31;
        if (!immediate) immediate = 32;
        reg(state, bank ? Bank::B : Bank::A, reg_index) = immediate;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }

    if ((opcode & 0xf000) == 0xc000) {
        const unsigned cc = (opcode >> 8) & 15;
        const unsigned low = opcode & 0xff;
        std::uint32_t fallthrough = next_pc;
        std::uint32_t target = next_pc;
        unsigned taken_cycles = 2;
        unsigned untaken_cycles = 1;
        if (low == 0) {
            const auto displacement = static_cast<std::int16_t>(
                bus.read16(bus.opaque, next_pc));
            fallthrough += 16;
            target = fallthrough + static_cast<std::int32_t>(displacement) * 16;
            taken_cycles = 3;
            untaken_cycles = 2;
        } else if (low == 0x80) {
            if (!read_long(bus, next_pc, target)) return BlockExit::BusFault;
            fallthrough += 32;
            taken_cycles = 3;
            untaken_cycles = 4;
        } else {
            const auto displacement = static_cast<std::int8_t>(low);
            target = fallthrough + static_cast<std::int32_t>(displacement) * 16;
        }
        const bool taken = condition(state.st, cc);
        state.pc = (taken ? target : fallthrough) & 0xfffffff0u;
        state.cycles += taken ? taken_cycles : untaken_cycles;
        return BlockExit::Dispatch;
    }
    if (((opcode & 0xfe00) == 0x0400 ||
         (opcode & 0xfe00) == 0x0600) &&
        ((opcode & 0x01e0) == 0x0180 ||
         (opcode & 0x01e0) == 0x01a0)) {
        std::uint32_t absolute = 0;
        if (!read_long(bus, next_pc, absolute)) return BlockExit::BusFault;
        const unsigned field = (opcode & 0xfe00) == 0x0600 ? 1u : 0u;
        if ((opcode & 0x01e0) == 0x0180) {
            const std::uint32_t value = reg(state, bank ? Bank::B : Bank::A,
                                            reg_index);
            if (!write_field(bus, absolute, value, state.st, field))
                return BlockExit::BusFault;
            state.pc += 48;
            state.cycles += 3;
        } else {
            std::uint32_t value = 0;
            if (!read_field(bus, absolute, state.st, field, value))
                return BlockExit::BusFault;
            reg(state, bank ? Bank::B : Bank::A, reg_index) = value;
            state.st &= ~(flag_n | flag_z | flag_v);
            set_nz(state, value);
            state.pc += 48;
            state.cycles += 5;
        }
        return BlockExit::Dispatch;
    }
    const std::uint16_t top = opcode & 0xfe00;
    // 0x0a00/0x0b00 share this masked pattern; the preceding MAME slot check
    // routes the illegal 0x0a00 half to its exception vector.
    if (top == 0x0a00) {
        std::uint32_t immediate = 0;
        const std::uint32_t address = next_pc;
        const bool long_form = sub == 0x0120 || sub == 0x0160 ||
                               sub == 0x0180 || sub == 0x01a0 ||
                               sub == 0x01c0;
        if (long_form) {
            if (!read_long(bus, address, immediate)) return BlockExit::BusFault;
        } else {
            const auto word = static_cast<std::int16_t>(
                bus.read16(bus.opaque, address));
            immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(word));
        }
        if (sub == 0x0180) immediate = ~immediate;
        std::uint32_t& destination = reg(state, bank ? Bank::B : Bank::A,
                                         reg_index);
        const std::uint32_t lhs = destination;
        if (sub == 0x0100 || sub == 0x0120) {
            destination += immediate;
            set_add_flags(state, lhs, immediate, destination);
        } else if (sub == 0x0140 || sub == 0x0160) {
            set_sub_flags(state, lhs, immediate, lhs - immediate);
        } else if (sub == 0x01e0) {
            immediate = ~immediate;
            destination -= immediate;
            set_sub_flags(state, lhs, immediate, destination);
        } else if (sub == 0x0180) {
            destination &= immediate;
            set_logic_z(state, destination);
        } else if (sub == 0x01a0) {
            destination |= immediate;
            set_logic_z(state, destination);
        } else if (sub == 0x01c0) {
            destination ^= immediate;
            set_logic_z(state, destination);
        } else {
            return BlockExit::NeedsInterpreter;
        }
        if (sub == 0x0140 || sub == 0x0160)
            destination = lhs;
        state.pc += long_form ? 48u : 32u;
        state.cycles += long_form ? 3u : 2u;
        return BlockExit::Dispatch;
    }
    if (top == 0xb400 || top == 0xb600) {
        const std::uint16_t raw_displacement =
            bus.read16(bus.opaque, next_pc);
        const auto displacement = static_cast<std::int16_t>(raw_displacement);
        const std::uint32_t base = reg(state, bank ? Bank::B : Bank::A,
                                       (opcode >> 5) & 15);
        const unsigned destination = opcode & 15;
        std::uint32_t value = 0;
        const unsigned field = top == 0xb600 ? 1u : 0u;
        if (!read_field(bus, base + static_cast<std::uint32_t>(displacement),
                        state.st, field, value))
            return BlockExit::BusFault;
        state.st &= ~(flag_n | flag_z | flag_v);
        reg(state, bank ? Bank::B : Bank::A, destination) = value;
        set_nz(state, value);
        state.pc += 32;
        state.cycles += 5;
        return BlockExit::Dispatch;
    }
    if (top == 0x4c00 || top == 0x4e00 || top == 0x4f00) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const std::uint32_t value = reg(state, bank ? Bank::B : Bank::A,
                                        source);
        if (top == 0x4c00) {
            reg(state, bank ? Bank::B : Bank::A, destination) = value;
            state.st &= ~(flag_n | flag_z | flag_v);
            set_nz(state, value);
        } else {
            reg(state, bank ? Bank::A : Bank::B, destination) = value;
            state.st &= ~(flag_n | flag_z | flag_v);
            set_nz(state, value);
        }
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if (top == 0x4000 || top == 0x4400 || top == 0x4800) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        const std::uint32_t lhs = reg(state, bank ? Bank::B : Bank::A,
                                      destination);
        const std::uint32_t rhs = reg(state, bank ? Bank::B : Bank::A, source);
        const std::uint32_t result = top == 0x4000 ? lhs + rhs : lhs - rhs;
        if (top == 0x4000)
            set_add_flags(state, lhs, rhs, result);
        else
            set_sub_flags(state, lhs, rhs, result);
        if (top != 0x4800)
            reg(state, bank ? Bank::B : Bank::A, destination) = result;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if (top == 0x5000 || top == 0x5200 || top == 0x5400 || top == 0x5600) {
        const unsigned source = (opcode >> 5) & 15;
        const unsigned destination = opcode & 15;
        std::uint32_t& dst = reg(state, bank ? Bank::B : Bank::A, destination);
        const std::uint32_t rhs = reg(state, bank ? Bank::B : Bank::A, source);
        if (top == 0x5000) dst &= rhs;
        else if (top == 0x5200) dst &= ~rhs;
        else if (top == 0x5400) dst |= rhs;
        else dst ^= rhs;
        set_logic_z(state, dst);
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    if (top == 0x6a00) {
        const unsigned source = (opcode >> 5) & 15;
        const std::uint32_t value = reg(state, bank ? Bank::B : Bank::A, source);
        std::uint32_t count = 0;
        state.st &= ~flag_z;
        if (!value) state.st |= flag_z;
        else {
            std::uint32_t shifted = value;
            while (!(shifted & flag_n)) {
                shifted <<= 1;
                ++count;
            }
        }
        reg(state, bank ? Bank::B : Bank::A, reg_index) = count;
        state.pc = next_pc;
        ++state.cycles;
        return BlockExit::Dispatch;
    }
    // PIXT transfer forms occupy F000-F5FF and F800-FDFF; F600-F7FF is DRAV.
    const auto graphics_family = opcode & 0xfe00;
    if (graphics_family == 0xf000 || graphics_family == 0xf200 ||
        graphics_family == 0xf400 || graphics_family == 0xf800 ||
        graphics_family == 0xfa00 || graphics_family == 0xfc00) {
        if (pixt(state, bus, opcode)) return BlockExit::Dispatch;
    }
    if (graphics_family == 0xf600) {
        const unsigned graphics_bank = (opcode & 0x10) ? 1u : 0u;
        if (drav(state, bus, graphics_bank, (opcode >> 5) & 15,
                 opcode & 15)) {
            state.pc = next_pc;
            return BlockExit::Dispatch;
        }
    }
    if ((opcode & 0xfff0) == 0x0f20) {
        if (pixblt_l_xy(state, bus)) return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0fa0) {
        if (pixblt_b_xy(state, bus)) return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0fe0) {
        if (fill_xy(state, bus)) return BlockExit::Dispatch;
    }
    if ((opcode & 0xfff0) == 0x0f20) {
        if (pixblt_l_xy(state, bus)) return BlockExit::Dispatch;
    }
    return BlockExit::NeedsInterpreter;
}

MamerecompFallbackReason classify_unsupported_opcode(
    std::uint16_t opcode) noexcept {
    // A generated interpreter stub is emitted when static decode cannot find
    // a native emitter for this opcode.
    (void)opcode;
    return MAMERECOMP_UNSUPPORTED_OPCODE;
}

namespace {
const char* fallback_instruction_name(std::uint16_t opcode) noexcept {
    const auto graphics_family = opcode & 0xfe00;
    if (graphics_family == 0xf000) return "PIXT_R_I_XY";
    if (graphics_family == 0xf200) return "PIXT_XY_R";
    if (graphics_family == 0xf400) return "PIXT_XY_XY";
    if (graphics_family == 0xf600) return "DRAV";
    if (graphics_family == 0xf800) return "PIXT_R_I_R";
    if (graphics_family == 0xfa00) return "PIXT_R_R_I";
    if (graphics_family == 0xfc00) return "PIXT_R_R";
    if ((opcode & 0xfff0) == 0x0f20) return "PIXBLT_L_XY";
    if ((opcode & 0xfff0) == 0x0fe0) return "FILL_XY";
    if ((opcode & 0xfff0) == 0x0300) return "NOP";
    if ((opcode & 0xfff0) == 0x03e0) return "NOT";
    if ((opcode & 0xf000) == 0xc000) return "JUMP_CONDITIONAL";
    if ((opcode & 0xfe00) == 0x1000) return "ADDK";
    if ((opcode & 0xfe00) == 0x1400) return "SUBK";
    if ((opcode & 0xfc00) == 0xf000) return "PIXEL_GRAPHICS";
    if ((opcode & 0xfe00) == 0xe600) return "CPW";
    if ((opcode & 0xfe00) == 0xe800) return "CVXYL";
    if ((opcode & 0xfe00) == 0xec00) return "MOVX";
    if ((opcode & 0xfe00) == 0xee00) return "MOVY";
    if ((opcode & 0xfe00) == 0xe000) return "ADD_XY";
    if ((opcode & 0xfe00) == 0xe200) return "SUB_XY";
    if ((opcode & 0xfe00) == 0xe400) return "CMP_XY";
    if ((opcode & 0xfe00) == 0x8400) return "MOVE_FIELD";
    if ((opcode & 0xfe00) == 0x9400) return "MOVE_FIELD_POSTINCREMENT";
    if ((opcode & 0xfc00) == 0xa000) return "MOVE_FIELD_PREDECREMENT";
    if ((opcode & 0xf000) == 0xb000) return "MOVE_FIELD_DISPLACEMENT";
    if ((opcode & 0xf000) == 0x5000) return "LOGIC_REGISTER";
    if ((opcode & 0xf000) == 0x4000) return "ARITHMETIC_REGISTER";
    return "TMS34010_OPCODE";
}

bool is_exception_opcode(std::uint16_t opcode) noexcept {
    const unsigned slot = opcode >> 4;
    return (slot >= 0x020 && slot <= 0x02f) ||
           (slot >= 0x040 && slot <= 0x04f) ||
           (slot >= 0x080 && slot <= 0x08f) ||
           (slot >= 0x0a0 && slot <= 0x0af) ||
           (slot >= 0x0c0 && slot <= 0x0cf) ||
           (slot >= 0x0e0 && slot <= 0x0ef) ||
           (slot >= 0x340 && slot <= 0x37f) ||
           (slot >= 0x700 && slot <= 0x7ff) ||
           (slot >= 0x9e0 && slot <= 0x9ff) ||
           (slot >= 0xbe0 && slot <= 0xbff) ||
           (slot >= 0xd80 && slot <= 0xdef) ||
           (slot >= 0xfe0 && slot <= 0xfff);
}

bool is_unimplemented_opcode(std::uint16_t opcode) noexcept {
    const unsigned slot = opcode >> 4;
    return slot == 0x000 || slot == 0x001 ||
           (slot >= 0x004 && slot <= 0x00f) || slot == 0x011 ||
           slot == 0x01d || slot == 0x01f || slot == 0x031 ||
           slot == 0x033 || slot == 0x035 || slot == 0x037 ||
           slot == 0x05d || (slot >= 0x060 && slot <= 0x06f) ||
           slot == 0x07d || slot == 0x095 || slot == 0x0d2 ||
           slot == 0x0d4 || slot == 0x0d7 || slot == 0x0df ||
           slot == 0x0f1 || slot == 0x0f3 || slot == 0x0f5 ||
           slot == 0x0f7 || slot == 0x0f9 || slot == 0x0fb ||
           slot == 0x0fd || slot == 0x0ff ||
           (slot >= 0xd42 && slot <= 0xd4f) ||
           (slot >= 0xd52 && slot <= 0xd5f) ||
           (slot >= 0xd62 && slot <= 0xd6f) ||
           (slot >= 0xd72 && slot <= 0xd7f) || slot == 0xdf0 ||
           (slot >= 0xdf2 && slot <= 0xdf8) ||
           (slot >= 0xdfa && slot <= 0xdff) ||
           (slot >= 0xea0 && slot <= 0xebf);
}

bool is_trap_opcode(std::uint16_t opcode) noexcept {
    return (opcode & 0xffe0) == 0x0900;
}

bool is_known_but_unhandled_variant(std::uint16_t opcode) noexcept {
    const unsigned top = opcode & 0xfe00;
    const unsigned sub = opcode & 0x01e0;
    if (top != 0x0a00) return false;
    return sub != 0x0100 && sub != 0x0120 && sub != 0x0140 &&
           sub != 0x0160 && sub != 0x0180 && sub != 0x01a0 &&
           sub != 0x01c0 && sub != 0x01e0;
}
} // namespace

bool opcode_causes_exception(std::uint16_t opcode) noexcept {
    return is_exception_opcode(opcode) || is_trap_opcode(opcode);
}

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle) noexcept {
    return interpreter_step(state, bus, stop_cycle,
                           MAMERECOMP_UNSUPPORTED_OPCODE);
}

BlockExit interpreter_step(State& state, Bus& bus,
                           std::uint64_t stop_cycle,
                           MamerecompFallbackReason reason) noexcept {
    if (!bus.read16) return BlockExit::BusFault;
    const std::uint32_t pc = state.pc;
    const std::uint16_t opcode = bus.read16(bus.opaque, pc);
    const std::uint64_t cycles = state.cycles;
    BlockExit result = execute_mame_interpreter(state, bus, stop_cycle);
    const bool used_mame_backend = result != BlockExit::NeedsInterpreter;
    if (!used_mame_backend)
        result = interpreter_step_partial(state, bus, stop_cycle);

    MamerecompFallbackReason recorded_reason = reason;
    if (result == BlockExit::BusFault) {
        recorded_reason = MAMERECOMP_MEMORY_ACCESS;
    } else if (result == BlockExit::NeedsInterpreter) {
        if (is_exception_opcode(opcode))
            recorded_reason = MAMERECOMP_EXCEPTION_BOUNDARY;
        else if (is_known_but_unhandled_variant(opcode))
            recorded_reason = MAMERECOMP_UNSUPPORTED_VARIANT;
        else
            recorded_reason = MAMERECOMP_UNSUPPORTED_OPCODE;
    } else if (result == BlockExit::Dispatch && opcode_causes_exception(opcode)) {
        recorded_reason = MAMERECOMP_EXCEPTION_BOUNDARY;
    }
    const char* backend = used_mame_backend
        ? "MAME-TMS34010-fallback" : "MameRecomp-graphics-fallback";
    mamerecomp_interpreter_trace_step("TMS34010", backend,
        recorded_reason, pc, opcode, 16, cycles, state.cycles, state.pc,
        used_mame_backend ? MameTMS34010::opcode_handler_name(opcode)
                          : fallback_instruction_name(opcode));
    return result;
}

} // namespace mamerecomp::tms34010
