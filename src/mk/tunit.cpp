// T-Unit board behavior adapted from MAME's midtunit.cpp and midtunit_v.cpp.
// The MAME files are BSD-3-Clause; this implementation is a local rework for
// MameRecomp's bit-addressed TMS34010 bus and does not include MAME framework
// code. Keep the upstream attribution with the board module's license record.

#include "mk/tunit.hpp"

#include <algorithm>
#include <cstring>

namespace mamerecomp::mk {
namespace {
constexpr std::uint32_t kVideoBase = 0x00000000u;
constexpr std::uint32_t kVideoEnd = 0x00400000u;
constexpr std::uint32_t kWorkBase = 0x01000000u;
constexpr std::uint32_t kWorkEnd = 0x01400000u;
constexpr std::uint32_t kCmosBase = 0x01400000u;
constexpr std::uint32_t kCmosEnd = 0x01420000u;
constexpr std::uint32_t kCmosEnableBase = 0x01480000u;
constexpr std::uint32_t kCmosEnableEnd = 0x01500000u;
constexpr std::uint32_t kInputBase = 0x01600000u;
constexpr std::uint32_t kInputEnd = 0x01600040u;
constexpr std::uint32_t kPaletteBase = 0x01800000u;
constexpr std::uint32_t kPaletteEnd = 0x01880000u;
constexpr std::uint32_t kDmaBase = 0x01a80000u;
constexpr std::uint32_t kDmaEnd = 0x01a80100u;
constexpr std::uint32_t kVideoControlBase = 0x01b00000u;
constexpr std::uint32_t kVideoControlEnd = 0x01b00020u;
constexpr std::uint32_t kProtectionBase = 0x01b00000u;
constexpr std::uint32_t kProtectionEnd = 0x01b70000u;
constexpr std::uint32_t kGraphicsBase = 0x02000000u;
constexpr std::uint32_t kGraphicsEnd = 0x08000000u;
constexpr std::uint32_t kWatchdogBase = 0x01d81060u;
constexpr std::uint32_t kWatchdogEnd = 0x01d81080u;
constexpr std::uint32_t kSoundStateBase = 0x01d00000u;
constexpr std::uint32_t kSoundStateEnd = 0x01d00020u;
constexpr std::uint32_t kSoundCommandBase = 0x01d01020u;
constexpr std::uint32_t kSoundCommandEnd = 0x01d01040u;

constexpr unsigned kRegHesync = 0;
constexpr unsigned kRegHeblnk = 1;
constexpr unsigned kRegHsblnk = 2;
constexpr unsigned kRegHtotal = 3;
constexpr unsigned kRegVesync = 4;
constexpr unsigned kRegVeblnk = 5;
constexpr unsigned kRegVsblnk = 6;
constexpr unsigned kRegVtotal = 7;
constexpr unsigned kRegDpyctl = 8;
constexpr unsigned kRegDpystart = 9;
constexpr unsigned kRegDpyint = 10;
constexpr unsigned kRegControl = 11;
constexpr unsigned kRegIntenb = 17;
constexpr unsigned kRegIntpend = 18;
constexpr unsigned kRegConvsp = 19;
constexpr unsigned kRegConvdp = 20;
constexpr unsigned kRegPsize = 21;
constexpr unsigned kRegDpyTap = 27;
constexpr unsigned kRegHcount = 28;
constexpr unsigned kRegVcount = 29;
constexpr unsigned kRegDpyadr = 30;
constexpr unsigned kRegRefcnt = 31;

constexpr std::array<std::uint8_t, 57> kMkProtectionValues{
    0x13, 0x27, 0x0f, 0x1f, 0x3e, 0x3d, 0x3b, 0x37,
    0x2e, 0x1c, 0x38, 0x31, 0x22, 0x05, 0x0a, 0x15,
    0x2b, 0x16, 0x2d, 0x1a, 0x34, 0x28, 0x10, 0x21,
    0x03, 0x06, 0x0c, 0x19, 0x32, 0x24, 0x09, 0x13,
    0x27, 0x0f, 0x1f, 0x3e, 0x3d, 0x3b, 0x37, 0x2e,
    0x1c, 0x38, 0x31, 0x22, 0x05, 0x0a, 0x15, 0x2b,
    0x16, 0x2d, 0x1a, 0x34, 0x28, 0x10, 0x21, 0x03,
    0xff,
};

std::uint32_t bit_width_for_psize(std::uint16_t psize) noexcept {
    if (psize == 0x20) return 32;
    const unsigned code = psize & 0x1f;
    if (code < 2) return 1;
    if (code < 4) return 2;
    if (code < 8) return 4;
    if (code < 16) return 8;
    return 16;
}

unsigned pixel_shift_for_psize(std::uint16_t psize) noexcept {
    if (psize == 0x20) return 5;
    const unsigned code = psize & 0x1f;
    if (code < 2) return 0;
    if (code < 4) return 1;
    if (code < 8) return 2;
    if (code < 16) return 3;
    return 4;
}

std::uint32_t palette_entry_to_argb(std::uint16_t color) noexcept {
    const auto expand = [](unsigned value) {
        return (value << 3) | (value >> 2);
    };
    const std::uint32_t red = expand((color >> 10) & 31);
    const std::uint32_t green = expand((color >> 5) & 31);
    const std::uint32_t blue = expand(color & 31);
    return 0xff000000u | (red << 16) | (green << 8) | blue;
}

} // namespace

TUnit::TUnit(std::span<const std::uint8_t> main_rom,
             std::span<const std::uint8_t> graphics_rom)
    : m_main_rom(main_rom.begin(), main_rom.end()),
      m_graphics_rom(graphics_rom.begin(), graphics_rom.end()),
      m_work_ram(0x80000),
      m_cmos(0x4000),
      m_video_ram(0x80000),
      m_palette(0x8000),
      m_framebuffer(static_cast<std::size_t>(frame_width) * frame_height,
                    0xff000000u),
      m_presented_framebuffer(
          static_cast<std::size_t>(frame_width) * frame_height,
                    0xff000000u) {
    m_io[kRegPsize] = 0x08;
    m_palette_rgb.fill(0xff000000u);
}

mamerecomp::tms34010::Bus TUnit::cpu_bus() noexcept {
    return {this, &bus_read16, &bus_write16, &bus_read_pixel,
            &bus_write_pixel, &bus_convert_src_xy, &bus_convert_dst_xy,
            &bus_window_checking,
            &bus_is_mutable_code};
}

std::uint16_t TUnit::read_ram_word(std::span<const std::uint8_t> memory,
                                   std::uint32_t byte_offset) const noexcept {
    if (memory.empty()) return 0xffff;
    const std::size_t index = byte_offset % memory.size();
    const std::uint8_t lo = memory[index];
    const std::uint8_t hi = memory[(index + 1) % memory.size()];
    return static_cast<std::uint16_t>(lo | (static_cast<std::uint16_t>(hi) << 8));
}

void TUnit::write_ram_word(std::span<std::uint8_t> memory,
                           std::uint32_t byte_offset,
                           std::uint16_t value) noexcept {
    if (memory.empty()) return;
    const std::size_t index = byte_offset % memory.size();
    memory[index] = static_cast<std::uint8_t>(value);
    memory[(index + 1) % memory.size()] = static_cast<std::uint8_t>(value >> 8);
}

std::uint16_t TUnit::read_video_word(std::uint32_t byte_offset) const noexcept {
    const std::size_t index = byte_offset % m_video_ram.size();
    const std::size_t next = (index + 1) % m_video_ram.size();
    if (m_video_bank) {
        return static_cast<std::uint16_t>((m_video_ram[index] & 0x00ff) |
                                          ((m_video_ram[next] & 0x00ff) << 8));
    }
    return static_cast<std::uint16_t>((m_video_ram[index] >> 8) |
                                      (m_video_ram[next] & 0xff00));
}

void TUnit::write_video_word(std::uint32_t byte_offset,
                             std::uint16_t value) noexcept {
    const std::size_t index = byte_offset % m_video_ram.size();
    const std::size_t next = (index + 1) % m_video_ram.size();
    if (m_video_bank) {
        m_video_ram[index] = static_cast<std::uint16_t>(
            ((m_dma[8] & 0x00ff) << 8) | (value & 0x00ff));
        m_video_ram[next] = static_cast<std::uint16_t>(
            (m_dma[8] & 0xff00) | (value >> 8));
    } else {
        m_video_ram[index] = static_cast<std::uint16_t>(
            (m_video_ram[index] & 0x00ff) | ((value & 0x00ff) << 8));
        m_video_ram[next] = static_cast<std::uint16_t>(
            (m_video_ram[next] & 0x00ff) | (value & 0xff00));
    }
}

std::uint16_t TUnit::read_graphics_word(std::uint32_t byte_offset) const noexcept {
    if (m_graphics_rom.empty()) return 0xffff;
    const std::uint32_t word_offset = byte_offset >> 4;
    const std::uint32_t bank = (word_offset >> 21) & 1;
    // MAME's T-Unit has two 2 MiB windows: bank 0 is selected by control bit
    // 7 (base 0 or 8 MiB), while bank 1 is fixed at 4 MiB. This is deliberately
    // separate from DMA, whose source pointer addresses the raw graphics ROM.
    // MK's MIDTUNIT_VIDEO profile leaves m_gfx_rom_large=false in MAME.
    // Control bit 7 only selects the 8 MiB bank on W-Unit hardware; it must
    // not be inferred from the ROM region's allocated size (the last half of
    // MK's 12 MiB region is unpopulated). Bank 1 remains fixed at +4 MiB.
    const std::uint32_t bank_base = bank ? 0x400000u : 0u;
    const std::uint32_t offset = bank_base + ((word_offset & 0x001fffffu) * 2);
    return read_ram_word(m_graphics_rom, offset);
}

std::uint16_t TUnit::read16(std::uint32_t bit_address) const noexcept {
    const std::uint32_t address = bit_address & 0xfffffff0u;
    if (m_protection) {
        std::uint16_t value{};
        if (m_protection->read(address, value)) return value;
    }
    if (address >= internal_io_base && address < internal_io_base + 0x200) {
        const unsigned reg = (address - internal_io_base) >> 4;
        if (reg == kRegIntpend) return m_io[reg];
        if (reg == kRegRefcnt) return static_cast<std::uint16_t>(m_io[reg] & 0xfffc);
        return m_io[reg];
    }
    if (address >= kVideoBase && address < kVideoEnd)
        return read_video_word(address >> 3);
    if (address >= kWorkBase && address < kWorkEnd)
        return read_ram_word(m_work_ram, (address - kWorkBase) >> 3);
    if (address >= kCmosBase && address < kCmosEnd)
        return read_ram_word(m_cmos, (address - kCmosBase) >> 3);
    if (address >= kInputBase && address < kInputEnd) {
        const unsigned port = ((address - kInputBase) >> 4) & 3;
        ++m_input_read_counts[port];
        m_input_last_read_pc[port] = m_debug_cpu_pc;
        return m_inputs[port];
    }
    if (address >= kPaletteBase && address < kPaletteEnd) {
        const std::size_t index = ((address - kPaletteBase) >> 4) & 0x7fff;
        return m_palette[index];
    }
    if (address >= kDmaBase && address < kDmaEnd) {
        unsigned offset = (address - kDmaBase) >> 4;
        // MAME aliases DMA register 0 to the command/status register. Games
        // poll offset 0 to wait for the active blit to finish; returning the
        // raw left/right-skip value here lets the CPU race ahead of DMA.
        if (offset == 0) offset = 1;
        return m_dma[offset & 31];
    }
    if (!m_protection && address >= kProtectionBase && address < kProtectionEnd) {
        if (m_mk_protection_index >= kMkProtectionValues.size())
            m_mk_protection_index = 0;
        return static_cast<std::uint16_t>(
            kMkProtectionValues[m_mk_protection_index++] << 9);
    }
    if (address >= kSoundStateBase && address < kSoundStateEnd) {
        if (m_dcs_board) return m_dcs_board->host_status();
        if (m_fake_sound_state) {
            --m_fake_sound_state;
            return 0;
        }
        return 0xffff;
    }
    if (address >= kSoundCommandBase && address < kSoundCommandEnd)
        if (m_dcs_board) return m_dcs_board->host_data_read();
    if (address >= kSoundCommandBase && address < kSoundCommandEnd)
        return 0xffff;
    if ((address >= kVideoControlBase && address < kVideoControlEnd) ||
        (address >= 0x01f00000u && address < 0x01f00020u))
        return m_control;
    if (address >= kGraphicsBase && address < kGraphicsEnd)
        return read_graphics_word(address - kGraphicsBase);
    if ((address >= main_rom_base && address < main_rom_base + main_rom_window) ||
        address >= main_rom_mirror) {
        const std::uint32_t base = address >= main_rom_mirror
            ? main_rom_mirror : main_rom_base;
        return read_ram_word(m_main_rom, (address - base) >> 3);
    }
    return 0xffff;
}

void TUnit::write_dma_register(std::uint32_t word_offset,
                               std::uint16_t value) noexcept {
    static constexpr std::uint8_t register_map[2][16] = {
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16, 17, 14, 15},
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    };
    const unsigned bank = (m_dma[15] >> 5) & 1;
    const unsigned reg = register_map[bank][word_offset & 15];
    m_dma[reg] = value;
    ++m_dma_diagnostics.register_writes;
    m_dma_diagnostics.last_register = static_cast<std::uint16_t>(reg);
    m_dma_diagnostics.last_value = value;
    if (reg == 1) {
        ++m_dma_diagnostics.command_writes;
        m_dma_diagnostics.last_command_write = value;
        // MAME clears the external DMA IRQ when a new command is written.
        m_io[kRegIntpend] &= ~0x0002;
        if (value & 0x8000) {
            ++m_dma_diagnostics.commands;
            m_dma_diagnostics.last_command = value;
            m_dma_diagnostics.last_source = m_dma[2] |
                (static_cast<std::uint32_t>(m_dma[3]) << 16);
            execute_dma();
        }
        else m_dma_cycles_remaining = 0;
    }
}

