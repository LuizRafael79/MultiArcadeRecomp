#include "naomi_address_space.hpp"
#include "naomi_arm7.hpp"
#include "naomi_m2crypt.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mamerecomp::naomi {
namespace {
constexpr std::uint32_t kPhysicalMask = 0x1fffffffu;
constexpr std::uint32_t kSramStart = 0x00200000;
constexpr std::uint32_t kSh4OnchipRamP0 = 0x1c000000;
constexpr std::uint32_t kSh4OnchipRamP1 = 0x1e000000;
constexpr std::uint32_t kSh4InternalStart = 0x1f000000;
constexpr std::uint32_t kSh4InternalSize = 0x00010000;
constexpr std::uint32_t kSh4Expevt = 0x1f000024;
constexpr std::uint32_t kSh4CpuVersion = 0x1f000030;
constexpr std::uint32_t kSh4Prr = 0x1f000044;
constexpr std::uint32_t kSh4BscStart = 0x1f800000;
constexpr std::uint32_t kSh4BscEnd = 0x1f800060;
constexpr std::uint32_t kSh4BscMcr = 0x1f800014;
constexpr std::uint32_t kSh4BscRtcsr = 0x1f80001c;
constexpr std::uint32_t kSh4BscRfcr = 0x1f800028;
constexpr std::uint32_t kSh4TmuStart = 0x1fd80000;
constexpr std::uint32_t kSh4TmuEnd = 0x1fd80030;
constexpr std::uint32_t kSh4Scif2Status = 0x1fe80010;
constexpr std::uint64_t kSh4RtcTickCycles = 200'000'000u / 128u;
constexpr std::uint32_t kAicaTickCycles = 4535u; // Flycast AICA_TICK, about 44.1 kHz at 200 MHz.
constexpr std::uint32_t kAicaArmCyclesPerSample = 512u; // 22.5792 MHz / 44.1 kHz.
constexpr std::uint32_t kSoundRegsStart = 0x00700000;
constexpr std::uint32_t kSoundRamStart = 0x00800000;
constexpr std::uint32_t kTextureRamStart = 0x04000000;
constexpr std::uint32_t kFrameRamStart = 0x05000000;
constexpr std::uint32_t kMainRamStart = 0x0c000000;
unsigned g_bsc_trace_events = 0;
unsigned g_g1_trace_events = 0;
unsigned g_g1_dma_trace_events = 0;
unsigned g_tmu_trace_events = 0;
unsigned g_m2_trace_events = 0;
unsigned g_unmapped_mmio_trace_events = 0;
unsigned g_scif2_trace_events = 0;
unsigned g_aica_mmio_trace_events = 0;
unsigned g_braf_table_write_count = 0;
unsigned g_braf_table_write_printed = 0;
unsigned g_callback_table_write_count = 0;
unsigned g_callback_table_nonzero_writes = 0;
unsigned g_callback_table_nonzero_printed = 0;
unsigned g_callback_table_zero_printed = 0;
unsigned g_video_setup_write_events = 0;

bool isNaomiMmioWindow(std::uint32_t physical) {
    return (physical >= 0x005f0000u && physical < 0x00600000u) ||
           (physical >= 0x00700000u && physical < 0x00708000u) ||
           (physical >= 0x01000000u && physical < 0x02000000u);
}

void traceUnmappedMmio(char operation, std::uint32_t address, std::uint32_t physical,
                       unsigned width, std::uint32_t value) {
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") &&
        physical >= kSoundRegsStart && physical < kSoundRegsStart + 0x8000u &&
        g_aica_mmio_trace_events < 128) {
        ++g_aica_mmio_trace_events;
        std::fprintf(stderr,
                     "Naomi unmapped AICA MMIO %c guest=%08x physical=%08x width=%u value=%08x\n",
                     operation, address, physical, width, value);
    }
    if (!std::getenv("MAMERECOMP_NAOMI_TRACE_UNMAPPED_MMIO") ||
        !isNaomiMmioWindow(physical) || g_unmapped_mmio_trace_events >= 128)
        return;
    ++g_unmapped_mmio_trace_events;
    std::fprintf(stderr,
                 "Naomi unmapped MMIO %c guest=%08x physical=%08x width=%u value=%08x\n",
                 operation, address, physical, width, value);
}
}

NaomiAddressSpace::NaomiAddressSpace(NaomiDeviceBus devices)
    : m_devices(std::move(devices)),
      m_sram(0x8000),
      m_bios(0x200000, 0xff),
      m_m2_ram(0x10000),
      m_onchip_ram_p0(0x1000),
      m_onchip_ram_p1(0x1000),
      m_sh4_internal_regs(kSh4InternalSize / 4, 0),
      m_sound_ram(0x800000),
      m_texture_ram(0x1000000),
      m_frame_ram(0x1000000),
      m_main_ram(0x2000000) {
    arm7::bind_memory(
        [this](std::uint32_t address, unsigned width) {
            return readAicaArm(address, width);
        },
        [this](std::uint32_t address, unsigned width, std::uint32_t value,
               std::uint32_t writer_pc) {
            writeAicaArm(address, width, value, writer_pc);
        });
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA"))
        m_sound_ram_dirty_pages.assign(m_sound_ram.size() / 0x1000u, 0);
}

