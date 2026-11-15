#pragma once

#include "core/cpus/tms34010/tms34010.hpp"
#include "core/chips/dcs/host_interface.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace multiarcaderecomp::mk {

class SoundCommandInterface {
public:
    virtual ~SoundCommandInterface() = default;
    virtual void write_main_command(std::uint8_t value) noexcept = 0;
    virtual void reset_write(bool asserted) noexcept = 0;
};

using SoundHostInterface = multiarcaderecomp::dcs::HostInterface;

class ProtectionInterface {
public:
    virtual ~ProtectionInterface() = default;
    virtual bool read(std::uint32_t address, std::uint16_t& value) const noexcept = 0;
    virtual bool write(std::uint32_t address, std::uint16_t value) noexcept = 0;
};

// Midway T-Unit board bus. Addresses passed to this interface are TMS34010
// bit addresses; a 16-bit memory word therefore advances the address by 16.
// The map follows MAME's midtunit_base_state::main_map and
// midtunit_adpcm_state::main_adpcm_map, with the implementation kept local so
// it can be connected to the project's AOT CPU and later board devices.
class TUnit {
public:
    static constexpr unsigned frame_width = 506;
    static constexpr unsigned frame_height = 289;

    struct VideoDiagnostics {
        std::uint64_t scanlines{};
        std::uint64_t enabled_visible_scanlines{};
        std::uint64_t nonzero_source_words{};
        std::uint64_t scanout_unmapped_indices{};
        std::uint64_t scanout_colored_pixels{};
        std::uint64_t last_frame_colored_pixels{};
        std::uint64_t completed_frames{};
    };
    struct DmaDiagnostics {
        std::uint64_t register_writes{};
        std::uint64_t command_writes{};
        std::uint64_t commands{};
        std::uint64_t completed{};
        std::uint64_t output_pixels{};
        std::uint16_t last_register{};
        std::uint16_t last_value{};
        std::uint16_t last_command_write{};
        std::uint16_t current_command{};
        std::uint32_t last_source{};
        std::uint16_t last_command{};
        std::uint64_t cycles_remaining{};
    };
    struct DmaCommandRecord {
        std::uint64_t sequence{};
        std::uint32_t cpu_pc{};
        std::uint64_t cpu_cycles{};
        std::array<std::uint32_t, 30> cpu_registers{};
        std::uint16_t command{};
        std::uint16_t left_right_skip{};
        std::uint16_t x_start{};
        std::uint16_t y_start{};
        std::uint16_t width{};
        std::uint16_t height{};
        std::uint16_t palette{};
        std::uint16_t color{};
        std::uint16_t x_step{};
        std::uint16_t y_step{};
        std::uint16_t top_clip{};
        std::uint16_t bottom_clip{};
        std::uint16_t left_clip{};
        std::uint16_t right_clip{};
        std::uint16_t config{};
        std::uint32_t source{};
        std::uint64_t pixels_written{};
    };
    struct DisplayParams {
        bool enabled{};
        std::uint16_t vcount{};
        std::uint16_t visible_start{};
        std::uint16_t blank_start{};
        unsigned hblank_start{};
        unsigned hblank_end{};
        std::uint32_t row_address{};
        std::uint32_t column_address{};
    };
    static constexpr std::uint32_t main_rom_base = 0x1f800000u;
    static constexpr std::uint32_t main_rom_mirror = 0xff800000u;
    static constexpr std::uint32_t main_rom_window = 0x00800000u;
    static constexpr std::uint32_t internal_io_base = 0xc0000000u;
    static constexpr unsigned kRegHtotal = 3;

    TUnit(std::span<const std::uint8_t> main_rom,
          std::span<const std::uint8_t> graphics_rom,
          bool large_graphics_rom = false);

    multiarcaderecomp::tms34010::Bus cpu_bus() noexcept;