void TUnit::schedule_dma_completion(std::uint64_t pixels) noexcept {
    // MAME's DMA timer is 41 ns per output pixel. The T-Unit CPU runs at
    // 50 MHz input clock, while the TMS34010 execution cycle spans eight
    // input clocks (160 ns). Convert the timer duration into TMS execution
    // cycles and round up to the next CPU dispatch boundary.
    m_dma_cycles_remaining = (pixels * 41u + 159u) / 160u;
    m_dma_diagnostics.output_pixels += pixels;
    m_dma_diagnostics.cycles_remaining = m_dma_cycles_remaining;
    if (!m_dma_cycles_remaining) {
        m_dma[1] &= 0x7fff;
        m_io[kRegIntpend] |= 0x0002; // External interrupt 1 / DMA complete.
        ++m_dma_diagnostics.completed;
    }
}

std::uint32_t TUnit::read_graphics_bits(std::uint32_t bit_offset,
                                       unsigned width) const noexcept {
    if (m_graphics_rom.empty() || !width) return 0;
    if (width <= 8) {
        std::size_t byte = bit_offset >> 3;
        if (byte >= m_graphics_rom.size()) byte %= m_graphics_rom.size();
        const unsigned shift = bit_offset & 7;
        std::uint32_t pair = m_graphics_rom[byte];
        if (shift + width > 8) {
            const std::size_t next = byte + 1 == m_graphics_rom.size()
                ? 0 : byte + 1;
            pair |= static_cast<std::uint32_t>(m_graphics_rom[next]) << 8;
        }
        return (pair >> shift) & ((1u << width) - 1u);
    }

    std::uint32_t result = 0;
    for (unsigned bit = 0; bit < width; ++bit) {
        const std::uint32_t at = bit_offset + bit;
        const std::size_t byte = (at >> 3) % m_graphics_rom.size();
        if ((m_graphics_rom[byte] >> (at & 7)) & 1)
            result |= 1u << bit;
    }
    return result;
}