NaomiAddressSpace::~NaomiAddressSpace() {
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA")) {
        const auto arm_regs = arm7::register_snapshot();
        std::fprintf(stderr,
                     "AICA ARM summary enabled=%u instructions=%llu pc=%08x undefined=%llu mailbox_writes=%llu sample_ticks=%llu SCIEB=%04x SCIPD=%04x e68k_out=%u L=%u regs={r0:%08x,r1:%08x,r2:%08x,r3:%08x,sp:%08x,lr:%08x,cpsr:%08x}\n",
                     m_aica_arm_enabled ? 1u : 0u,
                     static_cast<unsigned long long>(arm7::instructions_executed()),
                     arm7::program_counter(),
                     static_cast<unsigned long long>(arm7::undefined_instructions()),
                     static_cast<unsigned long long>(m_aica_arm_mailbox_writes),
                     static_cast<unsigned long long>(m_aica_sample_ticks),
                     static_cast<unsigned>(m_aica_registers[0x289c] |
                         (static_cast<unsigned>(m_aica_registers[0x289d]) << 8)),
                     static_cast<unsigned>(m_aica_registers[0x28a0] |
                         (static_cast<unsigned>(m_aica_registers[0x28a1]) << 8)),
                     m_aica_e68k_interrupt_out ? 1u : 0u,
                     m_aica_arm_interrupt_level,
                     arm_regs[0], arm_regs[1], arm_regs[2], arm_regs[3],
                     arm_regs[13], arm_regs[14], arm_regs[16]);
        std::fprintf(stderr,
                     "AICA ARM shared words final b0=%08x b4=%08x\n",
                     readAicaArm(0xb0u, 4), readAicaArm(0xb4u, 4));
    }
    arm7::unbind_memory();
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AOT_TABLE_WRITES"))
        std::fprintf(stderr,
                     "SH-4 AOT table write summary braf_writes=%u callback_writes=%u callback_nonzero_writes=%u\n",
                     g_braf_table_write_count, g_callback_table_write_count,
                     g_callback_table_nonzero_writes);
    if (!m_sound_ram_dirty_pages.empty()) {
        constexpr std::size_t page_size = 0x1000u;
        std::size_t page = 0;
        while (page < m_sound_ram_dirty_pages.size()) {
            while (page < m_sound_ram_dirty_pages.size() && !m_sound_ram_dirty_pages[page])
                ++page;
            if (page == m_sound_ram_dirty_pages.size()) break;
            const auto first_page = page;
            while (page < m_sound_ram_dirty_pages.size() && m_sound_ram_dirty_pages[page])
                ++page;
            const auto begin = first_page * page_size;
            const auto end = std::min(page * page_size, m_sound_ram.size());
            std::uint64_t hash = 14695981039346656037ull;
            for (auto offset = begin; offset < end; ++offset) {
                hash ^= m_sound_ram[offset];
                hash *= 1099511628211ull;
            }
            std::fprintf(stderr,
                         "AICA trace RAM span start=%08x covered_bytes=%zu hash_fnv1a64=%016llx\n",
                         static_cast<unsigned>(kSoundRamStart + begin), end - begin,
                         static_cast<unsigned long long>(hash));
        }
        const auto dirty_pages = static_cast<std::uint64_t>(std::count(
            m_sound_ram_dirty_pages.begin(), m_sound_ram_dirty_pages.end(), 1));
        std::fprintf(stderr,
                     "AICA trace RAM summary written_bytes=%llu dirty_pages=%llu page_size=4096\n",
                     static_cast<unsigned long long>(m_sound_ram_trace_write_bytes),
                     static_cast<unsigned long long>(dirty_pages));
    }
    if (const char* dump_path = std::getenv("MAMERECOMP_NAOMI_DUMP_AICA_RAM")) {
        std::ofstream dump(dump_path, std::ios::binary | std::ios::trunc);
        if (!dump) {
            std::fprintf(stderr, "AICA RAM dump failed path=%s\n", dump_path);
        } else {
            dump.write(reinterpret_cast<const char*>(m_sound_ram.data()),
                       static_cast<std::streamsize>(m_sound_ram.size()));
            if (dump)
                std::fprintf(stderr, "AICA RAM dump path=%s bytes=%zu\n", dump_path,
                             m_sound_ram.size());
            else
                std::fprintf(stderr, "AICA RAM dump incomplete path=%s\n", dump_path);
        }
    }
    m2crypt_set_ciphered_reader({});
    m2crypt_reset();
}

Sh4Bus NaomiAddressSpace::make_sh4_bus() {
    return {
        [this](std::uint32_t a) { return read8(a); },
        [this](std::uint32_t a) { return read16(a); },
        [this](std::uint32_t a) { return read32(a); },
        [this](std::uint32_t a, std::uint8_t v, std::uint32_t pc) { write8(a, v, pc); },
        [this](std::uint32_t a, std::uint16_t v, std::uint32_t pc) { write16(a, v, pc); },
        [this](std::uint32_t a, std::uint32_t v, std::uint32_t pc) { write32(a, v, pc); },
    };
}

std::uint32_t NaomiAddressSpace::physicalAddress(std::uint32_t address) {
    // P4 accesses reach SH-4 internal registers through the upper 29-bit
    // physical window; internal-register decode below handles its aliases.
    return address & kPhysicalMask;
}

std::optional<std::pair<std::vector<std::uint8_t>*, std::uint32_t>>
NaomiAddressSpace::memoryWindow(std::uint32_t physical) const {
    auto* self = const_cast<NaomiAddressSpace*>(this);
    if (physical >= kSramStart && physical < kSramStart + self->m_sram.size())
        return std::pair{&self->m_sram, physical - kSramStart};
    if (physical >= kSoundRamStart && physical < 0x01000000)
        return std::pair{&self->m_sound_ram, physical - kSoundRamStart};
    if (physical >= kTextureRamStart && physical < kTextureRamStart + self->m_texture_ram.size())
        return std::pair{&self->m_texture_ram, physical - kTextureRamStart};
    if (physical >= kFrameRamStart && physical < kFrameRamStart + self->m_frame_ram.size())
        return std::pair{&self->m_frame_ram, physical - kFrameRamStart};
    if (physical >= kMainRamStart && physical < kMainRamStart + self->m_main_ram.size())
        return std::pair{&self->m_main_ram, physical - kMainRamStart};
    return std::nullopt;
}

std::uint16_t NaomiAddressSpace::readM2RomWord(std::uint32_t offset) {
    if (offset & 0x40000000u) {
        if (offset != 0x4001fffeu) return 0xffff;
        m2crypt_set_key(m_m2_key);
        return m2crypt_read_word();
    }
    if ((m_rom_pio_offset & 0x20000000u) == 0)
        offset = (offset & 0x103fffffu) | ((offset & 0x07c00000u) << 1);
    // The PIO mode bits remain in the cartridge address until the base ROM
    // window decodes it. Flycast's Cartridge::Read applies this physical
    // 29-bit mask after M2 mode/bank translation; without it 32 MB mode
    // offsets such as 0x20001000 incorrectly fall past the ROM region.
    offset &= 0x1fffffffu;
    if (offset + 1u >= m_rom_board.size()) return 0xffff;
    return static_cast<std::uint16_t>(m_rom_board[offset] |
        (static_cast<std::uint16_t>(m_rom_board[offset + 1u]) << 8));
}

std::uint16_t NaomiAddressSpace::readM2Port(std::uint32_t offset) {
    switch (offset) {
    case 0x00: return static_cast<std::uint16_t>((m_rom_pio_offset >> 16) |
                         (m_rom_pio_autoincrement ? 0x8000u : 0));
    case 0x04: return static_cast<std::uint16_t>(m_rom_pio_offset);
    case 0x08: {
        const auto address = m_rom_pio_offset;
        const auto value = readM2RomWord(address);
        if (m_rom_pio_autoincrement) m_rom_pio_offset += 2;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1") && g_m2_trace_events++ < 4096)
            std::fprintf(stderr, "Naomi M2 PIO R offset=%08x -> %04x%s\n",
                         address, value, (address & 0x40000000u) ? " encrypted-mode" : "");
        return value;
    }
    case 0x0c: return static_cast<std::uint16_t>(m_dma_offset >> 16);
    case 0x10: return static_cast<std::uint16_t>(m_dma_offset);
    case 0x14: return m_dma_count;
    default: return 0xffff;
    }
}

