#include "vdp.hpp"

#include <algorithm>
#include <utility>

namespace multiarcaderecomp::chips {

Sega3155313::Sega3155313(Callbacks callbacks) : m_callbacks(std::move(callbacks)) {
    reset();
}

void Sega3155313::reset() {
    m_regs.fill(0);
    m_vram.fill(0);
    m_cram.fill(0);
    m_vsram.fill(0);
    m_internal_sprite_attributes.fill(0);
    m_command_part1 = 0;
    m_command_part2 = 0;
    m_vdp_address = 0;
    m_vdp_code = 0;
    m_vram_fill_length = 0;
    m_scanline = 0;
    m_horizontal_position = 0;
    m_scanline_initialized = false;
    m_irq4_counter = -1;
    m_command_pending = false;
    m_vram_fill_pending = false;
    m_vblank = false;
    m_irq4_pending = false;
    m_irq6_pending = false;
    set_irq(4, false);
    set_irq(6, false);
}

std::uint16_t Sega3155313::read(std::uint32_t byte_offset) {
    switch (byte_offset & 0x1eU) {
    case 0x00:
    case 0x02: return read_data();
    case 0x04:
    case 0x06: return read_status();
    case 0x08:
    case 0x0a:
    case 0x0c:
    case 0x0e: return read_hv_counter();
    default: return 0xffff;
    }
}

void Sega3155313::write(std::uint32_t byte_offset, std::uint16_t data, std::uint8_t lanes) {
    switch (byte_offset & 0x1eU) {
    case 0x00:
    case 0x02:
        if (lanes == 1) data = static_cast<std::uint16_t>((data & 0xffU) * 0x0101U);
        else if (lanes == 2) data = static_cast<std::uint16_t>(((data >> 8) & 0xffU) * 0x0101U);
        write_data(data);
        break;
    case 0x04:
    case 0x06:
        write_control(data);
        break;
    case 0x10:
    case 0x12:
    case 0x14:
    case 0x16:
        if ((lanes & 1U) && m_callbacks.psg_write) m_callbacks.psg_write(static_cast<std::uint8_t>(data));
        break;
    default:
        break;
    }
}

void Sega3155313::set_scanline(unsigned line) {
    const unsigned normalized_line = line % 262U;
    const bool changed = !m_scanline_initialized || normalized_line != m_scanline;
    m_scanline = normalized_line;
    m_horizontal_position = 0;
    m_scanline_initialized = true;
    if (changed) scanline_event();
}

void Sega3155313::set_beam_position(unsigned line, unsigned horizontal_position) {
    m_horizontal_position = std::min(horizontal_position, 480U);
    const unsigned normalized_line = line % 262U;
    if (!m_scanline_initialized || normalized_line != m_scanline) {
        m_scanline = normalized_line;
        m_scanline_initialized = true;
        scanline_event();
    }
}

void Sega3155313::scanline_event() {
    const unsigned line = m_scanline;
    if (line == 0) {
        m_vblank = false;
        m_irq4_counter = m_regs[0x0a];
        m_irq4_pending = false;
        set_irq(4, false);
    }
    if (line == 224) {
        m_vblank = true;
        m_irq6_pending = true;
        set_irq(6, (m_regs[1] & 0x20U) != 0);
    }

    if (line <= 224) {
        if (--m_irq4_counter < 0) {
            m_irq4_counter = m_regs[0x0a];
            m_irq4_pending = true;
            if (m_regs[0] & 0x10U) set_irq(4, true);
        }
    } else {
        m_irq4_counter = m_regs[0x0a];
    }
}

Sega3155313::Scanline Sega3155313::render_scanline(unsigned line) const {
    Scanline output{};
    // Register 1 bit 2 selects the Genesis display mode (MAME ignores writes
    // to Genesis-only registers 11+ when it is clear). Register 0 bit 2 is a
    // special-palette control, not a mode selector. Respect display disable
    // and DISP as separate blanking controls.
    if ((m_regs[1] & 0x04U) == 0 || (m_regs[1] & 0x40U) == 0
        || (m_regs[0] & 0x01U) != 0 || line >= 224U)
        return output;

    const auto dimension = [](unsigned field) {
        constexpr std::array<unsigned, 4> sizes{32, 64, 64, 128};
        return sizes[field & 3U];
    };
    const unsigned plane_width = dimension(m_regs[0x10] & 3U);
    const unsigned plane_height = dimension((m_regs[0x10] >> 4) & 3U);
    const unsigned plane_x_mask = plane_width * 8U - 1U;
    const unsigned plane_y_mask = plane_height * 8U - 1U;
    const unsigned plane_a_base = static_cast<unsigned>(m_regs[2] & 0x38U) << 10;
    const unsigned plane_b_base = static_cast<unsigned>(m_regs[4] & 0x07U) << 13;
    const unsigned window_base = static_cast<unsigned>(m_regs[3] & 0x1eU) << 11;
    const bool h40_mode = (m_regs[0x0c] & 0x01U) != 0;
    const unsigned active_width = h40_mode ? 320U : 256U;
    const unsigned window_width = h40_mode ? 64U : 32U;
    const unsigned sprite_base = static_cast<unsigned>(m_regs[5] & (h40_mode ? 0x7eU : 0x7fU)) << 9;
    const unsigned hscroll_base = static_cast<unsigned>(m_regs[0x0d] & 0x3fU) << 10;
    const unsigned hscroll_mode = m_regs[0x0b] & 3U;
    const bool column_vscroll = (m_regs[0x0b] & 0x04U) != 0;
    const unsigned window_x = static_cast<unsigned>(m_regs[0x11] & 0x1fU) * 16U;
    const unsigned window_y = static_cast<unsigned>(m_regs[0x12] & 0x1fU) * 8U;
    const bool window_right = (m_regs[0x11] & 0x80U) != 0;
    const bool window_down = (m_regs[0x12] & 0x80U) != 0;

    const auto pattern_pen = [&](unsigned code, unsigned x, unsigned y) {
        // Match MAME's md_debug_8x8_layout: each row is four bytes with
        // pixels addressed in nibble order 2,3,0,1,6,7,4,5. VRAM is stored
        // as 16-bit words here, so select the corresponding byte explicitly.
        static constexpr unsigned nibble_order[8] = {2, 3, 0, 1, 6, 7, 4, 5};
        const unsigned nibble = nibble_order[x & 7U];
        const unsigned byte_in_row = nibble >> 1U;
        const unsigned word = (code * 16U + (y & 7U) * 2U + (byte_in_row >> 1U)) & 0x7fffU;
        const unsigned byte = (byte_in_row & 1U) ? (m_vram[word] >> 8U) : (m_vram[word] & 0xffU);
        // MAME's gfx decoder numbers bit 0 as the byte's most significant
        // bit, and assigns plane 0 to the most significant pen bit.
        const unsigned packed = (byte >> ((nibble & 1U) ? 0U : 4U)) & 0x0fU;
        return ((packed & 0x01U) << 3U) | ((packed & 0x02U) << 1U)
            | ((packed & 0x04U) >> 1U) | ((packed & 0x08U) >> 3U);
    };

    std::array<Pixel, 320> low_priority{};
    std::array<Pixel, 320> high_priority{};
    std::array<Pixel, 320> sprite_pixels{};

    const auto hscroll_for = [&](unsigned plane) {
        unsigned row = 0;
        switch (hscroll_mode) {
        case 1: row = (line & 7U); break;       // 8-line scroll table mode.
        case 2: row = (line & ~7U); break;     // Cell scroll.
        case 3: row = line; break;             // Per-scanline scroll.
        default: break;                        // Full-screen scroll.
        }
        const unsigned word_index = ((hscroll_base >> 1) + row * 2U + plane) & 0x7fffU;
        return static_cast<unsigned>(m_vram[word_index] & 0x03ffU);
    };

    const auto draw_plane = [&](unsigned plane, unsigned base, bool is_high_priority) {
        const unsigned hscroll = hscroll_for(plane);
        for (unsigned x = 0; x < output.size(); ++x) {
            if (x >= active_width) continue;
            const bool window_full_width = window_down ? line >= window_y : line < window_y;
            const bool in_window = plane == 0 && (window_full_width
                || (window_right ? x >= window_x : x < window_x));
            unsigned name_index;
            unsigned source_x;
            unsigned source_y;
            if (in_window) {
                source_x = x;
                source_y = line;
                // The window table uses 16-pixel columns (two 8x8 names) but
                // retains an 8-pixel vertical name-table row stride.
                name_index = ((window_base >> 1) + (source_y >> 3) * window_width
                    + (source_x >> 4) * 2U + ((source_x >> 3) & 1U)) & 0x7fffU;
            } else {
                source_x = (x - hscroll) & plane_x_mask;
                const unsigned vscroll_index = column_vscroll
                    ? (((x >> 4) * 2U + plane) & 0x3fU) : plane;
                source_y = (line + m_vsram[vscroll_index]) & plane_y_mask;
                name_index = ((base >> 1) + (source_y >> 3) * plane_width + (source_x >> 3)) & 0x7fffU;
            }
            const std::uint16_t name = m_vram[name_index];
            const bool tile_priority = (name & 0x8000U) != 0;
            if (tile_priority != is_high_priority) continue;

            const unsigned px0 = source_x & 7U;
            const unsigned py0 = source_y & 7U;
            const unsigned px = (name & 0x0800U) ? 7U - px0 : px0;
            const unsigned py = (name & 0x1000U) ? 7U - py0 : py0;
            const unsigned tile_code = name & 0x07ffU;
            const unsigned pen = pattern_pen(tile_code, px, py);
            if (pen == 0) continue;

            auto& pixel = is_high_priority ? high_priority[x] : low_priority[x];
            pixel.color = static_cast<std::uint8_t>((((name >> 13) & 3U) << 4) | pen);
            pixel.opaque = true;
            pixel.high_priority = is_high_priority;
        }
    };

    draw_plane(1, plane_b_base, false);
    draw_plane(0, plane_a_base, false);

    // Sprite attributes are linked through word 1. The first opaque sprite
    // pixel at a screen position wins, matching the VDP's sprite line buffer.
    const unsigned max_sprites = h40_mode ? 80U : 64U;
    unsigned sprite_budget = h40_mode ? 320U : 256U;
    unsigned sprite_index = 0;
    unsigned sprite_mask = 0;
    for (unsigned visited = 0; visited < max_sprites && sprite_budget != 0; ++visited) {
        const unsigned entry = ((sprite_base >> 1) + sprite_index * 4U) & 0x7fffU;
        // The VDP keeps an internal copy of sprite Y and size/link words as
        // they are written to the sprite attribute table. MAME models those
        // fields from that latch while X and pattern attributes remain live
        // VRAM reads; using VRAM for all four words can tear multi-tile sprites
        // during updates (including Moonwalker's hostage/help effect).
        const unsigned sprite_attribute = (sprite_index * 4U) & 0x01ffU;
        const std::uint16_t y_word = m_internal_sprite_attributes[sprite_attribute];
        const std::uint16_t size_link = m_internal_sprite_attributes[(sprite_attribute + 1U) & 0x01ffU];
        const std::uint16_t attributes = m_vram[(entry + 2U) & 0x7fffU];
        const std::uint16_t x_word = m_vram[(entry + 3U) & 0x7fffU];
        const unsigned height = ((size_link >> 8) & 3U) + 1U;
        const unsigned width = ((size_link >> 10) & 3U) + 1U;
        const unsigned link = size_link & 0x7fU;
        const int top = static_cast<int>(y_word & 0x01ffU) - 128;
        const int row = static_cast<int>(line) - top;
        if (row >= 0 && row < static_cast<int>(height * 8U)) {
            const unsigned code = attributes & 0x07ffU;
            const bool xflip = (attributes & 0x0800U) != 0;
            const bool yflip = (attributes & 0x1000U) != 0;
            const unsigned palette = (attributes >> 13) & 3U;
            const bool priority = (attributes & 0x8000U) != 0;
            const unsigned tile_row = static_cast<unsigned>(row) >> 3;
            const unsigned pixel_y = static_cast<unsigned>(row) & 7U;
            const unsigned source_tile_row = yflip ? height - 1U - tile_row : tile_row;
            const unsigned source_y = yflip ? 7U - pixel_y : pixel_y;
            const unsigned x_origin = x_word & 0x01ffU;

            if (x_origin == 0) sprite_mask |= 1U;
            if (x_origin > 0 && x_origin < 0x40U) sprite_mask |= 2U;
            if (sprite_mask != 3U) {
                for (unsigned tile_column = 0; tile_column < width && sprite_budget != 0; ++tile_column) {
                    const unsigned source_tile_column = xflip ? width - 1U - tile_column : tile_column;
                    const unsigned tile_code = (code + source_tile_column * height + source_tile_row) & 0x07ffU;
                    for (unsigned pixel_x = 0; pixel_x < 8U && sprite_budget != 0; ++pixel_x, --sprite_budget) {
                        const unsigned source_x = xflip ? 7U - pixel_x : pixel_x;
                        const unsigned pen = pattern_pen(tile_code, source_x, source_y);
                        if (pen == 0) continue;
                        const unsigned buffer_x = (x_origin + tile_column * 8U + pixel_x) & 0x01ffU;
                        if (buffer_x < 128U || buffer_x >= 448U) continue;
                        const unsigned screen_x = buffer_x - 128U;
                        if (screen_x >= active_width || sprite_pixels[screen_x].opaque) continue;
                        sprite_pixels[screen_x] = {
                            static_cast<std::uint8_t>((palette << 4) | pen), true, priority};
                    }
                }
            }
        }
        if (link == 0) break;
        sprite_index = link;
    }

    // Low-priority sprites sit above low-priority scrolling planes. High-
    // priority tile pixels then cover them, followed by high-priority sprites.
    for (unsigned x = 0; x < output.size(); ++x) {
        if (sprite_pixels[x].opaque && !sprite_pixels[x].high_priority)
            low_priority[x] = sprite_pixels[x];
    }
    draw_plane(1, plane_b_base, true);
    draw_plane(0, plane_a_base, true);
    for (unsigned x = 0; x < output.size(); ++x) {
        output[x] = low_priority[x];
        if (high_priority[x].opaque) output[x] = high_priority[x];
        if (sprite_pixels[x].opaque && sprite_pixels[x].high_priority) output[x] = sprite_pixels[x];
    }
    return output;
}

void Sega3155313::acknowledge_irq(unsigned level) {
    if (level == 6 && m_irq6_pending && (m_regs[1] & 0x20U)) {
        m_irq6_pending = false;
        set_irq(6, false);
    } else if (level == 4 && m_irq4_pending && (m_regs[0] & 0x10U)) {
        m_irq4_pending = false;
        set_irq(4, false);
    }
}

void Sega3155313::set_irq(unsigned level, bool asserted) {
    bool& line = level == 6 ? m_irq6_asserted : m_irq4_asserted;
    if (line == asserted) return;
    line = asserted;
    if (m_callbacks.irq_line) m_callbacks.irq_line(level, asserted);
}

void Sega3155313::write_control(std::uint16_t data) {
    m_vram_fill_pending = false;
    if (m_command_pending) {
        m_command_pending = false;
        m_command_part2 = data;
        update_command();
        if (m_vdp_code & 0x20U) execute_dma();
        return;
    }

    if ((data & 0xc000U) == 0x8000U) {
        set_register((data >> 8) & 0x3fU, static_cast<std::uint8_t>(data));
        m_vdp_code = 0;
        m_vdp_address = 0;
        return;
    }
    m_command_pending = true;
    m_command_part1 = data;
    update_command();
}

void Sega3155313::write_data(std::uint16_t data) {
    m_command_pending = false;
    if (m_vram_fill_pending) {
        dma_vram_fill(data);
        return;
    }
    switch (m_vdp_code & 0x0fU) {
    case 0x01: write_vram(data); break;
    case 0x03: write_cram(data); break;
    case 0x05: write_vsram(data); break;
    default: break;
    }
}

std::uint16_t Sega3155313::read_data() {
    m_command_pending = false;
    switch (m_vdp_code & 0x0fU) {
    case 0x00: {
        const auto value = vram_read();
        increment_address();
        return value;
    }
    case 0x04: {
        const auto value = m_vsram[(m_vdp_address & 0x7eU) >> 1];
        increment_address();
        return value;
    }
    case 0x08: {
        const auto value = m_cram[(m_vdp_address & 0x7eU) >> 1];
        increment_address();
        return value;
    }
    default: return 0;
    }
}

std::uint16_t Sega3155313::read_status() {
    const bool display_disabled = (m_regs[1] & 0x40U) == 0;
    // MAME's System 18 VDP timing uses this horizontal interval for HBLANK.
    const bool hblank = m_horizontal_position > 400U && m_horizontal_position <= 460U;
    const std::uint16_t status = 0x2600U
        | (m_irq6_pending ? 0x0080U : 0)
        | (m_vblank || display_disabled ? 0x0008U : 0)
        | (hblank ? 0x0004U : 0);
    return status;
}

std::uint16_t Sega3155313::read_hv_counter() const {
    // Match the NTSC counter tables used by MAME's 315-5313. In 224-line
    // mode the V counter runs 00..EA then wraps through E5..FF; the 240-line
    // table runs 00..FF then wraps through 00..05.
    unsigned scanline = m_scanline;
    if (m_horizontal_position > 460U) scanline = (scanline + 1U) % 262U;
    const unsigned v_counter = (m_regs[1] & 0x08U)
        ? (scanline < 256U ? scanline : scanline - 256U)
        : (scanline < 235U ? scanline : scanline - 6U);
    // MAX_HPOSITION is 480. The H counter wraps its upper interval by
    // subtracting 0x49 after 0xf7, as in megadriv_read_hv_counters().
    const unsigned h_counter = m_horizontal_position > 0xf7U
        ? m_horizontal_position - 0x49U : m_horizontal_position;
    return static_cast<std::uint16_t>(((v_counter & 0xffU) << 8) | (h_counter & 0xffU));
}

void Sega3155313::set_register(unsigned index, std::uint8_t value) {
    index &= 0x1fU;
    if (!(m_regs[1] & 0x04U) && index > 10U) return;
    m_regs[index] = value;
    if (index == 0U) set_irq(4, m_irq4_pending && (value & 0x10U));
    if (index == 1U) set_irq(6, m_irq6_pending && (value & 0x20U));
}

void Sega3155313::update_command() {
    m_vdp_code = static_cast<std::uint8_t>(((m_command_part1 & 0xc000U) >> 14)
        | ((m_command_part2 & 0x00f0U) >> 2));
    m_vdp_address = static_cast<std::uint16_t>((m_command_part1 & 0x3fffU)
        | ((m_command_part2 & 0x0003U) << 14));
}

void Sega3155313::increment_address() {
    m_vdp_address = static_cast<std::uint16_t>(m_vdp_address + m_regs[0x0f]);
}

void Sega3155313::write_vram(std::uint16_t data) {
    const unsigned index = (m_vdp_address & 0xfffeU) >> 1;
    const auto stored = (m_vdp_address & 1U)
        ? static_cast<std::uint16_t>((data << 8) | (data >> 8)) : data;
    m_vram[index] = stored;
    const unsigned sprite_base = (m_regs[0x0c] & 1U)
        ? ((m_regs[5] & 0x7eU) << 9) : ((m_regs[5] & 0x7fU) << 9);
    const unsigned sprite_size = (m_regs[0x0c] & 1U) ? 0x400U : 0x200U;
    if (m_vdp_address >= sprite_base && m_vdp_address < sprite_base + sprite_size)
        m_internal_sprite_attributes[(m_vdp_address & (sprite_size - 1U)) >> 1] = stored;
    increment_address();
}

void Sega3155313::write_cram(std::uint16_t data) {
    m_cram[(m_vdp_address & 0x7eU) >> 1] = data;
    increment_address();
}

void Sega3155313::write_vsram(std::uint16_t data) {
    m_vsram[(m_vdp_address & 0x7eU) >> 1] = data;
    increment_address();
}

std::uint16_t Sega3155313::vram_read() const {
    return m_vram[(m_vdp_address & 0xfffeU) >> 1];
}

std::uint16_t Sega3155313::read_maincpu(std::uint32_t address) const {
    return m_callbacks.read_maincpu16 ? m_callbacks.read_maincpu16(address & 0x00ffffffU) : 0xffff;
}

void Sega3155313::execute_dma() {
    if (!(m_regs[1] & 0x10U)) return;
    const std::uint32_t raw_length = m_regs[0x13] | (static_cast<std::uint32_t>(m_regs[0x14]) << 8);
    const std::uint32_t source = (m_regs[0x15] | (static_cast<std::uint32_t>(m_regs[0x16]) << 8)
        | (static_cast<std::uint32_t>(m_regs[0x17] & 0x7fU) << 16)) << 1;
    const unsigned mode = (m_regs[0x17] >> 6) & 3U;
    if (mode <= 1U) {
        const std::uint32_t byte_length = raw_length << 1;
        if ((m_vdp_code & 0x0fU) == 0x01U) dma_68k_to_vram(source, byte_length);
        else if ((m_vdp_code & 0x0fU) == 0x03U) dma_68k_to_cram(source, byte_length);
        else if ((m_vdp_code & 0x0fU) == 0x05U) dma_68k_to_vsram(source, byte_length);
    } else if (mode == 2U && ((m_vdp_code & 0x0fU) == 0x01U || (m_vdp_code & 0x0fU) == 0x03U || (m_vdp_code & 0x0fU) == 0x05U)) {
        m_vram_fill_pending = true;
        m_vram_fill_length = static_cast<std::uint16_t>(raw_length);
    } else if (mode == 3U && (m_vdp_code & 0x0fU) == 0x01U) {
        dma_vram_copy(static_cast<std::uint16_t>(m_regs[0x15] | (m_regs[0x16] << 8)), static_cast<std::uint16_t>(raw_length));
    }
}

void Sega3155313::dma_68k_to_vram(std::uint32_t source, std::uint32_t byte_length) {
    if (byte_length == 0) byte_length = 0xffffU;
    for (std::uint32_t i = 0; i < (byte_length >> 1); ++i) {
        write_vram(read_maincpu(source));
        source = (source + 2U) & 0x00ffffffU;
        if (source < 2U) source = 0x00e00000U;
    }
    m_regs[0x13] = m_regs[0x14] = 0;
    m_regs[0x15] = static_cast<std::uint8_t>(source >> 1);
    m_regs[0x16] = static_cast<std::uint8_t>(source >> 9);
    m_regs[0x17] = static_cast<std::uint8_t>(source >> 17);
}

void Sega3155313::dma_68k_to_cram(std::uint32_t source, std::uint32_t byte_length) {
    if (byte_length == 0) byte_length = 0xffffU;
    for (std::uint32_t i = 0; i < (byte_length >> 1); ++i) {
        m_cram[(m_vdp_address & 0x7eU) >> 1] = read_maincpu(source);
        source = (source + 2U) & 0x00ffffffU;
        if (source < 2U) source = 0x00fe0000U;
        increment_address();
    }
    m_regs[0x13] = m_regs[0x14] = 0;
    m_regs[0x15] = static_cast<std::uint8_t>(source >> 1);
    m_regs[0x16] = static_cast<std::uint8_t>(source >> 9);
    m_regs[0x17] = static_cast<std::uint8_t>(source >> 17);
}

void Sega3155313::dma_68k_to_vsram(std::uint32_t source, std::uint32_t byte_length) {
    if (byte_length == 0) byte_length = 0xffffU;
    for (std::uint32_t i = 0; i < (byte_length >> 1); ++i) {
        m_vsram[(m_vdp_address & 0x7eU) >> 1] = read_maincpu(source);
        source = (source + 2U) & 0x00ffffffU;
        if (source < 2U) source = 0x00fe0000U;
        increment_address();
    }
    m_regs[0x13] = m_regs[0x14] = 0;
    m_regs[0x15] = static_cast<std::uint8_t>(source >> 1);
    m_regs[0x16] = static_cast<std::uint8_t>(source >> 9);
    m_regs[0x17] = static_cast<std::uint8_t>(source >> 17);
}

void Sega3155313::dma_vram_copy(std::uint16_t source, std::uint16_t length) {
    for (unsigned i = 0; i < length; ++i) {
        const auto word = m_vram[(source & 0xfffeU) >> 1];
        const auto byte = (source & 1U) ? static_cast<std::uint8_t>(word) : static_cast<std::uint8_t>(word >> 8);
        const unsigned index = (m_vdp_address & 0xfffeU) >> 1;
        if (m_vdp_address & 1U) m_vram[index] = static_cast<std::uint16_t>((m_vram[index] & 0xff00U) | byte);
        else m_vram[index] = static_cast<std::uint16_t>((m_vram[index] & 0x00ffU) | (byte << 8));
        ++source;
        increment_address();
    }
    m_regs[0x13] = m_regs[0x14] = 0;
}

void Sega3155313::dma_vram_fill(std::uint16_t value) {
    const auto write_byte = [this](std::uint32_t address, std::uint8_t byte) {
        auto& word = m_vram[(address & 0xfffeU) >> 1];
        if (address & 1U) word = static_cast<std::uint16_t>((word & 0xff00U) | byte);
        else word = static_cast<std::uint16_t>((word & 0x00ffU) | (byte << 8));
    };
    write_byte(m_vdp_address, static_cast<std::uint8_t>(value));
    for (unsigned count = 0; count <= m_vram_fill_length; ++count) {
        write_byte(m_vdp_address, static_cast<std::uint8_t>(value >> 8));
        increment_address();
    }
    m_regs[0x13] = m_regs[0x14] = 0;
}

} // namespace multiarcaderecomp::chips