void TUnit::execute_dma() noexcept {
    const std::uint16_t command = m_dma[1];
    if (!(command & 0x8000)) return;
    const bool capture_command = m_dma_history_enabled &&
                                 m_dma_command_history.size() < 1'000'000;
    std::uint64_t record_pixels_written = 0;

    unsigned bits_per_pixel = (command >> 12) & 7;
    if (!bits_per_pixel) bits_per_pixel = 8;
    const bool skip_rows = (command & 0x0080) != 0;
    const bool x_flip = (command & 0x0010) != 0;
    const bool y_flip = (command & 0x0020) != 0;
    const unsigned pre_skip_shift = (command >> 8) & 3;
    const unsigned post_skip_shift = (command >> 10) & 3;

    std::uint32_t source = m_dma[2] | (static_cast<std::uint32_t>(m_dma[3]) << 16);
    if ((command & 15) == 0x0c) source = 0;
    if (source >= 0x02000000u) source -= 0x02000000u;
    if (source >= 0xf8000000u) source -= 0xf8000000u;
    if (source >= 0x10000000u) {
        schedule_dma_completion(0);
        return;
    }

    const int image_width = m_dma[6] & 0x03ff;
    const int image_height = m_dma[7] & 0x03ff;
    const int x_start = m_dma[4] & 0x03ff;
    int y = m_dma[5] & 0x01ff;
    const int left_clip = m_dma[16] & 0x03ff;
    const int right_clip = m_dma[17] & 0x03ff;
    const int top_clip = m_dma[12] & 0x01ff;
    const int bottom_clip = m_dma[13] & 0x01ff;
    const int start_skip = (command & 0x0040) ? (m_dma[0] & 0xff) : 0;
    const int end_skip_initial = (command & 0x0040) ? (m_dma[0] >> 8) : m_dma[0];
    const int x_step = m_dma[10] ? m_dma[10] : 0x100;
    const int y_step = m_dma[11] ? m_dma[11] : 0x100;
    const std::uint16_t palette = m_dma[8] & 0x7f00;
    const std::uint16_t solid_color = palette | (m_dma[9] & 0xff);

    enum class PixelOp { Skip, Solid, Copy };
    PixelOp zero_op = PixelOp::Skip;
    PixelOp nonzero_op = PixelOp::Skip;
    switch (command & 15) {
    case 1: zero_op = PixelOp::Copy; break;
    case 2: nonzero_op = PixelOp::Copy; break;
    case 3: zero_op = nonzero_op = PixelOp::Copy; break;
    case 4: case 5: zero_op = PixelOp::Solid; break;
    case 6: case 7: zero_op = PixelOp::Solid; nonzero_op = PixelOp::Copy; break;
    case 8: case 10: nonzero_op = PixelOp::Solid; break;
    case 9: case 11: zero_op = PixelOp::Copy; nonzero_op = PixelOp::Solid; break;
    case 12: case 13: case 14: case 15:
        zero_op = nonzero_op = PixelOp::Solid;
        break;
    default:
        // Unsupported/no-op DMA modes are represented by MAME's
        // dma_draw_none: they still remain busy for the calculated duration.
        break;
    }

    // The MAME implementation keeps the source cursor and scale accumulators
    // in 8-bit fixed point. The ROM stream itself is little-endian bit order.
    const unsigned x_count = static_cast<unsigned>(std::clamp(image_width, 0, 1023));
    const unsigned y_count = static_cast<unsigned>(std::clamp(image_height, 0, 1023));
    const std::uint32_t pixel_mask = (1u << bits_per_pixel) - 1u;
    const bool is_scaled = x_step != 0x100 || y_step != 0x100;
    std::uint32_t row_source = source;
    int iy = 0;
    while (iy < static_cast<int>(y_count << 8)) {
        std::uint32_t pixel_source = row_source;
        int line_width = static_cast<int>(x_count << 8);
        int sx = x_start;
        int ix = 0;
        int pre = 0;
        int post = 0;
        if (skip_rows) {
            const std::uint8_t skip_header = static_cast<std::uint8_t>(
                read_graphics_bits(pixel_source, 8));
            pixel_source += 8;
            pre = (skip_header & 15) << (pre_skip_shift + 8);
            int advance = pre / x_step;
            sx = (sx + (x_flip ? -advance : advance)) & 0x03ff;
            ix += advance * x_step;
            post = (skip_header >> 4) << (post_skip_shift + 8);
            line_width -= post;
        }
        if (y >= top_clip && y <= bottom_clip) {
            if (ix < (start_skip << 8)) {
                const int advance = (((start_skip << 8) - ix) / x_step) * x_step;
                ix += advance;
                pixel_source += static_cast<std::uint32_t>(advance >> 8) * bits_per_pixel;
            }
            // MAME clips against the programmed end-skip register here, even
            // after applying the per-row post-skip header to line_width.
            if ((line_width >> 8) > image_width - end_skip_initial)
                line_width = (image_width - end_skip_initial) << 8;

            while (ix < line_width) {
                if (sx >= left_clip && sx <= right_clip) {
                    const std::uint32_t pixel = read_graphics_bits(
                        pixel_source, bits_per_pixel) & pixel_mask;
                    const PixelOp operation = pixel ? nonzero_op : zero_op;
                    if (operation != PixelOp::Skip) {
                        const std::uint16_t value = operation == PixelOp::Solid
                            ? solid_color
                            : static_cast<std::uint16_t>(palette | pixel);
                        m_video_ram[static_cast<std::size_t>(y) * 512 + sx] = value;
                        if (capture_command) ++record_pixels_written;
                    }
                }
                sx = (sx + (x_flip ? -1 : 1)) & 0x03ff;
                if (!is_scaled) {
                    ix += 0x100;
                    pixel_source += bits_per_pixel;
                } else {
                    const int old_x = ix >> 8;
                    ix += x_step;
                    pixel_source += static_cast<std::uint32_t>(
                        ((ix >> 8) - old_x) * bits_per_pixel);
                }
            }
        }

        y = (y + (y_flip ? -1 : 1)) & 0x01ff;
        if (!is_scaled) {
            // iy is always a 8.8 fixed-point coordinate. The MAME blitter
            // loops to (height << 8), so an unscaled output row advances by
            // 0x100 here, not by one. Incrementing by one repeats each DMA
            // image 256 times and smears its rows through the video RAM.
            iy += 0x100;
            if (skip_rows) {
                int source_width = image_width - ((pre + post) >> 8);
                row_source += 8;
                if (source_width > 0)
                    row_source += static_cast<std::uint32_t>(source_width) * bits_per_pixel;
            } else {
                row_source += x_count * bits_per_pixel;
            }
        } else {
            const int old_y = iy >> 8;
            iy += y_step;
            const unsigned row_advance = static_cast<unsigned>((iy >> 8) - old_y);
            if (!skip_rows) {
                row_source += row_advance * x_count * bits_per_pixel;
            } else if (row_advance) {
                row_source += 8;
                int first_width = image_width - ((pre + post) >> 8);
                if (first_width > 0)
                    row_source += static_cast<std::uint32_t>(first_width) * bits_per_pixel;
                for (unsigned skipped = 1; skipped < row_advance; ++skipped) {
                    const std::uint8_t header = static_cast<std::uint8_t>(
                        read_graphics_bits(row_source, 8));
                    const int row_pre = (header & 15) << pre_skip_shift;
                    const int row_post = (header >> 4) << post_skip_shift;
                    const int row_width = image_width - row_pre - row_post;
                    row_source += 8;
                    if (row_width > 0)
                        row_source += static_cast<std::uint32_t>(row_width) * bits_per_pixel;
                }
            }
        }
    }
    std::uint64_t pixels = 0;
    if (is_scaled) {
        if (x_step && y_step)
            pixels = static_cast<std::uint64_t>((x_count << 8) / x_step) *
                     static_cast<std::uint64_t>((y_count << 8) / y_step);
    } else {
        pixels = static_cast<std::uint64_t>(x_count) * y_count;
    }
    if (capture_command) {
        DmaCommandRecord record{};
        record.sequence = m_dma_diagnostics.commands;
        record.command = command;
        record.left_right_skip = m_dma[0];
        record.x_start = m_dma[4];
        record.y_start = m_dma[5];
        record.width = m_dma[6];
        record.height = m_dma[7];
        record.palette = m_dma[8];
        record.color = m_dma[9];
        record.x_step = m_dma[10];
        record.y_step = m_dma[11];
        record.top_clip = m_dma[12];
        record.bottom_clip = m_dma[13];
        record.source = m_dma[2] | (static_cast<std::uint32_t>(m_dma[3]) << 16);
        record.left_clip = m_dma[16];
        record.right_clip = m_dma[17];
        record.pixels_written = record_pixels_written;
        m_dma_command_history.push_back(record);
    }
    schedule_dma_completion(pixels);
}

