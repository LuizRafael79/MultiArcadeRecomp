#pragma once

#include "naomi_sh4_context.hpp"
#include "naomi_aica_dsp.hpp"
#include "naomi_aica_voices.hpp"
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
    // Device DMA is a separate bus master. Its writes must invalidate SH-4
    // translated code just like CPU stores, even though they bypass Sh4Context.
    void writeDma8(std::uint32_t address, std::uint8_t value);
    void writeDma16(std::uint32_t address, std::uint16_t value);
    void writeDma32(std::uint32_t address, std::uint32_t value);
    void setDmaWriteObserver(std::function<void(std::uint32_t, unsigned)> observer) {
        m_dma_write_observer = std::move(observer);
    }
    void loadBios(std::span<const std::uint8_t> bytes);
    // Attach the assembled MAME ROM region to the Naomi M2 G1 PIO window.
    // The caller retains the byte storage for the lifetime of this address space.
    void loadM2RomBoard(std::span<const std::uint8_t> bytes, std::uint32_t key,
                        std::span<const std::uint8_t> board_id_eeprom);
    void setG1DmaIrq(std::function<void(unsigned)> callback) {
        m_g1_dma_irq = callback;
    }
    void setAicaG2DmaIrq(std::function<void(unsigned)> callback) {
        m_aica_g2_dma_irq = std::move(callback);
    }
    void setAicaSh4Irq(std::function<void(bool)> callback) {
        m_aica_sh4_irq = std::move(callback);
    }
    // Advance the SH-4 BSC refresh counter on its 128 Hz RTC cadence.
    void advanceSh4Cycles(std::uint32_t cycles);
    [[nodiscard]] std::optional<Sh4PendingInterrupt> pendingSh4Interrupt() const;

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
    void startG1Dma(std::uint32_t writer_pc);
    void advanceG1Dma(std::uint32_t cycles);
    void finishG1Dma(bool aborted = false);
    void startAicaG2Dma();
    void finishAicaG2Dma();
    void observeDmaWrite(std::uint32_t address, unsigned width);
    std::uint32_t readAicaArm(std::uint32_t address, unsigned width);
    void writeAicaArm(std::uint32_t address, unsigned width, std::uint32_t value,
                      std::uint32_t writer_pc);
    void writeAicaRegister(std::uint32_t offset, unsigned width, std::uint32_t value,
                           bool from_arm);
    std::uint32_t readAicaRegister(std::uint32_t offset, unsigned width);
    void runAicaInternalDma();
    void updateAicaArmInterrupts();
    void updateAicaSh4Interrupts();
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
    // Flycast exposes the Naomi main board's X76F100 through BSC.PDTRA;
    // the cartridge has a separate serial ID device on its G1 board-ID port.
    NaomiX76F100 m_main_board_serial_id;
    std::vector<std::uint8_t> m_m2_ram;
    // SH-4 on-chip operand RAM. MAME's SH-4 map exposes two 4 KiB banks
    // at 0x1c000000 and 0x1e000000 with address mirrors.
    std::vector<std::uint8_t> m_onchip_ram_p0;
    std::vector<std::uint8_t> m_onchip_ram_p1;
    std::vector<std::uint32_t> m_sh4_internal_regs;
    // SH-4 DMAC has its own 0x1fa00000 register window, separate from the
    // general CCN internal-register aliases at 0x1f000000.
    std::array<std::uint32_t, 17> m_sh4_dmac_regs{};
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
    // Naomi ROM-board G1 DMA state. Flycast services the transfer in chunks
    // of at most 1 KiB at 10 SH-4 cycles per byte and raises Holly bit 14 on
    // completion. The source cursor is programmed through the cartridge's
    // M2 DMA offset ports and is distinct from its ROM PIO cursor.
    bool m_g1_dma_active = false;
    std::uint32_t m_g1_dma_destination = 0;
    std::uint32_t m_g1_dma_length = 0;
    std::uint32_t m_g1_dma_completed = 0;
    std::uint32_t m_g1_dma_cycle_remainder = 0;
    std::function<void(unsigned)> m_g1_dma_irq;
    std::function<void(unsigned)> m_aica_g2_dma_irq;
    std::function<void(std::uint32_t, unsigned)> m_dma_write_observer;
    std::uint64_t m_g1_dma_commands = 0;
    std::uint64_t m_g1_dma_bytes = 0;
    // Holly G2 channel 0: AICA sound RAM <-> system memory.
    std::array<std::uint32_t, 8> m_aica_g2_dma_registers{};
    bool m_aica_g2_dma_active = false;
    std::uint64_t m_aica_g2_dma_cycles_remaining = 0;
    // Naomi AICA register/interrupt state with Flycast ARM7 interpreter,
    // 64 channel voice engine and microcoded DSP interpreter.
    std::uint8_t m_aica_arm_reset = 0;
    std::uint8_t m_aica_vreg = 0;
    bool m_aica_arm_enabled = false;
    std::array<std::uint8_t, 0x8000> m_aica_registers{};
    NaomiAicaDsp m_aica_dsp;
    NaomiAicaVoices m_aica_voices;
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
    std::function<void(bool)> m_aica_sh4_irq;
    bool m_aica_sh4_irq_asserted = false;
    bool m_aica_internal_dma_active = false;
    std::vector<std::uint8_t> m_sound_ram;
    std::vector<std::uint8_t> m_sound_ram_dirty_pages;
    std::uint64_t m_sound_ram_trace_write_bytes = 0;
    std::vector<std::uint8_t> m_texture_ram;
    std::vector<std::uint8_t> m_frame_ram;
    std::vector<std::uint8_t> m_main_ram;
};

} // namespace mamerecomp::naomi