void NaomiAddressSpace::writeM2Port(std::uint32_t offset, std::uint16_t value) {
    switch (offset) {
    case 0x00:
        m_rom_pio_autoincrement = (value & 0x8000u) != 0;
        m_rom_pio_offset = (m_rom_pio_offset & 0xffffu) |
                           ((static_cast<std::uint32_t>(value) << 16) & 0x7fff0000u);
        break;
    case 0x04:
        m_rom_pio_offset = (m_rom_pio_offset & 0xffff0000u) | value;
        break;
    case 0x08:
        if (m_rom_pio_offset & 0x40000000u) {
            if (m_rom_pio_offset & 0x00020000u) {
                const auto ram_offset = m_rom_pio_offset & 0xffffu;
                m_m2_ram[ram_offset] = static_cast<std::uint8_t>(value);
                m_m2_ram[(ram_offset + 1u) & 0xffffu] = static_cast<std::uint8_t>(value >> 8);
            } else {
                switch (m_rom_pio_offset & 0x1ffffu) {
                case 0x1fff8: m2crypt_set_low_address(value); break;
                case 0x1fffa: m2crypt_set_high_address(value); break;
                case 0x1fffc: m2crypt_set_subkey(value); break;
                default: break;
                }
            }
        }
        if (m_rom_pio_autoincrement) m_rom_pio_offset += 2;
        break;
    case 0x0c:
        m_dma_offset = (m_dma_offset & 0x0000ffffu) |
                       (static_cast<std::uint32_t>(value) << 16);
        break;
    case 0x10: m_dma_offset = (m_dma_offset & 0xffff0000u) | value; break;
    case 0x14: m_dma_count = value; break;
    default: break;
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1") && g_m2_trace_events++ < 4096)
        std::fprintf(stderr, "Naomi M2 G1 W reg=%02x value=%04x rom=%08x dma=%08x\n",
                     offset, value, m_rom_pio_offset, m_dma_offset);
}

std::uint32_t NaomiAddressSpace::readAicaArm(std::uint32_t address, unsigned width) {
    address &= 0x00ffffffu;
    if (width != 1 && width != 2 && width != 4)
        throw std::runtime_error("unsupported ARM7 AICA read width");
    if (address < 0x00800000u) {
        std::uint32_t value = 0;
        const auto offset = address & 0x007fffffu;
        for (unsigned i = 0; i < width; ++i)
            value |= static_cast<std::uint32_t>(m_sound_ram[(offset + i) & 0x007fffffu]) << (i * 8u);
        return value;
    }

    const auto offset = (address - 0x00800000u) & 0x7fffu;
    const unsigned bytes = width == 4 ? 2u : width;
    std::uint32_t value = 0;
    if (offset == 0x2d00u) value = m_aica_arm_interrupt_level;
    else if (offset == 0x2d04u) value = m_aica_arm_interrupt_mode;
    else {
        for (unsigned i = 0; i < bytes; ++i)
            value |= static_cast<std::uint32_t>(m_aica_registers[(offset + i) & 0x7fffu]) << (i * 8u);
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") &&
        m_aica_arm_register_trace_events < 128) {
        ++m_aica_arm_register_trace_events;
        std::fprintf(stderr,
                     "AICA ARM register R pc=%08x address=%08x width=%u value=%08x\n",
                     arm7::instruction_address(), address, width, value);
    }
    return value;
}

void NaomiAddressSpace::writeAicaArm(std::uint32_t address, unsigned width,
                                      std::uint32_t value, std::uint32_t writer_pc) {
    address &= 0x00ffffffu;
    if (width != 1 && width != 2 && width != 4)
        throw std::runtime_error("unsupported ARM7 AICA write width");
    if (address < 0x00800000u) {
        // AICA DRAM is shared byte-addressable RAM. Preserve ARM STR's full
        // width here; the 16-bit truncation below applies only to AICA regs.
        const unsigned bytes = width;
        const auto offset = address & 0x007fffffu;
        for (unsigned i = 0; i < bytes; ++i)
            m_sound_ram[(offset + i) & 0x007fffffu] = static_cast<std::uint8_t>(value >> (i * 8u));
        if (!m_sound_ram_dirty_pages.empty()) {
            constexpr std::size_t page_size = 0x1000u;
            for (unsigned i = 0; i < bytes; ++i) {
                const auto page = ((offset + i) & 0x007fffffu) / page_size;
                m_sound_ram_dirty_pages[page] = 1;
            }
            m_sound_ram_trace_write_bytes += bytes;
        }
        if (offset < 0x60u && offset + bytes > 0x5cu) {
            ++m_aica_arm_mailbox_writes;
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") &&
                m_aica_arm_mailbox_log_events < 32) {
                ++m_aica_arm_mailbox_log_events;
                const auto mailbox = readAicaArm(0x5cu, 4);
                std::fprintf(stderr,
                             "AICA ARM mailbox write pc=%08x address=%08x width=%u data=%08x mailbox=%08x write_index=%llu\n",
                             writer_pc, address, width, value, mailbox,
                             static_cast<unsigned long long>(m_aica_arm_mailbox_writes));
            }
        }
        if (offset < 0xb8u && offset + bytes > 0xb0u &&
            std::getenv("MAMERECOMP_NAOMI_TRACE_AICA")) {
            static std::uint32_t shared_word_trace_events = 0;
            if (shared_word_trace_events < 64) {
                ++shared_word_trace_events;
                std::fprintf(stderr,
                             "AICA ARM shared W pc=%08x address=%08x width=%u data=%08x words={b0:%08x,b4:%08x} event=%u\n",
                             writer_pc, address, width, value,
                             readAicaArm(0xb0u, 4), readAicaArm(0xb4u, 4),
                             shared_word_trace_events);
            }
        }
        return;
    }

    const auto offset = (address - 0x00800000u) & 0x7fffu;
    // Flycast models AICA control registers as 16-bit lanes on 32-bit ARM
    // accesses, while the shared RAM path above retains all four bytes.
    const unsigned register_bytes = width == 4 ? 2u : width;
    writeAicaRegister(offset, register_bytes, value, true);
}

void NaomiAddressSpace::writeAicaRegister(std::uint32_t offset, unsigned width,
                                           std::uint32_t value, bool from_arm) {
    offset &= 0x7fffu;
    if (width != 1 && width != 2 && width != 4)
        throw std::runtime_error("unsupported AICA register write width");
    const unsigned bytes = width == 4 ? 2u : width;
    auto read16 = [this](std::uint32_t address) {
        return static_cast<std::uint16_t>(m_aica_registers[address & 0x7fffu] |
            (static_cast<std::uint16_t>(m_aica_registers[(address + 1u) & 0x7fffu]) << 8));
    };
    auto write16 = [this](std::uint32_t address, std::uint16_t data) {
        m_aica_registers[address & 0x7fffu] = static_cast<std::uint8_t>(data);
        m_aica_registers[(address + 1u) & 0x7fffu] = static_cast<std::uint8_t>(data >> 8);
    };

    // Flycast arm_mem.cpp exposes e68k L/M only on the ARM side.
    if (from_arm && offset == 0x2d00u) return;
    if (from_arm && offset == 0x2d04u) {
        if (value & 1u) {
            m_aica_e68k_interrupt_out = false;
            updateAicaArmInterrupts();
        }
        return;
    }
    if (from_arm && std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") &&
        m_aica_arm_register_trace_events < 128) {
        ++m_aica_arm_register_trace_events;
        std::fprintf(stderr,
                     "AICA ARM register W pc=%08x address=%08x width=%u value=%08x\n",
                     arm7::instruction_address(), 0x00800000u + offset, width, value);
    }
    if (offset == 0x289cu) {
        write16(offset, static_cast<std::uint16_t>(value & 0x07ffu)); // SCIEB
        updateAicaArmInterrupts();
        return;
    }
    if (offset == 0x28a0u) {
        if (value & (1u << 5))
            write16(offset, static_cast<std::uint16_t>(read16(offset) | (1u << 5))); // SCIPD.SCPU is W1S.
        updateAicaArmInterrupts();
        return;
    }
    if (offset == 0x28a4u) {
        write16(0x28a0u, static_cast<std::uint16_t>(read16(0x28a0u) & ~value)); // SCIRE clears SCIPD.
        updateAicaArmInterrupts();
        return;
    }
    if (offset == 0x2890u || offset == 0x2894u || offset == 0x2898u) {
        for (unsigned i = 0; i < bytes; ++i)
            m_aica_registers[(offset + i) & 0x7fffu] = static_cast<std::uint8_t>(value >> (i * 8u));
        const auto timer = (offset - 0x2890u) / 4u;
        const auto mode = (read16(offset) >> 8) & 7u;
        const auto next_step = 1u << mode;
        if (next_step != m_aica_timer_step[timer]) {
            m_aica_timer_step[timer] = next_step;
            m_aica_timer_countdown[timer] = next_step;
        }
        return;
    }
    for (unsigned i = 0; i < bytes; ++i)
        m_aica_registers[(offset + i) & 0x7fffu] = static_cast<std::uint8_t>(value >> (i * 8u));
}