void TUnit::write16(std::uint32_t bit_address, std::uint16_t value) noexcept {
    const std::uint32_t address = bit_address & 0xfffffff0u;
    if (m_protection && m_protection->write(address, value)) return;
    if (address >= internal_io_base && address < internal_io_base + 0x200) {
        const unsigned reg = (address - internal_io_base) >> 4;
        if (reg < m_io.size()) {
            if (reg == kRegIntpend) {
                const std::uint16_t previous = m_io[reg];
                m_io[reg] = previous;
                if (!(value & 0x0800)) m_io[reg] &= ~0x0800;
                if (!(value & 0x0400)) m_io[reg] &= ~0x0400;
            } else if (reg == 15) {
                // TMS34010 HSTCTLL CPU-side semantics from MAME: MSGOUT and
                // INTOUT are writable, while INTIN is cleared by writing 0.
                const std::uint16_t old = m_io[reg];
                std::uint16_t updated = static_cast<std::uint16_t>(
                    (old & 0xff8f) | (value & 0x0070));
                updated |= value & 0x0080;
                updated &= static_cast<std::uint16_t>(value | ~0x0008u);
                m_io[reg] = updated;
            } else if (reg == 16) {
                // HSTCTLH is the CPU halt/NMI register, not a board-local
                // halt flag. MAME stops execution immediately when the CPU
                // sets HALT and accepts NMI independently of ST.IE.
                m_io[reg] = value;
                m_cpu_halted = (value & 0x8000) != 0;
            } else {
                m_io[reg] = value;
            }
            if (reg == kRegConvsp) {
                const unsigned shift = (~value) & 31;
                m_convsp = 1u << shift;
            } else if (reg == kRegConvdp) {
                const unsigned shift = (~value) & 31;
                m_convdp = 1u << shift;
            }
        }
        return;
    }
    if (address >= kVideoBase && address < kVideoEnd) {
        write_video_word(address >> 3, value);
        return;
    }
    if (address >= kWorkBase && address < kWorkEnd) {
        write_ram_word(m_work_ram, (address - kWorkBase) >> 3, value);
        return;
    }
    if (address >= kCmosBase && address < kCmosEnd) {
        if (m_cmos_write_enable) {
            write_ram_word(m_cmos, (address - kCmosBase) >> 3, value);
            m_cmos_write_enable = false;
        }
        return;
    }
    if (address >= kCmosEnableBase && address < kCmosEnableEnd) {
        m_cmos_write_enable = true;
        return;
    }
    if (address >= kPaletteBase && address < kPaletteEnd) {
        const std::size_t index = ((address - kPaletteBase) >> 4) & 0x7fff;
        m_palette[index] = value;
        m_palette_rgb[index] = palette_entry_to_argb(value);
        return;
    }
    if (address >= kDmaBase && address < kDmaEnd) {
        write_dma_register((address - kDmaBase) >> 4, value);
        return;
    }
    if (!m_protection && address >= kProtectionBase && address < kProtectionEnd) {
        const std::uint8_t requested = static_cast<std::uint8_t>((value >> 9) & 0x3f);
        const auto found = std::find(kMkProtectionValues.begin(),
                                     kMkProtectionValues.end(), requested);
        m_mk_protection_index = found == kMkProtectionValues.end()
            ? 0 : static_cast<std::uint8_t>(found - kMkProtectionValues.begin());
        return;
    }
    if (address >= kSoundCommandBase && address < kSoundCommandEnd) {
        if (address == kSoundCommandBase) return; // MAME ignores the low lane.
        if (m_dcs_board) {
            m_dcs_board->host_data_write(value);
            return;
        }
        m_sound_reset_asserted = (value & 0x0100) == 0;
        m_sound_command = static_cast<std::uint8_t>(value);
        m_fake_sound_state = 128;
        if (m_sound_board) {
            m_sound_board->reset_write(m_sound_reset_asserted);
            m_sound_board->write_main_command(m_sound_command);
        }
        return;
    }
    if ((address >= kVideoControlBase && address < kVideoControlEnd) ||
        (address >= 0x01f00000u && address < 0x01f00020u)) {
        m_control = value;
        m_video_bank = static_cast<std::uint8_t>((value >> 5) & 1);
        return;
    }
    if (address >= kWatchdogBase && address < kWatchdogEnd) return;
}