    std::uint16_t read16(std::uint32_t bit_address) noexcept;
    void write16(std::uint32_t bit_address, std::uint16_t value) noexcept;
    std::uint32_t read_pixel(std::uint32_t bit_address) noexcept;
    bool write_pixel(std::uint32_t bit_address, std::uint32_t value) noexcept;
    bool convert_src_xy(std::uint32_t packed_xy, std::uint32_t offset,
                        std::uint32_t* linear_address) const noexcept;
    bool convert_dst_xy(std::uint32_t packed_xy, std::uint32_t offset,
                        std::uint32_t* linear_address) const noexcept;

    void set_input(unsigned port, std::uint16_t active_low_value) noexcept;
    std::uint16_t input(unsigned port) const noexcept;
    std::array<std::uint64_t, 4> input_read_counts() const noexcept;
    std::array<std::uint32_t, 4> input_last_read_pc() const noexcept;
    void note_cpu_pc(std::uint32_t pc, std::uint64_t cycles = 0) noexcept;
    void note_cpu_state(multiarcaderecomp::tms34010::State& state) noexcept;
    std::uint64_t take_pre_advanced_cycles() noexcept;
    bool load_cmos(std::span<const std::uint8_t> data) noexcept;
    std::span<const std::uint8_t> cmos() const noexcept;
    void attach_sound_board(SoundCommandInterface* board) noexcept;
    void attach_dcs_board(SoundHostInterface* board) noexcept;
    void attach_protection(ProtectionInterface* protection) noexcept;
    std::uint8_t sound_command() const noexcept;
    bool sound_reset_asserted() const noexcept;
    std::uint16_t io_register(unsigned index) const noexcept;
    std::span<const std::uint16_t> video_ram() const noexcept;
    std::span<const std::uint16_t> palette_ram() const noexcept;
    std::span<const std::uint32_t> framebuffer() const noexcept;
    VideoDiagnostics video_diagnostics() const noexcept;
    std::uint64_t take_completed_frames() noexcept;
    void render_scanline(std::uint32_t row_address,
                         std::uint32_t column_address,
                         unsigned horizontal_blank_start,
                         unsigned horizontal_blank_end,
                         std::span<std::uint16_t> destination) const noexcept;
    DisplayParams current_display_params() const noexcept;
    // Mirror the TMS34010 scanline callback phases: update VCOUNT/DPYADR
    // before the board draws the current line, then advance DPYADR/VCOUNT.
    void begin_scanline() noexcept;
    void end_scanline() noexcept;
    void advance_scanline() noexcept;
    void advance_cycles(std::uint64_t cycles) noexcept;
    void acknowledge_interrupt(std::uint16_t mask) noexcept;
    bool display_interrupt_pending() const noexcept;
    bool cpu_halted() const noexcept;
    DmaDiagnostics dma_diagnostics() const noexcept;
    void enable_dma_history(bool enabled) noexcept;
    std::span<const DmaCommandRecord> dma_command_history() const noexcept;
    std::uint32_t palette_rgb(unsigned index) const noexcept;

private:
    static std::uint16_t bus_read16(void* opaque, std::uint32_t address) noexcept;
    static void bus_write16(void* opaque, std::uint32_t address,
                            std::uint16_t value) noexcept;
    static std::uint32_t bus_read_pixel(void* opaque,
                                        std::uint32_t address) noexcept;
    static bool bus_write_pixel(void* opaque, std::uint32_t address,
                                std::uint32_t value) noexcept;
    static bool bus_convert_src_xy(void* opaque, std::uint32_t packed_xy,
                                   std::uint32_t offset,
                                   std::uint32_t* result) noexcept;
    static bool bus_convert_dst_xy(void* opaque, std::uint32_t packed_xy,
                                   std::uint32_t offset,
                                   std::uint32_t* result) noexcept;
    static std::uint8_t bus_window_checking(void* opaque) noexcept;
    static bool bus_is_mutable_code(void* opaque,
                                    std::uint32_t bit_address) noexcept;
    static void bus_read_interrupt_state(void* opaque,
                                         std::uint16_t* host_control,
                                         std::uint16_t* pending,
                                         std::uint16_t* enabled) noexcept;