void NaomiAddressSpace::updateAicaArmInterrupts() {
    auto read16 = [this](std::uint32_t address) {
        return static_cast<std::uint16_t>(m_aica_registers[address & 0x7fffu] |
            (static_cast<std::uint16_t>(m_aica_registers[(address + 1u) & 0x7fffu]) << 8));
    };
    const auto enabled = read16(0x289cu) & 0x07ffu; // SCIEB
    const auto pending = read16(0x28a0u) & 0x07ffu; // SCIPD
    const auto active = enabled & pending;
    m_aica_arm_interrupt_pending = active != 0;
    if (m_aica_arm_interrupt_pending) {
        unsigned source = 0;
        // Flycast selects the priority and SCILV from enabled pending sources,
        // not from unrelated pending bits. Include SAMPLE_DONE (bit 10).
        while (source < 11u && (active & (1u << source)) == 0) ++source;
        const auto source_bit = 1u << std::min(source, 7u);
        std::uint32_t level = 0;
        if (read16(0x28a8u) & source_bit) level |= 1u;
        if (read16(0x28acu) & source_bit) level |= 2u;
        if (read16(0x28b0u) & source_bit) level |= 4u;
        m_aica_pending_interrupt_level = level;
        if (!m_aica_e68k_interrupt_out) {
            m_aica_e68k_interrupt_out = true;
            m_aica_arm_interrupt_level = level;
        }
    }
    arm7::set_fiq_line(m_aica_e68k_interrupt_out);
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA") && m_aica_irq_trace_events < 64) {
        ++m_aica_irq_trace_events;
        std::fprintf(stderr,
                     "AICA ARM IRQ SCIEB=%03x SCIPD=%03x active=%03x pending=%u e68k_out=%u level=%u\n",
                     enabled, pending, active, m_aica_arm_interrupt_pending ? 1u : 0u,
                     m_aica_e68k_interrupt_out ? 1u : 0u, m_aica_arm_interrupt_level);
    }
}

void NaomiAddressSpace::aicaSampleStep() {
    ++m_aica_sample_ticks;
    if (m_aica_arm_enabled && std::getenv("MAMERECOMP_NAOMI_TRACE_ARM7_PROGRESS") &&
        (m_aica_sample_ticks % 4096u) == 0) {
        const auto arm_regs = arm7::register_snapshot();
        std::fprintf(stderr,
                     "AICA ARM progress ticks=%llu instructions=%llu pc=%08x r0=%08x r1=%08x r2=%08x r3=%08x sp=%08x lr=%08x\n",
                     static_cast<unsigned long long>(m_aica_sample_ticks),
                     static_cast<unsigned long long>(arm7::instructions_executed()),
                     arm7::program_counter(), arm_regs[0], arm_regs[1], arm_regs[2],
                     arm_regs[3], arm_regs[13], arm_regs[14]);
    }
    auto read16 = [this](std::uint32_t address) {
        return static_cast<std::uint16_t>(m_aica_registers[address & 0x7fffu] |
            (static_cast<std::uint16_t>(m_aica_registers[(address + 1u) & 0x7fffu]) << 8));
    };
    auto write16 = [this](std::uint32_t address, std::uint16_t value) {
        m_aica_registers[address & 0x7fffu] = static_cast<std::uint8_t>(value);
        m_aica_registers[(address + 1u) & 0x7fffu] = static_cast<std::uint8_t>(value >> 8);
    };
    auto scipd = read16(0x28a0u);
    auto mcipd = read16(0x28b8u);
    scipd |= 1u << 10; // Flycast timeStep(): SAMPLE_DONE on each AICA sample.
    mcipd |= 1u << 10;
    for (unsigned timer = 0; timer < 3; ++timer) {
        if (--m_aica_timer_countdown[timer] != 0) continue;
        m_aica_timer_countdown[timer] = m_aica_timer_step[timer];
        const auto offset = 0x2890u + timer * 4u;
        const auto timer_value = read16(offset);
        const auto count = static_cast<std::uint8_t>(timer_value + 1u);
        write16(offset, static_cast<std::uint16_t>((timer_value & 0xff00u) | count));
        if (count == 0) {
            scipd |= 1u << (6u + timer);
            mcipd |= 1u << (6u + timer);
        }
    }
    write16(0x28a0u, scipd);
    write16(0x28b8u, mcipd);
    updateAicaArmInterrupts();
}