std::uint32_t TUnit::read_pixel(std::uint32_t bit_address) const noexcept {
    // MAME's set_pixel_function switches PIXBLT pixel reads/writes to the
    // T-Unit shift register whenever DPYCTL.SR is set. The callbacks copy
    // 2*512 local-video words starting at address >> 3; the pixel read itself
    // returns the first 16-bit word in that register.
    if (m_io[kRegDpyctl] & 0x0800) {
        const std::size_t first = (bit_address >> 3) & 0x7ffff;
        const std::size_t available = m_video_ram.size() - first;
        const std::size_t copied = std::min(available, m_shift_register.size());
        std::copy_n(m_video_ram.begin() + first, copied, m_shift_register.begin());
        if (copied < m_shift_register.size())
            std::fill_n(m_shift_register.begin() + copied,
                        m_shift_register.size() - copied, 0);
        return m_shift_register[0];
    }
    const unsigned width = bit_width_for_psize(m_io[kRegPsize]);
    std::uint32_t value = 0;
    for (unsigned bit = 0; bit < width; ++bit) {
        const std::uint32_t at = bit_address + bit;
        const std::uint16_t word = read16(at & 0xfffffff0u);
        const unsigned bit_in_word = at & 15;
        if ((word >> bit_in_word) & 1) value |= (1u << bit);
    }
    return value;
}