    std::uint16_t read_ram_word(std::span<const std::uint8_t> memory,
                                std::uint32_t byte_offset) const noexcept;
    void write_ram_word(std::span<std::uint8_t> memory,
                        std::uint32_t byte_offset,
                        std::uint16_t value) noexcept;
    std::uint16_t read_video_word(std::uint32_t byte_offset) const noexcept;
    void write_video_word(std::uint32_t byte_offset,
                          std::uint16_t value) noexcept;
    std::uint16_t read_graphics_word(std::uint32_t byte_offset) const noexcept;
    void write_dma_register(std::uint32_t word_offset,
                            std::uint16_t value) noexcept;
    void execute_dma() noexcept;
    void schedule_dma_completion(std::uint64_t pixels) noexcept;
    std::uint32_t read_graphics_bits(std::uint32_t bit_offset,
                                    unsigned width) const noexcept;

    std::vector<std::uint8_t> m_main_rom;
    std::vector<std::uint8_t> m_graphics_rom;
    // MAME enables this only for MK2's MIDTUNIT_VIDEO profile. It changes
    // control-bit-7 graphics bank selection and DMA source address decoding.
    bool m_large_graphics_rom{};
    std::vector<std::uint8_t> m_work_ram;
    std::vector<std::uint8_t> m_cmos;
    std::vector<std::uint16_t> m_video_ram;
    std::vector<std::uint16_t> m_palette;
    std::array<std::uint32_t, 0x8000> m_palette_rgb{};
    std::array<std::uint16_t, 32> m_io{};
    std::array<std::uint16_t, 32> m_dma{};
    // T-Unit shift-register callbacks move two 512-pixel scanlines (1024
    // 16-bit words) between the TMS34010's pixel shift register and local
    // video RAM. MAME stores the register on the CPU; the board adapter owns
    // it here because pixel accesses already enter through this board bus.
    mutable std::array<std::uint16_t, 1024> m_shift_register{};
    // MAME's mk input defaults: DSW is 0xfe7d (CMOS coinage source and
    // POST skip enabled); player/system inputs idle high.
    std::array<std::uint16_t, 4> m_inputs{0xffff, 0xffff, 0xffff, 0xfe7d};
    mutable std::array<std::uint64_t, 4> m_input_read_counts{};
    mutable std::array<std::uint32_t, 4> m_input_last_read_pc{};
    std::uint32_t m_debug_cpu_pc{};
    std::uint64_t m_debug_cpu_cycles{};
    std::array<std::uint32_t, 30> m_debug_cpu_registers{};
    multiarcaderecomp::tms34010::State* m_active_cpu_state{};
    std::uint64_t m_pre_advanced_cycles{};
    std::uint16_t m_control{};
    std::uint8_t m_video_bank{};
    bool m_cmos_write_enable{};
    std::uint32_t m_convsp{};
    std::uint32_t m_convdp{};
    mutable std::uint8_t m_mk_protection_index{};
    mutable std::uint8_t m_fake_sound_state{};
    std::uint8_t m_sound_command{0xff};
    bool m_sound_reset_asserted{};
    SoundCommandInterface* m_sound_board{};
    SoundHostInterface* m_dcs_board{};
    ProtectionInterface* m_protection{};
    bool m_cpu_halted{};
    std::uint64_t m_dma_cycles_remaining{};
    DmaDiagnostics m_dma_diagnostics{};
    std::vector<DmaCommandRecord> m_dma_command_history;
    bool m_dma_history_enabled{};
    // Scanout updates one line at a time; publish only complete frames to the
    // host so it cannot display a mixture of adjacent frames.
    std::vector<std::uint32_t> m_framebuffer;
    std::vector<std::uint32_t> m_presented_framebuffer;
    // Beam phase in sixteenths of a TMS34010 execution cycle. MAME divides
    // the 50 MHz input clock by eight for CPU execution, so the 4 MHz pixel
    // clock needs sixteenth-cycle precision for its 25/16 ratio.
    std::uint64_t m_video_cycle_remainder_sixteenths{};
    std::uint16_t m_active_htotal{252};
    VideoDiagnostics m_video_diagnostics{};
    std::uint64_t m_current_frame_colored_pixels{};
};

} // namespace multiarcaderecomp::mk