std::uint32_t NaomiAddressSpace::read(std::uint32_t address, unsigned width) {
    const auto physical = physicalAddress(address);
    if (physical >= kSoundRegsStart && physical < kSoundRegsStart + 0x8000u) {
        const auto offset = physical - kSoundRegsStart;
        if (offset == 0x2c00u) {
            const std::uint16_t value = static_cast<std::uint16_t>(m_aica_arm_reset) |
                                        (static_cast<std::uint16_t>(m_aica_vreg) << 8);
            if (width == 1) return m_aica_arm_reset;
            return value;
        }
        if (offset == 0x2c01u && width == 1) return m_aica_vreg;
        std::uint32_t value = 0;
        const unsigned bytes = width == 4 ? 2u : width;
        for (unsigned i = 0; i < bytes; ++i)
            value |= static_cast<std::uint32_t>(m_aica_registers[(offset + i) & 0x7fffu]) << (i * 8u);
        return value;
    }
    // MAME: naomibd.cpp::boardid_r; the X76F100 SDA line is returned in bit
    // 15 of the 16-bit ROM-board port at byte address 0x5f707c.
    if (m_board_id_eeprom_loaded && physical >= 0x005f707cu && physical < 0x005f707eu) {
        const auto word = m_board_id_eeprom.readSDA() ? 0x8000u : 0u;
        const auto shift = (physical & 1u) * 8u;
        return width == 1 ? ((word >> shift) & 0xffu) : word;
    }
    if (!m_rom_board.empty() && physical >= 0x005f7000u && physical <= 0x005f7015u) {
        const auto offset = (physical - 0x005f7000u) & ~1u;
        const auto word = readM2Port(offset);
        const auto shift = (physical & 1u) * 8u;
        if (width == 1) return (word >> shift) & 0xffu;
        if (width == 2) return word;
        return word | (static_cast<std::uint32_t>(readM2Port(offset + 2u)) << 16);
    }
    // MAME: src/mame/sega/naomig1.cpp::amap and the corresponding Naomi G1
    // device handlers. These are distinct from the 16-bit ROM-board ports.
    if (physical >= 0x005f7400u && physical < 0x005f7500u) {
        const auto reg = physical & ~3u;
        std::uint32_t value;
        switch (reg) {
        case 0x005f7404u: case 0x005f7408u: case 0x005f740cu:
        case 0x005f7414u: case 0x005f7418u:
            value = m_g1_registers[(reg - 0x005f7400u) / 4u];
            break;
        case 0x005f74b0u: // SB_G1SYSM: MAME returns 0 on Naomi.
        case 0x005f74f4u: // SB_GDSTARD
        case 0x005f74f8u: // SB_GDLEND
            value = 0;
            break;
        default:
            value = width == 1 ? 0xffu : (width == 2 ? 0xffffu : 0xffffffffu);
            break;
        }
        const auto shift = (physical & 3u) * 8u;
        value >>= shift;
        if (width < 4) value &= (1u << (width * 8u)) - 1u;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1_DMA") &&
            (reg == 0x005f7404u || reg == 0x005f7408u || reg == 0x005f740cu ||
             reg == 0x005f7414u || reg == 0x005f7418u) &&
            g_g1_dma_trace_events < 128) {
            ++g_g1_dma_trace_events;
            std::fprintf(stderr, "Naomi G1 DMA R reg=%08x width=%u value=%08x\n",
                         reg, width, value);
        }
        return value;
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1") &&
        (physical >= 0x005f7000u && physical < 0x005f7500u) &&
        g_g1_trace_events++ < 128)
        std::fprintf(stderr, "Naomi G1 R [%08x] physical=%08x width=%u\n",
                     address, physical, width);
    // Naomi BIOS is a 2 MiB ROM at 0x00000000, mirrored in the SH-4 P1/P2
    // aliases after physical-address normalization. Keep raw ROM byte order;
    // Flycast maps the BIOS blob without word swapping.
    if (physical < m_bios.size()) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < width; ++i) {
            const auto offset = physical + i;
            const auto byte = offset < m_bios.size() ? m_bios[offset] : 0xff;
            value |= static_cast<std::uint32_t>(byte) << (i * 8);
        }
        return value;
    }
    // SH-4 internal map with OIX=1, matching sh4_base_device::sh4_internal_map
    // in MAME. Mirror bits are removed before indexing the 4 KiB RAM banks.
    if ((physical >= kSh4OnchipRamP0 && physical < 0x1e000000u) ||
        (physical >= kSh4OnchipRamP1 && physical < kSh4InternalStart)) {
        const bool p1 = physical >= kSh4OnchipRamP1;
        const auto& ram = p1 ? m_onchip_ram_p1 : m_onchip_ram_p0;
        const auto base = p1 ? kSh4OnchipRamP1 : kSh4OnchipRamP0;
        std::uint32_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= static_cast<std::uint32_t>(ram[(physical - base + i) & 0xfffu]) << (i * 8);
        return value;
    }
    if (physical >= kSh4BscStart && physical < kSh4BscEnd) {
        const auto aligned = physical & ~3u;
        const auto index = (aligned - kSh4BscStart) / 4;
        std::uint32_t value = index < m_bsc_registers.size() ? m_bsc_registers[index] : 0;
        if (aligned == kSh4BscRfcr) value = m_bsc_rfcr;
        const auto shift = (physical & 3u) * 8u;
        value >>= shift;
        if (width < 4) value &= (1u << (width * 8u)) - 1u;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_BSC") && g_bsc_trace_events++ < 64)
            std::fprintf(stderr, "SH-4 BSC R [%08x] -> %08x (%u)\n",
                         address, value, width);
        return value;
    }
    if (physical >= kSh4TmuStart && physical < kSh4TmuEnd) {
        const auto offset = physical - kSh4TmuStart;
        std::uint32_t value = 0;
        if (offset == 0) value = m_tmu_tocr;
        else if (offset == 4) value = m_tmu_tstr;
        else if (offset == 8) value = m_tmu_tcor[0];
        else if (offset == 12) value = m_tmu_tcnt[0];
        else if (offset == 16) value = m_tmu_tcr[0];
        else if (offset == 20) value = m_tmu_tcor[1];
        else if (offset == 24) value = m_tmu_tcnt[1];
        else if (offset == 28) value = m_tmu_tcr[1];
        else if (offset == 32) value = m_tmu_tcor[2];
        else if (offset == 36) value = m_tmu_tcnt[2];
        else if (offset == 40) value = m_tmu_tcr[2];
        if (width < 4) value &= (1u << (width * 8u)) - 1u;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_TMU") && g_tmu_trace_events++ < 128)
            std::fprintf(stderr, "SH-4 TMU R [%08x] -> %08x (%u)\n", address, value, width);
        return value;
    }
    if (physical >= kSh4Scif2Status && physical < kSh4Scif2Status + 2u) {
        const auto shift = (physical - kSh4Scif2Status) * 8u;
        auto value = static_cast<std::uint32_t>(m_scif2_status) >> shift;
        if (width < 4) value &= (1u << (width * 8u)) - 1u;
        m_scif2_status_last_read = m_scif2_status;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_SCIF") && g_scif2_trace_events++ < 32)
            std::fprintf(stderr, "SH-4 SCIF2 SCFSR2 R [%08x] -> %04x\n",
                         address, m_scif2_status);
        return value;
    }
    if (physical >= kSh4InternalStart && physical < 0x20000000u) {
        // MAME's SH-4 map aliases 0xfffffff8..0xfffffffb to the legacy TTB
        // handler, in addition to the architectural P4 register at
        // 0xff000008..0xff00000b. Do not decode this high alias by simply
        // folding its low 16 bits into the internal-register window.
        const auto decoded = physical == 0x1ffffff8u
            ? kSh4InternalStart + 0x08u
            : kSh4InternalStart | (physical & 0xffffu);
        const auto offset = (decoded - kSh4InternalStart) / 4;
        if (decoded == kSh4Expevt) return 0; // MAME sh4_base_device::device_reset clears EXPEVT.
        if (decoded == kSh4CpuVersion) return 0x040206c8; // Naomi SH-4 revision.
        if (decoded == kSh4Prr) return 0;
        const auto value = offset < m_sh4_internal_regs.size() ? m_sh4_internal_regs[offset] : 0;
        if ((physical & 0x1fff0000u) == 0x1fd80000u &&
            std::getenv("MAMERECOMP_NAOMI_TRACE_TMU") && g_tmu_trace_events++ < 128)
            std::fprintf(stderr, "SH-4 TMU R [%08x] decoded=%08x -> %08x (%u)\n",
                         address, physical, value, width);
        if (physical >= 0x1f800000u && physical < 0x1f800054u &&
            std::getenv("MAMERECOMP_NAOMI_TRACE_BSC") && g_bsc_trace_events++ < 64)
            std::fprintf(stderr, "SH-4 BSC R [%08x] -> %08x (%u)\n",
                         address, value, width);
        return value;
    }
    // Let attached Naomi devices claim their windows before the generic RAM
    // mirrors. This is required for the PVR's Area 1 aliases to share VRAM.
    if (m_devices.read) {
        if (const auto value = m_devices.read(physical, width)) return *value;
    }
    const auto window = memoryWindow(physical);
    if (window) {
        const auto& [bytes, offset] = *window;
        if (offset + width > bytes->size()) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < width; ++i)
                value |= static_cast<std::uint32_t>(read8(address + i)) << (i * 8);
            return value;
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= static_cast<std::uint32_t>((*bytes)[offset + i]) << (i * 8);
        return value;
    }

    // These windows are delegated to attached devices (Holly, TA/PVR, AICA,
    // and ROM board). BIOS and unassigned areas read as open bus for now.
    const bool device_window =
        (physical >= 0x005f6800 && physical <= 0x005f9fff) ||
        (physical >= kSoundRegsStart && physical < kSoundRegsStart + 0x8000) ||
        (physical >= 0x01000000 && physical < 0x02000000);
    if (device_window && m_devices.read) {
        if (const auto value = m_devices.read(physical, width)) return *value;
    }
    const auto open_bus = width == 1 ? 0xffu : (width == 2 ? 0xffffu : 0xffffffffu);
    traceUnmappedMmio('R', address, physical, width, open_bus);
    return open_bus;
}