bool TUnit::write_pixel(std::uint32_t bit_address,
                        std::uint32_t value) noexcept {
    if (m_io[kRegDpyctl] & 0x0800) {
        (void)value;
        const std::size_t first = (bit_address >> 3) & 0x7ffff;
        const std::size_t available = m_video_ram.size() - first;
        const std::size_t copied = std::min(available, m_shift_register.size());
        std::copy_n(m_shift_register.begin(), copied,
                    m_video_ram.begin() + first);
        return true;
    }
    const unsigned width = bit_width_for_psize(m_io[kRegPsize]);
    for (unsigned bit = 0; bit < width; ++bit) {
        const std::uint32_t at = bit_address + bit;
        const std::uint32_t aligned = at & 0xfffffff0u;
        const unsigned bit_in_word = at & 15;
        std::uint16_t word = read16(aligned);
        const std::uint16_t mask = static_cast<std::uint16_t>(1u << bit_in_word);
        word = (value & (1u << bit)) ? static_cast<std::uint16_t>(word | mask)
                                     : static_cast<std::uint16_t>(word & ~mask);
        write16(aligned, word);
    }
    return true;
}

bool TUnit::convert_src_xy(std::uint32_t packed_xy, std::uint32_t offset,
                           std::uint32_t* linear_address) const noexcept {
    if (!linear_address) return false;
    const auto x = static_cast<std::int16_t>(packed_xy & 0xffff);
    const auto y = static_cast<std::int16_t>(packed_xy >> 16);
    const unsigned convsp = m_convsp ? m_convsp : 1;
    const unsigned shift = pixel_shift_for_psize(m_io[kRegPsize]);
    *linear_address = offset + static_cast<std::uint32_t>(
        static_cast<std::int64_t>(y) * convsp +
        (static_cast<std::int64_t>(x) << shift));
    return true;
}

bool TUnit::convert_dst_xy(std::uint32_t packed_xy, std::uint32_t offset,
                           std::uint32_t* linear_address) const noexcept {
    if (!linear_address) return false;
    const auto x = static_cast<std::int16_t>(packed_xy & 0xffff);
    const auto y = static_cast<std::int16_t>(packed_xy >> 16);
    const unsigned convdp = m_convdp ? m_convdp : 1;
    const unsigned shift = pixel_shift_for_psize(m_io[kRegPsize]);
    *linear_address = offset + static_cast<std::uint32_t>(
        static_cast<std::int64_t>(y) * convdp +
        (static_cast<std::int64_t>(x) << shift));
    return true;
}

void TUnit::set_input(unsigned port, std::uint16_t active_low_value) noexcept {
    if (port < m_inputs.size()) m_inputs[port] = active_low_value;
}

std::uint16_t TUnit::input(unsigned port) const noexcept {
    return port < m_inputs.size() ? m_inputs[port] : 0xffffu;
}

std::array<std::uint64_t, 4> TUnit::input_read_counts() const noexcept {
    return m_input_read_counts;
}

