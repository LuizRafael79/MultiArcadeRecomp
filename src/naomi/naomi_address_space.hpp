#pragma once

#include "naomi_sh4_context.hpp"
#include "naomi_x76f100.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace mamerecomp::naomi {

struct NaomiDeviceBus {
    // Device callbacks receive physical SH-4 addresses after P1/P2 alias
    // normalization. Return nullopt/false when the address is not theirs.
    std::function<std::optional<std::uint32_t>(std::uint32_t, unsigned)> read;
    std::function<bool(std::uint32_t, unsigned, std::uint32_t, std::uint32_t)> write;
};

// Naomi 1 main memory map shared by the MvC2 cartridge profiles. MAME map:
// src/mame/sega/naomi.cpp::naomi_map. Device windows are delegated through
// NaomiDeviceBus so PVR2, Holly, cartridge, and AICA can be attached separately.
class NaomiAddressSpace {
public:
    explicit NaomiAddressSpace(NaomiDeviceBus devices = {});
    ~NaomiAddressSpace();
    Sh4Bus make_sh4_bus();

    std::uint8_t read8(std::uint32_t address);
    std::uint16_t read16(std::uint32_t address);
    std::uint32_t read32(std::uint32_t address);
    void write8(std::uint32_t address, std::uint8_t value, std::uint32_t writer_pc = 0);
    void write16(std::uint32_t address, std::uint16_t value, std::uint32_t writer_pc = 0);
    void write32(std::uint32_t address, std::uint32_t value, std::uint32_t writer_pc = 0);
    void loadBios(std::span<const std::uint8_t> bytes);
    // Attach the assembled MAME ROM region to the Naomi M2 G1 PIO window.
    // The caller retains the byte storage for the lifetime of this address space.
    void loadM2RomBoard(std::span<const std::uint8_t> bytes, std::uint32_t key,
                        std::span<const std::uint8_t> board_id_eeprom);
    // Advance the SH-4 BSC refresh counter on its 128 Hz RTC cadence.
    void advanceSh4Cycles(std::uint32_t cycles);

    [[nodiscard]] std::vector<std::uint8_t>& mainRam() { return m_main_ram; }
    [[nodiscard]] const std::vector<std::uint8_t>& mainRam() const { return m_main_ram; }
    [[nodiscard]] std::vector<std::uint8_t>& soundRam() { return m_sound_ram; }

private:
    static std::uint32_t physicalAddress(std::uint32_t address);
    std::optional<std::pair<std::vector<std::uint8_t>*, std::uint32_t>> memoryWindow(
        std::uint32_t physical) const;
    std::uint32_t read(std::uint32_t address, unsigned width);
    void write(std::uint32_t address, unsigned width, std::uint32_t value,
               std::uint32_t writer_pc = 0);
    std::uint16_t readM2RomWord(std::uint32_t offset);
    std::uint16_t readM2Port(std::uint32_t offset);
    void writeM2Port(std::uint32_t offset, std::uint16_t value);
    std::uint32_t readAicaArm(std::uint32_t address, unsigned width);
    void writeAicaArm(std::uint32_t address, unsigned width, std::uint32_t value,
                      std::uint32_t writer_pc);
    void writeAicaRegister(std::uint32_t offset, unsigned width, std::uint32_t value,
                           bool from_arm);
    void updateAicaArmInterrupts();
    void aicaSampleStep();

    NaomiDeviceBus m_devices;
    std::vector<std::uint8_t> m_sram;
    std::vector<std::uint8_t> m_bios;
    std::span<const std::uint8_t> m_rom_board;
    std::uint32_t m_m2_key = 0;
    std::uint32_t m_rom_pio_offset = 0;
    bool m_rom_pio_autoincrement = false;
    std::uint32_t m_dma_offset = 0;
    std::uint16_t m_dma_count = 0xffff;
    NaomiX76F100 m_board_id_eeprom;
    bool m_board_id_eeprom_loaded = false;
    std::vector<std::uint8_t> m_m2_ram;
    // SH-4 on-chip operand RAM. MAME's SH-4 map exposes two 4 KiB banks
    // at 0x1c000000 and 0x1e000000 with address mirrors.
    std::vector<std::uint8_t> m_onchip_ram_p0;
    std::vector<std::uint8_t> m_onchip_ram_p1;
    std::vector<std::uint32_t> m_sh4_internal_regs;
    std::array<std::uint32_t, 0x60 / 4> m_bsc_registers{};
    std::uint16_t m_bsc_rfcr = 0;
    std::uint64_t m_bsc_cycle_remainder = 0;
    std::uint8_t m_tmu_tocr = 0;
    std::uint8_t m_tmu_tstr = 0;
    std::array<std::uint32_t, 3> m_tmu_tcor{0xffffffffu, 0xffffffffu, 0xffffffffu};
    std::array<std::uint32_t, 3> m_tmu_tcnt{0xffffffffu, 0xffffffffu, 0xffffffffu};
    std::array<std::uint16_t, 3> m_tmu_tcr{};
    std::array<std::uint32_t, 3> m_tmu_cycle_remainder{};
    // SH-4 SCIF2 starts with TEND and TDFE set (Flycast reset value 0x0060).
    std::uint16_t m_scif2_status = 0x0060;
    std::uint16_t m_scif2_status_last_read = 0x0060;
    std::array<std::uint32_t, 0x100 / 4> m_g1_registers{};
    // Naomi AICA reset/voltage registers and a bounded ARM7-visible SCSP
    // register/interrupt model. The ARM7 executes from the shared sound RAM;
    // audio synthesis and the AICA DSP/mixer are intentionally not connected.
    std::uint8_t m_aica_arm_reset = 0;
    std::uint8_t m_aica_vreg = 0;
    bool m_aica_arm_enabled = false;
    std::array<std::uint8_t, 0x8000> m_aica_registers{};
    std::uint64_t m_aica_sample_cycle_remainder = 0;
    std::uint64_t m_aica_arm_mailbox_writes = 0;
    std::uint32_t m_aica_arm_mailbox_log_events = 0;
    std::uint32_t m_aica_arm_register_trace_events = 0;
    std::uint64_t m_aica_sample_ticks = 0;
    std::uint32_t m_aica_irq_trace_events = 0;
    std::array<std::uint32_t, 3> m_aica_timer_step{1, 1, 1};
    std::array<std::uint32_t, 3> m_aica_timer_countdown{1, 1, 1};
    bool m_aica_arm_interrupt_pending = false;
    bool m_aica_e68k_interrupt_out = false;
    std::uint32_t m_aica_pending_interrupt_level = 0;
    std::uint32_t m_aica_arm_interrupt_level = 0;
    std::uint32_t m_aica_arm_interrupt_mode = 0;
    std::vector<std::uint8_t> m_sound_ram;
    std::vector<std::uint8_t> m_sound_ram_dirty_pages;
    std::uint64_t m_sound_ram_trace_write_bytes = 0;
    std::vector<std::uint8_t> m_texture_ram;
    std::vector<std::uint8_t> m_frame_ram;
    std::vector<std::uint8_t> m_main_ram;
};

} // namespace mamerecomp::naomi