void NaomiAddressSpace::write(std::uint32_t address, unsigned width, std::uint32_t value,
                              std::uint32_t writer_pc) {
    const auto physical = physicalAddress(address);
    if (physical >= kSoundRegsStart && physical < kSoundRegsStart + 0x8000u) {
        const auto offset = physical - kSoundRegsStart;
        if (offset == 0x2c00u) {
            if (width == 1) {
                m_aica_arm_reset = static_cast<std::uint8_t>(value);
            } else {
                m_aica_arm_reset = static_cast<std::uint8_t>(value);
                m_aica_vreg = static_cast<std::uint8_t>(value >> 8);
            }
            m_aica_registers[0x2c00] = m_aica_arm_reset;
            m_aica_registers[0x2c01] = m_aica_vreg;
            const bool was_enabled = m_aica_arm_enabled;
            m_aica_arm_enabled = (m_aica_arm_reset & 1u) == 0;
            arm7::set_enabled(m_aica_arm_enabled);
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_AICA"))
                std::fprintf(stderr,
                             "AICA ARMRST write pc=%08x guest=%08x value=%08x ARMRST=%02x VREG=%02x arm_enabled=%u transition=%u sample_ticks=%llu arm_instructions=%llu\n",
                             writer_pc, address, value, m_aica_arm_reset, m_aica_vreg,
                             m_aica_arm_enabled ? 1u : 0u,
                             was_enabled != m_aica_arm_enabled ? 1u : 0u,
                             static_cast<unsigned long long>(m_aica_sample_ticks),
                             static_cast<unsigned long long>(arm7::instructions_executed()));
            return;
        }
        if (offset == 0x2c01u && width == 1) {
            m_aica_vreg = static_cast<std::uint8_t>(value);
            m_aica_registers[offset] = m_aica_vreg;
            return;
        }
        writeAicaRegister(offset, width, value, false);
        return;
    }
    static const bool trace_aot_tables =
        std::getenv("MAMERECOMP_NAOMI_TRACE_AOT_TABLE_WRITES") != nullptr;
    const bool touches_braf_table =
        physical < 0x0c035fb0u && physical + width > 0x0c035f84u;
    const bool touches_callback_table =
        physical < 0x0c05f03cu && physical + width > 0x0c05ec3cu;
    if (trace_aot_tables && touches_braf_table) {
        ++g_braf_table_write_count;
        if (g_braf_table_write_printed < 64) {
            ++g_braf_table_write_printed;
            std::fprintf(stderr,
                         "SH-4 AOT table write kind=braf pc=%08x guest=%08x physical=%08x value=%08x width=%u\n",
                         writer_pc, address, physical, value, width);
        }
    }
    if (trace_aot_tables && touches_callback_table) {
        ++g_callback_table_write_count;
        if (value != 0) {
            ++g_callback_table_nonzero_writes;
            if (g_callback_table_nonzero_printed < 128) {
                ++g_callback_table_nonzero_printed;
                std::fprintf(stderr,
                             "SH-4 AOT table write kind=callback pc=%08x guest=%08x physical=%08x value=%08x width=%u\n",
                             writer_pc, address, physical, value, width);
            }
        } else if (g_callback_table_zero_printed < 8) {
            ++g_callback_table_zero_printed;
            std::fprintf(stderr,
                         "SH-4 AOT table write kind=callback-zero pc=%08x guest=%08x physical=%08x width=%u\n",
                         writer_pc, address, physical, width);
        }
    }

    static const bool trace_video_setup =
        std::getenv("MAMERECOMP_NAOMI_TRACE_VIDEO_SETUP") != nullptr;
    const bool qacr_write = physical == 0x1f000038u || physical == 0x1f00003cu;
    const bool dmac2_write = physical >= 0x1fa00020u && physical <= 0x1fa0002cu;
    const bool holly_c2_write = physical >= 0x005f6800u && physical <= 0x005f6808u;
    const bool pvr_setup_write =
        physical == 0x005f8044u || physical == 0x005f80d0u ||
        (physical >= 0x005f8124u && physical <= 0x005f8134u) ||
        physical == 0x005f8144u || physical == 0x005f8160u;
    if (trace_video_setup && (qacr_write || dmac2_write || holly_c2_write || pvr_setup_write) &&
        g_video_setup_write_events < 512) {
        ++g_video_setup_write_events;
        std::fprintf(stderr,
                     "SH-4 video setup write pc=%08x guest=%08x physical=%08x value=%08x width=%u\n",
                     writer_pc, address, physical, value, width);
    }
    // MAME: naomibd.cpp::boardid_w. The ROM-board's 16-bit GPIO register
    // drives CS, reset, clock and data on the serial EEPROM.
    if (m_board_id_eeprom_loaded && physical >= 0x005f7078u && physical < 0x005f707au) {
        const auto data = width == 1
            ? static_cast<std::uint16_t>((value & 0xffu) << ((physical & 1u) * 8u))
            : static_cast<std::uint16_t>(value);
        m_board_id_eeprom.writeCS((data & 0x0004u) != 0);
        m_board_id_eeprom.writeRST((data & 0x0008u) != 0);
        m_board_id_eeprom.writeSCL((data & 0x0002u) != 0);
        m_board_id_eeprom.writeSDA((data & 0x0001u) != 0);
        return;
    }
    if (!m_rom_board.empty() && physical >= 0x005f7000u && physical <= 0x005f7015u) {
        const auto offset = (physical - 0x005f7000u) & ~1u;
        if (width == 1) {
            const auto old = readM2Port(offset);
            const auto shift = (physical & 1u) * 8u;
            const auto mask = static_cast<std::uint16_t>(0xffu << shift);
            writeM2Port(offset, static_cast<std::uint16_t>((old & ~mask) |
                ((value << shift) & mask)));
        } else {
            writeM2Port(offset, static_cast<std::uint16_t>(value));
            if (width == 4)
                writeM2Port(offset + 2u, static_cast<std::uint16_t>(value >> 16));
        }
        return;
    }
    if (physical >= 0x005f7400u && physical < 0x005f7500u) {
        const auto reg = physical & ~3u;
        const auto index = (reg - 0x005f7400u) / 4u;
        switch (reg) {
        case 0x005f7404u: case 0x005f7408u: case 0x005f740cu:
        case 0x005f7414u: case 0x005f7418u: {
            const auto shift = (physical & 3u) * 8u;
            const auto mask = width == 4 ? 0xffffffffu :
                ((1u << (width * 8u)) - 1u) << shift;
            auto& stored = m_g1_registers[index];
            const auto old = stored;
            stored = (stored & ~mask) | ((value << shift) & mask);
            if (reg == 0x005f740cu || reg == 0x005f7414u || reg == 0x005f7418u)
                stored &= 1u;
            if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1_DMA") &&
                g_g1_dma_trace_events < 128) {
                const bool start_edge = reg == 0x005f7418u && !(old & 1u) &&
                                        (stored & 1u) &&
                                        (m_g1_registers[0x14u / 4u] & 1u);
                ++g_g1_dma_trace_events;
                std::fprintf(stderr,
                             "Naomi G1 DMA W pc=%08x reg=%08x width=%u value=%08x old=%08x stored=%08x start_edge=%u\n",
                             writer_pc, reg, width, value, old, stored,
                             start_edge ? 1u : 0u);
            }
            return;
        }
        // Naomi G1 counter/control writes are accepted but have no modeled
        // side effects yet, matching MAME's no-op handlers.
        case 0x005f7480u: case 0x005f7484u: case 0x005f7488u:
        case 0x005f748cu: case 0x005f7490u: case 0x005f7494u:
        case 0x005f74a0u: case 0x005f74a4u: case 0x005f74b4u:
        case 0x005f74b8u:
            return;
        default:
            break;
        }
    }
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_G1") &&
        (physical >= 0x005f7000u && physical < 0x005f7500u) &&
        g_g1_trace_events++ < 128)
        std::fprintf(stderr, "Naomi G1 W [%08x] physical=%08x <- %08x width=%u\n",
                     address, physical, value, width);
    if (std::getenv("MAMERECOMP_NAOMI_TRACE_BOOT") &&
        ((physical >= 0x0c000000 && physical < 0x0c000300) ||
         (physical >= 0x1f000000 && physical < 0x20000000))) {
        std::fprintf(stderr, "Naomi boot W [%08x] <- %08x (%u)\n",
                     address, value, width);
    }
    if (physical < m_bios.size()) return; // Naomi BIOS is read-only.
    if ((physical >= kSh4OnchipRamP0 && physical < 0x1e000000u) ||
        (physical >= kSh4OnchipRamP1 && physical < kSh4InternalStart)) {
        const bool p1 = physical >= kSh4OnchipRamP1;
        auto& ram = p1 ? m_onchip_ram_p1 : m_onchip_ram_p0;
        const auto base = p1 ? kSh4OnchipRamP1 : kSh4OnchipRamP0;
        for (unsigned i = 0; i < width; ++i)
            ram[(physical - base + i) & 0xfffu] =
                static_cast<std::uint8_t>(value >> (i * 8));
        return;
    }
    if (physical >= kSh4BscStart && physical < kSh4BscEnd) {
        const auto aligned = physical & ~3u;
        const auto index = (aligned - kSh4BscStart) / 4;
        if (index >= m_bsc_registers.size()) return;
        const auto shift = (physical & 3u) * 8u;
        const auto mask = width == 4 ? 0xffffffffu : ((1u << (width * 8u)) - 1u) << shift;
        auto& reg = m_bsc_registers[index];
        reg = (reg & ~mask) | ((value << shift) & mask);
        if (aligned == kSh4BscRfcr) {
            m_bsc_rfcr = static_cast<std::uint16_t>(reg & 0x03ffu);
            reg = m_bsc_rfcr;
        }
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_BSC") && g_bsc_trace_events++ < 64)
            std::fprintf(stderr, "SH-4 BSC W [%08x] <- %08x (%u)\n",
                         address, value, width);
        return;
    }
    if (physical >= kSh4TmuStart && physical < kSh4TmuEnd) {
        const auto offset = physical - kSh4TmuStart;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_TMU") && g_tmu_trace_events++ < 128)
            std::fprintf(stderr, "SH-4 TMU W [%08x] <- %08x (%u)\n", address, value, width);
        if (offset == 0) m_tmu_tocr = static_cast<std::uint8_t>(value & 1u);
        else if (offset == 4) {
            m_tmu_tstr = static_cast<std::uint8_t>(value & 7u);
            for (auto& remainder : m_tmu_cycle_remainder) remainder = 0;
        } else if (offset == 8) m_tmu_tcor[0] = value;
        else if (offset == 12) m_tmu_tcnt[0] = value;
        else if (offset == 16) m_tmu_tcr[0] = static_cast<std::uint16_t>(value & 0x013fu);
        else if (offset == 20) m_tmu_tcor[1] = value;
        else if (offset == 24) m_tmu_tcnt[1] = value;
        else if (offset == 28) m_tmu_tcr[1] = static_cast<std::uint16_t>(value & 0x013fu);
        else if (offset == 32) m_tmu_tcor[2] = value;
        else if (offset == 36) m_tmu_tcnt[2] = value;
        else if (offset == 40) m_tmu_tcr[2] = static_cast<std::uint16_t>(value & 0x03ffu);
        return;
    }
    if (physical >= kSh4Scif2Status && physical < kSh4Scif2Status + 2u) {
        constexpr std::uint16_t writable_status = 0x00f3u;
        const auto shift = (physical - kSh4Scif2Status) * 8u;
        const auto mask = width == 4 ? 0xffffu : ((1u << (width * 8u)) - 1u) << shift;
        const auto written = static_cast<std::uint16_t>(
            (m_scif2_status & ~mask) | ((value << shift) & mask));
        const auto clear_mask = static_cast<std::uint16_t>(
            written | static_cast<std::uint16_t>(~writable_status) |
            static_cast<std::uint16_t>(~m_scif2_status_last_read));
        m_scif2_status &= clear_mask;
        m_scif2_status_last_read &= clear_mask;
        // The transmit FIFO is empty and its trigger level is met, so TDFE
        // remains asserted just as in Flycast's SCIF reset/empty-FIFO model.
        m_scif2_status |= 0x0020u;
        if (std::getenv("MAMERECOMP_NAOMI_TRACE_SCIF") && g_scif2_trace_events++ < 32)
            std::fprintf(stderr, "SH-4 SCIF2 SCFSR2 W [%08x] <- %04x, now %04x\n",
                         address, written, m_scif2_status);
        return;
    }
    if (physical >= kSh4InternalStart && physical < 0x20000000u) {
        // Keep the MAME SH-4 TTB alias coherent with 0xff000008.
        const auto decoded = physical == 0x1ffffff8u
            ? kSh4InternalStart + 0x08u
            : kSh4InternalStart | (physical & 0xffffu);
        if ((physical & 0x1fff0000u) == 0x1fd80000u &&
            std::getenv("MAMERECOMP_NAOMI_TRACE_TMU") && g_tmu_trace_events++ < 128)
            std::fprintf(stderr, "SH-4 TMU W [%08x] decoded=%08x <- %08x (%u)\n",
                         address, physical, value, width);
        if (physical >= 0x1f800000u && physical < 0x1f800054u &&
            std::getenv("MAMERECOMP_NAOMI_TRACE_BSC") && g_bsc_trace_events++ < 64)
            std::fprintf(stderr, "SH-4 BSC W [%08x] <- %08x (%u)\n",
                         address, value, width);
        if (decoded != kSh4Expevt && decoded != kSh4CpuVersion && decoded != kSh4Prr) {
            const auto offset = (decoded - kSh4InternalStart) / 4;
            if (offset < m_sh4_internal_regs.size()) m_sh4_internal_regs[offset] = value;
        }
        return;
    }
    if (m_devices.write && m_devices.write(physical, width, value, writer_pc)) return;
    const auto window = memoryWindow(physical);
    if (window) {
        const auto& [bytes, offset] = *window;
        if (offset + width > bytes->size()) {
            for (unsigned i = 0; i < width; ++i)
                write8(address + i, static_cast<std::uint8_t>(value >> (i * 8)), writer_pc);
            return;
        }
        if (bytes == &m_sound_ram && !m_sound_ram_dirty_pages.empty()) {
            constexpr std::size_t page_size = 0x1000u;
            const auto first_page = offset / page_size;
            const auto last_page = (offset + width - 1u) / page_size;
            for (auto page = first_page; page <= last_page; ++page)
                m_sound_ram_dirty_pages[page] = 1;
            m_sound_ram_trace_write_bytes += width;
        }
        for (unsigned i = 0; i < width; ++i)
            (*bytes)[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
        return;
    }

    const bool device_window =
        (physical >= 0x005f6800 && physical <= 0x005f9fff) ||
        (physical >= kSoundRegsStart && physical < kSoundRegsStart + 0x8000) ||
        (physical >= 0x01000000 && physical < 0x02000000) ||
        (physical >= 0x10000000 && physical < 0x14000000);
    if (device_window && m_devices.write && m_devices.write(physical, width, value, writer_pc)) return;
    traceUnmappedMmio('W', address, physical, width, value);
}