std::array<std::uint32_t, 4> TUnit::input_last_read_pc() const noexcept {
    return m_input_last_read_pc;
}

void TUnit::note_cpu_pc(std::uint32_t pc) noexcept {
    m_debug_cpu_pc = pc;
}

bool TUnit::load_cmos(std::span<const std::uint8_t> data) noexcept {
    if (data.size() != m_cmos.size()) return false;
    std::copy(data.begin(), data.end(), m_cmos.begin());
    return true;
}

std::span<const std::uint8_t> TUnit::cmos() const noexcept {
    return m_cmos;
}

void TUnit::attach_sound_board(SoundCommandInterface* board) noexcept {
    m_sound_board = board;
}

void TUnit::attach_dcs_board(SoundHostInterface* board) noexcept {
    m_dcs_board = board;
}

void TUnit::attach_protection(ProtectionInterface* protection) noexcept {
    m_protection = protection;
}

std::uint8_t TUnit::sound_command() const noexcept { return m_sound_command; }

bool TUnit::sound_reset_asserted() const noexcept { return m_sound_reset_asserted; }

std::uint16_t TUnit::io_register(unsigned index) const noexcept {
    return index < m_io.size() ? m_io[index] : 0xffff;
}

std::span<const std::uint16_t> TUnit::video_ram() const noexcept {
    return m_video_ram;
}

std::span<const std::uint16_t> TUnit::palette_ram() const noexcept {
    return m_palette;
}

std::span<const std::uint32_t> TUnit::framebuffer() const noexcept {
    return m_presented_framebuffer;
}

TUnit::VideoDiagnostics TUnit::video_diagnostics() const noexcept {
    return m_video_diagnostics;
}

std::uint64_t TUnit::take_completed_frames() noexcept {
    const auto completed = m_video_diagnostics.completed_frames;
    m_video_diagnostics.completed_frames = 0;
    return completed;
}

void TUnit::render_scanline(std::uint32_t row_address,
                            std::uint32_t column_address,
                            unsigned horizontal_blank_start,
                            unsigned horizontal_blank_end,
                            std::span<std::uint16_t> destination) const noexcept {
    const std::size_t source_base = (row_address << 9) & 0x3fe00;
    std::uint32_t column = column_address << 1;
    const unsigned first = std::min<unsigned>(horizontal_blank_start,
                                               destination.size());
    const unsigned last = std::min<unsigned>(horizontal_blank_end,
                                              destination.size());
    for (unsigned x = first; x < last; ++x) {
        destination[x] = m_video_ram[(source_base + (column++ & 0x1ff)) %
                                     m_video_ram.size()] & 0x7fff;
    }
}

TUnit::DisplayParams TUnit::current_display_params() const noexcept {
    DisplayParams params{};
    params.enabled = (m_io[kRegDpyctl] & 0x8000) != 0;
    params.vcount = m_io[kRegVcount];
    params.visible_start = m_io[kRegVeblnk];
    params.blank_start = m_io[kRegVsblnk];
    params.hblank_start = m_io[kRegHeblnk] * 2;
    params.hblank_end = m_io[kRegHsblnk] * 2;

    std::uint16_t display_address = m_io[kRegDpyadr];
    if (!(m_io[kRegDpyctl] & 0x0400)) display_address ^= 0xfffc;
    params.row_address = display_address >> 4;
    params.column_address = ((display_address & 0x007c) << 4) |
                            (m_io[kRegDpyTap] & 0x3fff);
    return params;
}

void TUnit::begin_scanline() noexcept {
    const std::uint16_t vcount = m_io[kRegVcount];
    const std::uint16_t blank_start = m_io[kRegVsblnk];

    if ((m_io[kRegDpyctl] & 0x8000) && vcount == m_io[kRegDpyint])
        m_io[kRegIntpend] |= 0x0400; // TMS34010 display interrupt.
    // MAME's TMS34010 scanline callback loads DPYSTRT at the start of
    // vertical blank before invoking the board's scanline renderer.
    if (vcount == blank_start) m_io[kRegDpyadr] = m_io[kRegDpystart];
}

void TUnit::end_scanline() noexcept {
    const std::uint16_t vcount = m_io[kRegVcount];
    const std::uint16_t visible_start = m_io[kRegVeblnk];
    const std::uint16_t blank_start = m_io[kRegVsblnk];
    const std::uint16_t total = m_io[kRegVtotal];

    // DPYADR advances after the visible scanline callback in MAME.
    if (vcount >= visible_start && vcount < blank_start) {
        std::uint16_t display_address = m_io[kRegDpyadr];
        if ((display_address & 3) == 0) {
            display_address = static_cast<std::uint16_t>(
                ((display_address & 0xfffc) - (m_io[kRegDpyctl] & 0x03fc)) |
                (m_io[kRegDpystart] & 3));
        } else {
            display_address = static_cast<std::uint16_t>(
                (display_address & 0xfffc) | ((display_address - 1) & 3));
        }
        m_io[kRegDpyadr] = display_address;
    }
    // Match MAME's scanline callback: increment first, then wrap when the
    // next count exceeds VTOTAL. Comparing the current count before the
    // increment processes an extra scanline per frame and drifts page flips.
    const std::uint16_t next_vcount = static_cast<std::uint16_t>(vcount + 1);
    m_io[kRegVcount] = (next_vcount > total) ? 0 : next_vcount;
}

void TUnit::advance_scanline() noexcept {
    begin_scanline();
    end_scanline();
}

