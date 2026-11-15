#include "midway/vunit/vunit_machine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string_view>

namespace multiarcaderecomp::vunit {
namespace {
constexpr std::uint32_t kAddressMask = 0x00ffffff;
constexpr std::uint32_t kRomBase = 0x00c00000;
constexpr std::uint32_t kRomWords = 0x00400000;
constexpr std::uint64_t kMainCyclesPerSecond = 25'000'000;
// MAME configures a 33.333333 MHz video crystal, divided by two, with
// 666x432 total pixels. Keep the fractional refresh cadence instead of
// assuming 60 Hz (the measured frame rate is about 57.93 Hz).
constexpr std::uint64_t kVideoClockHz = 33'333'333;
constexpr std::uint64_t kHorizontalTotal = 666;
constexpr unsigned kTotalScanlines = 432;
constexpr std::uint64_t kCyclesPerFrameNumerator =
    kMainCyclesPerSecond * 2 * kHorizontalTotal * kTotalScanlines;
}

Machine::Machine(std::span<const std::uint32_t> main_rom_words,
                 std::span<const std::uint8_t> dcs_sound_region)
    : m_main_rom(main_rom_words.begin(), main_rom_words.end()),
      m_low_ram(0x20000), m_extra_ram(0x20000), m_tms_internal_ram(0x800),
      m_nvram(0x2000, 0xffffffffu), m_dcs(dcs_sound_region),
      m_cpu(m_cpu_state, {this, bus_read, bus_write, map_code_page}) {
    m_adc_shift = 24; // Crus'n USA v4.5 init_crusnusa_common.
    reset();
}

void Machine::reset() noexcept {
    std::fill(m_low_ram.begin(), m_low_ram.end(), 0);
    std::copy_n(m_main_rom.begin(), std::min(m_main_rom.size(), m_low_ram.size()),
                m_low_ram.begin());
    m_tms_control.fill(0);
    m_control_data = 0;
    m_comm_data = 0;
    m_comm_flags = 0;
    m_wheel_board_last = 0;
    m_wheel_board_u8_latch = 0;
    m_wheel_board_output = 0;
    m_galil_input = ":";
    m_galil_input_index = 0;
    m_galil_output.fill(0);
    m_galil_output_index = 0;
    m_cmos_protected = 1;
    m_frame_remainder = 0;
    m_audio_phase = 0;
    m_audio_synced_main_cycle = 0;
    m_audio_reset_asserted = true;
    m_dcs.set_reset(false);
    m_dcs.set_reset(true);
    m_cpu.reset();
}

tms320c31::Exit Machine::run_cycles(std::uint64_t cycles) noexcept {
    const auto before = m_cpu_state.cycles;
    const auto exit = m_cpu.run(cycles);
    const auto elapsed = m_cpu_state.cycles - before;
    m_adc.advance_cycles(elapsed);
    if (m_adc.interrupt_asserted()) m_cpu.set_irq(3, true);
    advance_audio_to(m_cpu_state.cycles);
    return exit;
}

void Machine::run_frame() noexcept {
    std::uint64_t target_frame_cycles =
        kCyclesPerFrameNumerator / kVideoClockHz;
    m_frame_remainder += kCyclesPerFrameNumerator % kVideoClockHz;
    if (m_frame_remainder >= kVideoClockHz) {
        ++target_frame_cycles;
        m_frame_remainder -= kVideoClockHz;
    }
    std::uint64_t previous = 0;
    const auto irq_line = m_video.video_register(0) & 0x1ff;
    for (unsigned line = 0; line < kTotalScanlines; ++line) {
        m_video.set_scanline(line);
        const std::uint64_t target = target_frame_cycles * (line + 1) / kTotalScanlines;
        std::uint64_t line_cycles = target - previous;
        previous = target;
        if (m_video.scanline_irq_enabled() && line == irq_line) {
            m_cpu.set_irq(0, true);
            if (line_cycles) { (void)run_cycles(1); --line_cycles; }
            m_cpu.set_irq(0, false);
        }
        if (line_cycles) (void)run_cycles(line_cycles);
    }
}

void Machine::set_inputs(std::uint16_t in0, std::uint16_t in1,
                         std::uint16_t dips) noexcept {
    m_input0 = in0;
    m_input1 = in1;
    m_dips = dips;
}

void Machine::set_analog(std::uint8_t wheel, std::uint8_t accelerator,
                         std::uint8_t brake) noexcept {
    m_adc_inputs = {wheel, accelerator, brake, 0xff};
    m_adc.set_inputs(wheel, accelerator, brake, 0xff);
}

std::vector<std::int16_t> Machine::take_audio_samples() noexcept {
    return m_dcs.take_samples();
}

std::vector<std::uint8_t> Machine::framebuffer_rgb() const {
    return m_video.visible_rgb();
}

RuntimeReport Machine::report() const noexcept {
    return {m_cpu_state.cycles, m_cpu_state.instructions,
            m_dcs.cpu().cycles, m_cpu_state.pc, m_cpu_state.stop_reason,
            m_video.dma_commands()};
}

void Machine::set_interpreter_only(bool enabled) noexcept {
    set_interpreter_modes(enabled, enabled);
}

void Machine::set_interpreter_modes(bool c31_only, bool adsp_only) noexcept {
    m_c31_interpreter_only = c31_only;
    m_adsp_interpreter_only = adsp_only;
    m_cpu.set_aot_enabled(!c31_only);
}

void Machine::load_nvram(std::span<const std::uint32_t> words) noexcept {
    std::copy_n(words.begin(), std::min(words.size(), m_nvram.size()), m_nvram.begin());
}

std::uint32_t Machine::bus_read(void* opaque, std::uint32_t address) noexcept {
    return static_cast<Machine*>(opaque)->read(address);
}
void Machine::bus_write(void* opaque, std::uint32_t address,
                        std::uint32_t value) noexcept {
    static_cast<Machine*>(opaque)->write(address, value);
}
const std::uint32_t* Machine::map_code_page(
        void* opaque, std::uint32_t page_word_address) noexcept {
    auto& machine = *static_cast<Machine*>(opaque);
    constexpr std::uint32_t page_words = 1u << 12;
    if (page_word_address < machine.m_low_ram.size() &&
        page_word_address + page_words <= machine.m_low_ram.size())
        return machine.m_low_ram.data() + page_word_address;
    if (page_word_address >= 0x00400000 &&
        page_word_address + page_words <= 0x00420000)
        return machine.m_extra_ram.data() + (page_word_address - 0x00400000);
    if (page_word_address >= kRomBase &&
        page_word_address + page_words <= kRomBase + machine.m_main_rom.size())
        return machine.m_main_rom.data() + (page_word_address - kRomBase);
    return nullptr;
}

std::uint32_t Machine::read(std::uint32_t address) noexcept {
    address &= kAddressMask;
    if (address < 0x00020000) {
        if (address == 0x0000c93e) m_cpu.consume_cycles(100); // MAME v4.5 speedup hook.
        return m_low_ram[address];
    }
    if (address >= 0x00400000 && address < 0x00420000)
        return m_extra_ram[address - 0x00400000];
    if (address >= 0x00809800 && address < 0x0080a000)
        return m_tms_internal_ram[address - 0x00809800];
    if (address >= 0x00808000 && address < 0x00808080)
        return tms_control_read(address - 0x00808000);
    if (address >= 0x00900000 && address < 0x00980000)
        return m_video.read_vram(address - 0x00900000);
    if (address == 0x00980000) return m_video.dma_queue_entries();
    if (address == 0x00980020) return m_video.scanline();
    if (address == 0x00980040) return m_video.page_control();
    if (address == 0x00980082 || address == 0x00980083) {
        m_video.trigger(address - 0x00980082);
        return 0;
    }
    if (address == 0x00990000) return 4;
    if (address == 0x00991030) return m_input1;
    if (address == 0x00991060)
        return (static_cast<std::uint32_t>(m_input0) << 16) | m_input0;
    if (address == 0x00992000) return m_dips;
    if (address == 0x00993000) {
        if (!(m_control_data & 0x40)) {
            const auto result = static_cast<std::uint32_t>(m_adc.read()) << m_adc_shift;
            m_cpu.set_irq(3, false);
            return result;
        }
        return 0xffffffffu;
    }
    if (address == 0x00995000) return static_cast<std::uint32_t>(m_wheel_board_output) << 8;
    if (address >= 0x00997000 && address <= 0x00997008) {
        const auto offset = address - 0x00997000;
        static const bool trace_comm = std::getenv("MULTIARCADERECOMP_VUNIT_TRACE_COMM") != nullptr;
        static std::uint64_t trace_count = 0;
        if (trace_comm && trace_count++ < 256)
            std::fprintf(stderr, "[V-Unit COMM] PC=%06x R[%06x] offset=%x data=%04x flags=%02x\n",
                m_cpu_state.last_pc, address, offset, m_comm_data, m_comm_flags);
        if (offset == 0) return m_comm_data << 16;
        return 0;
    }
    if (address >= 0x009c0000 && address < 0x009c2000)
        return m_nvram[address - 0x009c0000];
    if (address >= 0x009e0000 && address < 0x009e8000)
        return m_video.read_palette(address - 0x009e0000);
    if (address >= 0x00a00000 && address < 0x00c00000)
        return m_video.read_texture(address - 0x00a00000);
    if (address >= kRomBase && address < kRomBase + kRomWords) {
        const auto offset = address - kRomBase;
        return offset < m_main_rom.size() ? m_main_rom[offset] : 0xffffffffu;
    }
    return 0xffffffffu;
}

void Machine::write(std::uint32_t address, std::uint32_t value) noexcept {
    address &= kAddressMask;
    // Invalidate only writable regions that can supply executable code. In
    // particular, do not add a bitset operation to the high-volume video/DMA
    // writes. The C31 revalidates the exact word on its next AOT visit.
    if (address < 0x00020000 ||
        (address >= 0x00400000 && address < 0x00420000) ||
        (address >= 0x00809800 && address < 0x0080a000))
        m_cpu.invalidate_code_word(address);
    if (address < 0x00020000) {
        static const bool trace_ram = std::getenv("MULTIARCADERECOMP_VUNIT_TRACE_RAM") != nullptr;
        static std::uint64_t trace_count = 0;
        if (trace_ram && address >= 0x00009fe0 && address <= 0x0000a030 &&
            trace_count++ < 256) {
            std::fprintf(stderr, "[V-Unit RAM] PC=%06x W[%06x]=%08x old=%08x\n",
                m_cpu_state.last_pc, address, value, m_low_ram[address]);
        }
        m_low_ram[address] = value; return;
    }
    if (address >= 0x00400000 && address < 0x00420000) {
        m_extra_ram[address - 0x00400000] = value; return;
    }
    if (address >= 0x00809800 && address < 0x0080a000) {
        m_tms_internal_ram[address - 0x00809800] = value; return;
    }
    if (address == 0x00600000) { m_video.queue_word(value); return; }
    if (address >= 0x00808000 && address < 0x00808080) {
        tms_control_write(address - 0x00808000, value); return;
    }
    if (address >= 0x00900000 && address < 0x00980000) {
        m_video.write_vram(address - 0x00900000, value); return;
    }
    if (address >= 0x00980020 && address <= 0x0098002b) {
        m_video.write_video_register(address - 0x00980020, value); return;
    }
    if (address == 0x00980040) { m_video.set_page_control(value); return; }
    if (address == 0x00993000) {
        if (!(m_control_data & 0x20)) m_adc.write(static_cast<std::uint8_t>(value >> m_adc_shift));
        return;
    }
    if (address == 0x00994000) {
        advance_audio_to(m_cpu.current_cycles());
        m_control_data = static_cast<std::uint16_t>(value);
        // The crusnusa GAME entry uses the midvunit machine/configuration and
        // generic control_w(), where bit 1 drives DCS reset. Bit 11 belongs to
        // crusnwld_state::crusnwld_control_w() (Cruis'n World), not this set.
        const bool released = (m_control_data & 0x0002) != 0;
        m_dcs.set_reset(released);
        m_audio_reset_asserted = !released;
        return;
    }
    if (address == 0x00995020) {
        m_cmos_protected = ((value & 0x0c00) != 0x0c00);
        return;
    }
    if (address == 0x00995000) { wheel_board_write(value); return; }
    if (address >= 0x00997000 && address <= 0x00997008) {
        const auto offset = address - 0x00997000;
        static const bool trace_comm = std::getenv("MULTIARCADERECOMP_VUNIT_TRACE_COMM") != nullptr;
        static std::uint64_t trace_count = 0;
        if (trace_comm && trace_count++ < 256)
            std::fprintf(stderr, "[V-Unit COMM] PC=%06x W[%06x] offset=%x value=%08x data=%04x flags=%02x\n",
                m_cpu_state.last_pc, address, offset, value, m_comm_data, m_comm_flags);
        if (offset == 0) m_comm_data = value >> 16;
        else if (offset == 1) m_comm_flags = (value >> 24) & 0xe0;
        return;
    }
    if (address == 0x009a0000) {
        // MAME's dcs_data_w() yields to the scheduler so the ADSP can observe
        // each host command before a later write replaces the one-byte latch.
        // Catch the DSP up to this exact C31 timestamp before changing it.
        advance_audio_to(m_cpu.current_cycles());
        m_dcs.send_command(static_cast<std::uint8_t>(value)); return;
    }
    if (address >= 0x009c0000 && address < 0x009c2000) {
        if (!m_cmos_protected) m_nvram[address - 0x009c0000] = value;
        return;
    }
    if (address >= 0x009e0000 && address < 0x009e8000) {
        m_video.write_palette(address - 0x009e0000, value); return;
    }
    if (address >= 0x00a00000 && address < 0x00c00000) {
        m_video.write_texture(address - 0x00a00000, value); return;
    }
}

void Machine::wheel_board_write(std::uint32_t data) noexcept {
    // Port of MAME's DECODE0/74LS138/GALIL model in wheel_board_w().
    if ((data & 0x800) && !(m_wheel_board_last & 0x800)) {
        m_wheel_board_u8_latch = ((data & 1) << 6) | ((data & 2) << 4) |
                                ((data & 4) << 2) | ((data & 8) << 0);
    }

    if ((data & 0x200) && !(data & 0x400)) {
        const std::uint8_t arg = static_cast<std::uint8_t>(data);
        const std::uint8_t wa = static_cast<std::uint8_t>(
            ((m_wheel_board_u8_latch >> 6) & 1) |
            (((m_wheel_board_u8_latch >> 5) & 1) << 1) |
            (((m_wheel_board_u8_latch >> 4) & 1) << 2));
        if (m_wheel_board_u8_latch & 8) {
            switch (wa) {
            case 0:
                m_wheel_board_output = m_galil_input_index < m_galil_input.size()
                    ? static_cast<std::uint8_t>(m_galil_input[m_galil_input_index++]) : 0;
                break;
            case 1:
                if (arg != 0x0d) {
                    if (m_galil_output_index < m_galil_output.size())
                        m_galil_output[m_galil_output_index++] = static_cast<char>(arg);
                } else {
                    const std::string_view command(m_galil_output.data(), m_galil_output_index);
                    if (command.find("MG \"V\" IBO {$2.0}") != std::string_view::npos)
                        m_galil_input = "V$00";
                    else if (command.find("MG \"X\", _TSX {$2.0}") != std::string_view::npos)
                        m_galil_input = "X$00";
                    else if (command.find("MG \"Y\", _TSY {$2.0}") != std::string_view::npos)
                        m_galil_input = "Y$00";
                    else if (command.find("MG \"Z\", _TSZ {$2.0}") != std::string_view::npos)
                        m_galil_input = "Z$00";
                    else
                        m_galil_input = ":";
                    m_galil_input_index = 0;
                    std::fill_n(m_galil_output.begin(), m_galil_output_index, '\0');
                    m_galil_output_index = 0;
                }
                break;
            case 2:
                m_wheel_board_output = m_galil_input_index < m_galil_input.size() ? 0x80 : 0;
                break;
            case 4:
                m_galil_input = ":";
                m_galil_input_index = 0;
                m_galil_output.fill(0);
                m_galil_output_index = 0;
                break;
            default:
                break;
            }
        }
    }
    m_wheel_board_last = data;
}

std::uint32_t Machine::tms_control_read(std::uint32_t offset) noexcept {
    if (offset == 0x64) return m_cpu_state.primary_bus_control;
    if (offset == 0x24 || offset == 0x34) {
        const unsigned which = (offset >> 4) & 1;
        const auto elapsed = m_cpu_state.cycles - m_timer_origin[which];
        return static_cast<std::uint32_t>(elapsed * m_timer_rate[which] /
                                          kMainCyclesPerSecond);
    }
    return m_tms_control[offset];
}

void Machine::tms_control_write(std::uint32_t offset, std::uint32_t value) noexcept {
    m_tms_control[offset] = value;
    if (offset == 0x64) return; // MAME ignores writes to primary bus control.
    if (offset == 0x20 || offset == 0x30) {
        const unsigned which = (offset >> 4) & 1;
        if (value & 0x40) m_timer_origin[which] = m_cpu_state.cycles;
        m_timer_rate[which] = (value & 0x200) ? 25'000'000 : 10'000'000;
    }
}

void Machine::advance_audio_to(std::uint64_t main_cycle) noexcept {
    // C31 execution cycle is 40 ns; ADSP-2105 input clock is 10 MHz.
    if (main_cycle <= m_audio_synced_main_cycle) return;
    const auto main_cycles = main_cycle - m_audio_synced_main_cycle;
    m_audio_synced_main_cycle = main_cycle;
    m_audio_phase += main_cycles * 2;
    const auto audio_cycles = m_audio_phase / 5;
    m_audio_phase %= 5;
    if (!audio_cycles) return;
    const auto target = m_dcs.cpu().cycles + audio_cycles;
    if (!m_audio_reset_asserted)
        (void)m_dcs.run_until(target, m_adsp_interpreter_only);
    else m_dcs.advance_cycles(audio_cycles);
}

} // namespace multiarcaderecomp::vunit