std::uint8_t NaomiAddressSpace::read8(std::uint32_t address) {
    return static_cast<std::uint8_t>(read(address, 1));
}
std::uint16_t NaomiAddressSpace::read16(std::uint32_t address) {
    return static_cast<std::uint16_t>(read(address, 2));
}
std::uint32_t NaomiAddressSpace::read32(std::uint32_t address) {
    return read(address, 4);
}
void NaomiAddressSpace::write8(std::uint32_t address, std::uint8_t value,
                              std::uint32_t writer_pc) {
    write(address, 1, value, writer_pc);
}
void NaomiAddressSpace::write16(std::uint32_t address, std::uint16_t value,
                                std::uint32_t writer_pc) {
    write(address, 2, value, writer_pc);
}
void NaomiAddressSpace::write32(std::uint32_t address, std::uint32_t value,
                                std::uint32_t writer_pc) {
    write(address, 4, value, writer_pc);
}

void NaomiAddressSpace::loadBios(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != m_bios.size())
        throw std::invalid_argument("Naomi BIOS must be exactly 2 MiB");
    std::copy(bytes.begin(), bytes.end(), m_bios.begin());
}

void NaomiAddressSpace::loadM2RomBoard(
    std::span<const std::uint8_t> bytes, std::uint32_t key,
    std::span<const std::uint8_t> board_id_eeprom) {
    if (bytes.empty()) throw std::invalid_argument("Naomi M2 ROM region is empty");
    m_board_id_eeprom.load(board_id_eeprom);
    m_board_id_eeprom_loaded = true;
    m_rom_board = bytes;
    m_m2_key = key;
    m_rom_pio_offset = 0;
    m_rom_pio_autoincrement = false;
    m_dma_offset = 0;
    m_dma_count = 0xffff;
    std::fill(m_m2_ram.begin(), m_m2_ram.end(), 0);
    m2crypt_reset();
    m2crypt_set_key(m_m2_key);
    m2crypt_set_ciphered_reader([this](std::uint32_t word_address) {
        std::uint32_t byte_offset = word_address * 2u;
        if ((word_address & 0xffff0000u) == 0x01000000u) {
            byte_offset = 2u * (word_address & 0x7fffu);
            if (byte_offset + 1u >= m_m2_ram.size()) return std::uint16_t{0xffff};
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(m_m2_ram[byte_offset + 1u]) << 8) |
                m_m2_ram[byte_offset]);
        }
        if (byte_offset + 1u >= m_rom_board.size()) return std::uint16_t{0xffff};
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(m_rom_board[byte_offset + 1u]) << 8) |
            m_rom_board[byte_offset]);
    });
}