void TUnit::advance_cycles(std::uint64_t cycles) noexcept {
    if (m_dma_cycles_remaining) {
        if (cycles < m_dma_cycles_remaining) {
            m_dma_cycles_remaining -= cycles;
        } else {
            m_dma_cycles_remaining = 0;
            m_dma_diagnostics.cycles_remaining = 0;
            m_dma[1] &= 0x7fff;
            m_io[kRegIntpend] |= 0x0002;
            ++m_dma_diagnostics.completed;
        }
    }

    // TMS34010's execute cycle is eight input clocks (MAME's
    // execute_cycles_to_clocks). The 4 MHz pixel clock with two pixels per
    // clock gives 253 pixel clocks per 506-pixel line, or 3162.5 TMS input
    // clocks. Express the beam in sixteenths of a TMS execute cycle so the
    // half-cycle line duration remains exact: 3162.5 / 8 * 16 = 6325.
    constexpr std::uint64_t sixteenths_per_scanline = 6325;
    m_video_cycle_remainder_twice += cycles * 16;
    while (m_video_cycle_remainder_twice >= sixteenths_per_scanline) {
        m_video_cycle_remainder_twice -= sixteenths_per_scanline;
        ++m_video_diagnostics.scanlines;
        begin_scanline();
        const DisplayParams params = current_display_params();
        if (params.enabled && params.vcount >= params.visible_start &&
            params.vcount < params.blank_start && params.vcount < frame_height) {
            const std::size_t frame_offset =
                static_cast<std::size_t>(params.vcount) * frame_width;
            std::fill_n(m_framebuffer.begin() + frame_offset, frame_width,
                        0xff000000u);
            ++m_video_diagnostics.enabled_visible_scanlines;
            const unsigned first = std::min<unsigned>(params.hblank_start,
                                                       frame_width);
            const unsigned last = std::min<unsigned>(params.hblank_end,
                                                      frame_width);
            const std::size_t source_base = (params.row_address << 9) & 0x3fe00;
            std::uint32_t column = params.column_address << 1;
            std::uint64_t nonzero_words = 0;
            for (unsigned x = first; x < last; ++x) {
                const auto word = static_cast<unsigned>(
                    m_video_ram[(source_base + (column++ & 0x1ff)) %
                                m_video_ram.size()] & 0x7fff);
                nonzero_words += word != 0;
                m_framebuffer[frame_offset + x] = m_palette_rgb[word];
            }
            m_video_diagnostics.nonzero_source_words += nonzero_words;
        }
        end_scanline();
        if (m_io[kRegVcount] == 0) {
            ++m_video_diagnostics.completed_frames;
            m_framebuffer.swap(m_presented_framebuffer);
            std::fill(m_framebuffer.begin(), m_framebuffer.end(), 0xff000000u);
        }
    }
}

void TUnit::acknowledge_interrupt(std::uint16_t mask) noexcept {
    // The CPU clears edge-triggered external interrupt latches on acceptance.
    m_io[kRegIntpend] &= static_cast<std::uint16_t>(~mask);
}

bool TUnit::display_interrupt_pending() const noexcept {
    return (m_io[kRegIntpend] & m_io[kRegIntenb] & 0x0400) != 0;
}

bool TUnit::cpu_halted() const noexcept { return m_cpu_halted; }

TUnit::DmaDiagnostics TUnit::dma_diagnostics() const noexcept {
    auto result = m_dma_diagnostics;
    result.current_command = m_dma[1];
    result.cycles_remaining = m_dma_cycles_remaining;
    return result;
}

void TUnit::enable_dma_history(bool enabled) noexcept {
    m_dma_history_enabled = enabled;
    if (!enabled) m_dma_command_history.clear();
}

std::span<const TUnit::DmaCommandRecord> TUnit::dma_command_history() const noexcept {
    return m_dma_command_history;
}

std::uint32_t TUnit::palette_rgb(unsigned index) const noexcept {
    return m_palette_rgb[index & 0x7fff] & 0x00ffffffu;
}

std::uint16_t TUnit::bus_read16(void* opaque, std::uint32_t address) noexcept {
    return static_cast<TUnit*>(opaque)->read16(address);
}

void TUnit::bus_write16(void* opaque, std::uint32_t address,
                        std::uint16_t value) noexcept {
    static_cast<TUnit*>(opaque)->write16(address, value);
}

std::uint32_t TUnit::bus_read_pixel(void* opaque,
                                   std::uint32_t address) noexcept {
    return static_cast<TUnit*>(opaque)->read_pixel(address);
}

bool TUnit::bus_write_pixel(void* opaque, std::uint32_t address,
                            std::uint32_t value) noexcept {
    return static_cast<TUnit*>(opaque)->write_pixel(address, value);
}

bool TUnit::bus_convert_src_xy(void* opaque, std::uint32_t xy,
                               std::uint32_t offset,
                               std::uint32_t* result) noexcept {
    return static_cast<TUnit*>(opaque)->convert_src_xy(xy, offset, result);
}

bool TUnit::bus_convert_dst_xy(void* opaque, std::uint32_t xy,
                               std::uint32_t offset,
                               std::uint32_t* result) noexcept {
    return static_cast<TUnit*>(opaque)->convert_dst_xy(xy, offset, result);
}

std::uint8_t TUnit::bus_window_checking(void* opaque) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<TUnit*>(opaque)->m_io[kRegControl] >> 6) & 3);
}

bool TUnit::bus_is_mutable_code(void*, std::uint32_t bit_address) noexcept {
    const std::uint32_t address = (bit_address & 0xfffffff0u) >> 3;
    return (address >= kVideoBase && address < kVideoEnd) ||
           (address >= kWorkBase && address < kWorkEnd) ||
           (address >= kCmosBase && address < kCmosEnd) ||
           (address >= kPaletteBase && address < kPaletteEnd);
}

} // namespace mamerecomp::mk