void NaomiAddressSpace::advanceSh4Cycles(std::uint32_t cycles) {
    m_aica_sample_cycle_remainder += cycles;
    while (m_aica_sample_cycle_remainder >= kAicaTickCycles) {
        m_aica_sample_cycle_remainder -= kAicaTickCycles;
        // Flycast's AicaUpdate calls arm::run(1): 512 ARM clocks, followed by
        // one timer/sample step. This stub models control/IRQ timing only;
        // it does not generate audio samples or emulate the AICA DSP/mixer.
        arm7::run_cycles(kAicaArmCyclesPerSample);
        aicaSampleStep();
    }
    for (std::size_t channel = 0; channel < m_tmu_tcnt.size(); ++channel) {
        if ((m_tmu_tstr & (1u << channel)) == 0) continue;
        const auto prescale = m_tmu_tcr[channel] & 7u;
        if (prescale > 4u) continue; // RTC/external/reserved clock sources are not wired here.
        const std::uint32_t divisor = 4u << prescale;
        const std::uint64_t elapsed = static_cast<std::uint64_t>(m_tmu_cycle_remainder[channel]) + cycles;
        std::uint64_t ticks = elapsed / divisor;
        m_tmu_cycle_remainder[channel] = static_cast<std::uint32_t>(elapsed % divisor);
        while (ticks != 0) {
            const std::uint64_t until_underflow = static_cast<std::uint64_t>(m_tmu_tcnt[channel]) + 1u;
            if (ticks < until_underflow) {
                m_tmu_tcnt[channel] -= static_cast<std::uint32_t>(ticks);
                break;
            }
            ticks -= until_underflow;
            m_tmu_tcr[channel] |= 0x0100u; // UNF, cleared when software writes zero.
            m_tmu_tcnt[channel] = m_tmu_tcor[channel];
        }
    }
    m_bsc_cycle_remainder += cycles;
    while (m_bsc_cycle_remainder >= kSh4RtcTickCycles) {
        m_bsc_cycle_remainder -= kSh4RtcTickCycles;
        const auto mcr = m_bsc_registers[(kSh4BscMcr - kSh4BscStart) / 4];
        if ((mcr & 0x4u) == 0 || (mcr & 0x2u) != 0) continue;

        m_bsc_rfcr = static_cast<std::uint16_t>((m_bsc_rfcr + 1u) & 0x03ffu);
        if (((m_bsc_registers[(kSh4BscRtcsr - kSh4BscStart) / 4] & 1u) != 0 &&
             m_bsc_rfcr == 512u) || m_bsc_rfcr == 0u) {
            m_bsc_rfcr = 0;
            m_bsc_registers[(kSh4BscRtcsr - kSh4BscStart) / 4] |= 4u;
        }
        m_bsc_registers[(kSh4BscRfcr - kSh4BscStart) / 4] = m_bsc_rfcr;
    }
}

} // namespace mamerecomp::naomi
