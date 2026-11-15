#include "core/chips/dcs/audio_2k.hpp"

#include "core/cpus/adsp2105/runtime.hpp"
#include "core/cpus/adsp2105/dag.hpp"
#include "core/cpus/adsp2105/interpreter.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>

namespace multiarcaderecomp::dcs {
namespace {
constexpr std::uint16_t kInputEmpty = 0x0800;
constexpr std::uint16_t kOutputEmpty = 0x0400;
constexpr std::uint16_t kRomBase = 0x2000;
constexpr std::uint16_t kBankSelectBase = 0x3000;
constexpr std::uint16_t kLatchBase = 0x3400;
constexpr std::uint16_t kLocalRamBase = 0x3800;
constexpr std::uint16_t kControlBase = 0x3fe0;
constexpr std::uint16_t kProgramFlagData = 5;
constexpr std::uint16_t kProgramFlagControl = 6;
constexpr std::uint16_t kSport1Autobuffer = 15;
constexpr std::uint16_t kSport1Sclkdiv = 17;
constexpr std::uint16_t kSport1Control = 18;
constexpr std::uint16_t kSystemControl = 31;
// DCS dcs_irq() pulses ADSP2105_IRQ1 for the scheduler's minimum quantum.
// DCS raises the interleave floor to 500 ns, i.e. five cycles at 10 MHz.
constexpr std::uint32_t kSportIrq1PulseCycles = 5;
}

DcsAudio2K::DcsAudio2K(std::span<const std::uint8_t> sound_region)
    : m_sound_region(sound_region.begin(), sound_region.end()),
      m_internal_program(0x400), m_external_program(0x1800),
      m_program_written(0x2000), m_local_ram(0x200) {
    m_boot_program_history.reserve(0x4000);
    m_control[0] = 0;
    boot();
}

multiarcaderecomp::adsp2105::State& DcsAudio2K::cpu() noexcept { return m_cpu; }
const multiarcaderecomp::adsp2105::State& DcsAudio2K::cpu() const noexcept { return m_cpu; }

multiarcaderecomp::adsp2105::Bus DcsAudio2K::cpu_bus() noexcept {
    return {this, program_read, program_write, data_read, data_write,
            io_read, io_write, sport_tx, sport_rx, nullptr, mutable_program,
            false, true};
}

bool DcsAudio2K::program_word_was_written(std::uint16_t address) const noexcept {
    address &= multiarcaderecomp::adsp2105::pc_mask;
    return address < m_program_written.size() && m_program_written[address];
}

std::vector<std::pair<std::uint16_t, std::uint32_t>>
DcsAudio2K::program_overlay() const {
    auto words = m_boot_program_history;
    for (std::uint16_t address = 0; address < m_program_written.size(); ++address) {
        if (m_program_written[address])
            words.emplace_back(address, read_program(address));
    }
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    return words;
}

std::uint16_t DcsAudio2K::host_status() const noexcept {
    // T-Unit mk2_state::dcs_state_r returns DCS control_r() >> 4.
    return static_cast<std::uint16_t>(((m_input_full ? 0 : kInputEmpty) |
                                       (m_output_full ? 0 : kOutputEmpty)) >> 4);
}

std::uint16_t DcsAudio2K::host_data_read() noexcept {
    ++m_host_reads;
    // MAME's dcs_audio_device::data_r() charges 4444 main-CPU cycles when
    // the host polls an empty output latch. Keep the stall in the board
    // runtime and let the T-Unit dispatcher advance its shared clock.
    if (!m_output_full) {
        ++m_empty_output_reads;
        m_host_stall_cycles += 4444;
        m_host_stall_cycles_total += 4444;
    }
    const std::uint16_t result = m_output_latch & 0x00ff;
    if (m_output_full) acknowledge_output();
    return result;
}

std::uint64_t DcsAudio2K::consume_host_stall_cycles() noexcept {
    const auto cycles = m_host_stall_cycles;
    m_host_stall_cycles = 0;
    return cycles;
}

void DcsAudio2K::host_data_write(std::uint16_t data) noexcept {
    // The MK2 TMS map ignores the low mirrored word (offset 0); the caller
    // must pass the selected high word and the 16-bit write value.
    set_reset((data & 0x0100) != 0);
    send_command(static_cast<std::uint8_t>(data));
}

void DcsAudio2K::send_command(std::uint8_t command) noexcept {
    if (m_input_full) ++m_overwritten_commands;
    m_input_latch = command;
    m_input_full = true;
    m_cpu.irq[2] = true;
    m_cpu.irq_latched[2] = true;
    m_cpu_stalled = false;
    ++m_host_commands;
}

void DcsAudio2K::set_reset(bool released) noexcept {
    if (!released) {
        m_reset_asserted = true;
        m_sound_bank = 0;
        m_input_full = false;
        m_output_full = false;
        m_cpu.irq[2] = false;
        m_cpu.irq_latched[2] = false;
        std::fill(std::begin(m_control), std::end(m_control), 0);
        m_sport1_enabled = false;
        m_sport1_autobuffer = false;
        m_dma_active = false;
        m_dma_length = 0;
        m_dma_increment = 0;
        m_dma_sample_count = 0;
        m_dma_period_cycles = 0;
        m_dma_cycle_phase = 0;
        m_sport_irq1_pulse_remaining = 0;
        m_sport_irq1_pulse_start_cycle = 0;
        m_cpu.irq[1] = false;
        m_cpu.irq_latched[1] = false;
        boot();
        return;
    }
    m_reset_asserted = false;
}

multiarcaderecomp::adsp2105::BlockExit DcsAudio2K::run_until(
        std::uint64_t cycle_limit, bool interpreter_only,
        bool recompiler_only) noexcept {
    if (m_reset_asserted || m_cpu_stalled)
        return multiarcaderecomp::adsp2105::BlockExit::Idle;
    auto bus = cpu_bus();
    auto result = multiarcaderecomp::adsp2105::BlockExit::Budget;
    while (m_cpu.cycles < cycle_limit) {
        const auto slice_start = m_cpu.cycles;
        const auto activation_generation = m_dma_activation_generation;
        auto slice_limit = cycle_limit;
        if (m_dma_active && m_dma_period_cycles) {
            const auto until_dma = std::max<std::uint64_t>(
                1, m_dma_period_cycles - m_dma_cycle_phase);
            slice_limit = std::min(slice_limit, m_cpu.cycles + until_dma);
        }
        if (m_sport_irq1_pulse_remaining) {
            slice_limit = std::min<std::uint64_t>(
                slice_limit, m_cpu.cycles + m_sport_irq1_pulse_remaining);
        }

        result = interpreter_only
            ? [&] {
                while (m_cpu.cycles < slice_limit) {
                    (void)multiarcaderecomp::adsp2105::service_interrupt(m_cpu);
                    if (m_cpu.halted || m_cpu.idle) {
                        multiarcaderecomp::adsp2105::advance_clock(
                            m_cpu, bus, slice_limit - m_cpu.cycles);
                        return multiarcaderecomp::adsp2105::BlockExit::Idle;
                    }
                    const auto step = multiarcaderecomp::adsp2105::interpreter_step(
                        m_cpu, bus, slice_limit, MULTIARCADERECOMP_UNCOVERED_ENTRY);
                    if (step != multiarcaderecomp::adsp2105::BlockExit::Dispatch)
                        return step;
                }
                return multiarcaderecomp::adsp2105::BlockExit::Budget;
            }()
            : multiarcaderecomp::adsp2105::run_until(
                  m_cpu, bus, slice_limit, MULTIARCADERECOMP_UNCOVERED_ENTRY,
                  !recompiler_only);

        if (m_dma_active) {
            // MAME's DCS IRQ timer reads the SPORT buffer at each half-buffer
            // boundary while the ADSP is running. Keep those reads interleaved
            // with DSP execution instead of sampling once after an entire V-Unit
            // frame, by which point the DSP may already have cleared/reused it.
            const auto dma_start = activation_generation != m_dma_activation_generation
                ? m_dma_activation_cycle : slice_start;
            if (m_cpu.cycles > dma_start)
                advance_cycles(m_cpu.cycles - dma_start);
        }

        if (m_sport_irq1_pulse_remaining) {
            const auto elapsed = m_cpu.cycles - m_sport_irq1_pulse_start_cycle;
            if (elapsed >= kSportIrq1PulseCycles) {
                m_sport_irq1_pulse_remaining = 0;
                m_cpu.irq[1] = false;
            } else {
                m_sport_irq1_pulse_remaining = kSportIrq1PulseCycles -
                    static_cast<std::uint32_t>(elapsed);
            }
        }

        if (result != multiarcaderecomp::adsp2105::BlockExit::Budget)
            break;
        if (m_cpu.cycles == slice_start)
            break;
    }
    if (result == multiarcaderecomp::adsp2105::BlockExit::NeedsInterpreter ||
        result == multiarcaderecomp::adsp2105::BlockExit::BusFault) {
        m_stopped_pc = m_cpu.pc & multiarcaderecomp::adsp2105::pc_mask;
        m_stopped_opcode = bus.read_program24
            ? bus.read_program24(bus.opaque, m_stopped_pc) &
                multiarcaderecomp::adsp2105::opcode_mask
            : 0;
        m_stop_reason = static_cast<std::uint8_t>(result);
    }
    if (result == multiarcaderecomp::adsp2105::BlockExit::NeedsInterpreter)
        m_cpu_stalled = true;
    return result;
}

std::vector<std::int16_t> DcsAudio2K::take_samples() noexcept {
    std::vector<std::int16_t> out;
    out.swap(m_samples);
    return out;
}

void DcsAudio2K::advance_cycles(std::uint64_t cycles) noexcept {
    if (!m_dma_active || !m_dma_period_cycles || !m_dma_sample_count) return;
    m_dma_cycle_phase += cycles;
    while (m_dma_cycle_phase >= m_dma_period_cycles) {
        m_dma_cycle_phase -= m_dma_period_cycles;
        std::uint32_t address = m_cpu.i[m_dma_ireg] & 0x3fff;
        for (std::uint32_t n = 0; n < m_dma_sample_count; ++n) {
            m_dma_last_address = static_cast<std::uint16_t>(address);
            const auto sample = static_cast<std::int16_t>(read_data(address));
            m_samples.push_back(sample);
            m_last_sample = sample;
            if (sample != 0) ++m_nonzero_samples;
            const auto magnitude = static_cast<std::uint16_t>(
                sample == std::numeric_limits<std::int16_t>::min()
                    ? 32768 : std::abs(static_cast<int>(sample)));
            m_sample_peak = std::max(m_sample_peak, magnitude);
            address = (address + m_dma_increment) & 0x3fff;
        }
        m_sport_words += m_dma_sample_count;
        // MAME refreshes the circular-buffer origin from the DAG base at
        // each SPORT timer tick. The initial aligned transmit address is not
        // the permanent wrap base.
        m_dma_base = m_cpu.base[m_dma_ireg];
        const std::uint32_t end = static_cast<std::uint32_t>(m_dma_base) +
                                  m_dma_length;
        if (address >= end) {
            address = m_dma_base;
            // MAME pulses ADSP2105_IRQ1 for minimum_quantum_time(). Keep the
            // line asserted while the DSP executes that quantum; the ADSP
            // core selects the line or its edge latch according to ICNTL.
            m_cpu.irq[1] = true;
            m_cpu.irq_latched[1] = true;
            m_sport_irq1_pulse_remaining = kSportIrq1PulseCycles;
            m_sport_irq1_pulse_start_cycle = m_cpu.cycles;
            ++m_sport_irq1_pulses;
        }
        // MAME updates I through set_state_int(), which refreshes the DAG
        // base as well as the visible index register.
        multiarcaderecomp::adsp2105::dag_write_i(m_cpu, m_dma_ireg,
            static_cast<std::uint16_t>(address));
    }
}

DcsAudio2KDiagnostics DcsAudio2K::diagnostics() const noexcept {
    const auto nonzero_local_words = static_cast<std::uint64_t>(
        std::count_if(m_local_ram.begin(), m_local_ram.end(),
                      [](std::uint16_t value) { return value != 0; }));
    const auto nonzero_shared_words = static_cast<std::uint64_t>(
        std::count_if(m_external_program.begin(), m_external_program.begin() + 0x800,
                      [](std::uint32_t value) { return (value >> 8) != 0; }));
    std::uint64_t stream_nonzero_words = 0;
    std::uint16_t stream_min = 0xffff;
    std::uint16_t stream_max = 0;
    std::uint64_t stream_writes = 0;
    std::uint64_t stream_nonzero_writes = 0;
    for (std::uint32_t n = 0; n < m_dma_length; ++n) {
        const auto address = (m_dma_base + n) & 0x3fff;
        std::uint16_t value = 0;
        if (address < 0x2000) {
            const auto offset = address & 0x07ff;
            value = static_cast<std::uint16_t>(m_external_program[offset] >> 8);
            stream_writes += m_shared_ram_write_counts[offset];
            stream_nonzero_writes += m_shared_ram_nonzero_write_counts[offset];
        } else if (address >= kLocalRamBase && address < 0x3a00) {
            const auto offset = address - kLocalRamBase;
            value = m_local_ram[offset];
            stream_writes += m_local_ram_write_counts[offset];
            stream_nonzero_writes += m_local_ram_nonzero_write_counts[offset];
        }
        if (value) ++stream_nonzero_words;
        stream_min = std::min(stream_min, value);
        stream_max = std::max(stream_max, value);
    }
    if (!m_dma_length) stream_min = 0;
    DcsAudio2KDiagnostics d{};
    d.host_commands = m_host_commands;
    d.overwritten_commands = m_overwritten_commands;
    d.host_reads = m_host_reads;
    d.empty_output_reads = m_empty_output_reads;
    d.host_stall_cycles_total = m_host_stall_cycles_total;
    d.input_reads = m_input_reads;
    d.output_writes = m_output_writes;
    d.sound_rom_reads = m_sound_rom_reads;
    d.sound_rom_nonzero_reads = m_sound_rom_nonzero_reads;
    d.sound_rom_ffff_reads = m_sound_rom_ffff_reads;
    d.sound_bank_writes = m_sound_bank_writes;
    d.sport_irq1_pulses = m_sport_irq1_pulses;
    d.timer_scale = m_control[0x1b];
    d.timer_count = m_cpu.control[0x1c];
    d.timer_period = m_control[0x1d];
    d.timer_phase = static_cast<std::uint32_t>(m_cpu.timer_scale_counter);
    d.timer_enabled = (m_cpu.mstat & 0x20) != 0;
    d.timer_irq_pending = m_cpu.irq_latched[5];
    d.adsp_icntl = m_cpu.icntl;
    d.adsp_imask = m_cpu.imask;
    d.sport_irq1_active = m_cpu.irq[1];
    d.local_ram_writes = m_local_ram_writes;
    d.local_ram_nonzero_words = nonzero_local_words;
    d.shared_ram_writes = m_shared_ram_writes;
    d.shared_ram_nonzero_words = nonzero_shared_words;
    d.stream_buffer_nonzero_words = stream_nonzero_words;
    d.stream_buffer_writes = stream_writes;
    d.stream_buffer_nonzero_writes = stream_nonzero_writes;
    d.stream_buffer_last_write_address = m_stream_last_write_address;
    d.stream_buffer_last_write_value = m_stream_last_write_value;
    d.stream_buffer_last_write_pc = m_stream_last_write_pc;
    d.stream_buffer_last_write_opcode = m_stream_last_write_opcode;
    d.stream_buffer_last_write_i7 = m_stream_last_write_i7;
    d.stream_buffer_last_write_m7 = m_stream_last_write_m7;
    d.stream_buffer_last_write_l7 = m_stream_last_write_l7;
    d.stream_buffer_last_write_ax0 = m_stream_last_write_ax0;
    d.stream_buffer_last_write_ay0 = m_stream_last_write_ay0;
    d.stream_buffer_last_write_ar = m_stream_last_write_ar;
    d.shared_ram_program_writes = m_shared_ram_program_writes;
    d.shared_ram_data_writes = m_shared_ram_data_writes;
    d.stream_buffer_min = stream_min;
    d.stream_buffer_max = stream_max;
    d.shared_ram_last_address = m_shared_ram_last_address;
    d.shared_ram_last_value = m_shared_ram_last_value;
    d.shared_ram_last_pc = m_shared_ram_last_pc;
    d.local_ram_last_address = m_local_ram_last_address;
    d.local_ram_last_value = m_local_ram_last_value;
    d.local_ram_last_pc = m_local_ram_last_pc;
    d.sound_rom_last_address = m_sound_rom_last_address;
    d.sound_rom_last_value = m_sound_rom_last_value;
    d.sound_bank_last_value = m_sound_bank_last_value;
    d.boot_count = m_boot_count;
    d.sport_words = m_sport_words;
    d.nonzero_samples = m_nonzero_samples;
    d.sample_peak = m_sample_peak;
    d.last_sample = m_last_sample;
    d.stream_ireg = m_dma_ireg;
    d.stream_base = m_dma_base;
    d.stream_length = m_dma_length;
    d.stream_increment = m_dma_increment;
    d.stream_address = m_dma_last_address;
    d.stopped_pc = m_stopped_pc;
    d.stopped_opcode = m_stopped_opcode;
    d.stop_reason = m_stop_reason;
    d.sound_bank = m_sound_bank;
    d.software_reboot_count = m_software_reboot_count;
    d.last_software_reboot_pc = m_last_software_reboot_pc;
    d.last_software_reboot_value = m_last_software_reboot_value;
    d.syscontrol_writes = m_syscontrol_writes;
    d.last_syscontrol_pc = m_last_syscontrol_pc;
    d.last_syscontrol_value = m_last_syscontrol_value;
    d.input_latch = m_input_latch;
    d.output_latch = m_output_latch;
    d.input_full = m_input_full;
    d.output_full = m_output_full;
    d.reset_asserted = m_reset_asserted;
    d.cpu_stalled = m_cpu_stalled;
    return d;
}

std::uint32_t DcsAudio2K::program_read(void* opaque, std::uint16_t address) noexcept {
    return static_cast<DcsAudio2K*>(opaque)->read_program(address);
}
void DcsAudio2K::program_write(void* opaque, std::uint16_t address,
                          std::uint32_t value) noexcept {
    static_cast<DcsAudio2K*>(opaque)->write_program(address, value);
}
std::uint16_t DcsAudio2K::data_read(void* opaque, std::uint16_t address) noexcept {
    return static_cast<DcsAudio2K*>(opaque)->read_data(address);
}
void DcsAudio2K::data_write(void* opaque, std::uint16_t address,
                       std::uint16_t value) noexcept {
    static_cast<DcsAudio2K*>(opaque)->write_data(address, value);
}
std::uint16_t DcsAudio2K::io_read(void* opaque, std::uint16_t address) noexcept {
    auto& self = *static_cast<DcsAudio2K*>(opaque);
    return static_cast<std::uint16_t>(self.m_control[address & 31]);
}
void DcsAudio2K::io_write(void* opaque, std::uint16_t address,
                      std::uint16_t value) noexcept {
    auto& self = *static_cast<DcsAudio2K*>(opaque);
    self.m_control[address & 31] = value;
}
void DcsAudio2K::sport_tx(void* opaque, unsigned port, std::uint32_t value) noexcept {
    auto& self = *static_cast<DcsAudio2K*>(opaque);
    if (port != 1) return;
    (void)value;
    if (!self.m_sport1_enabled || !self.m_sport1_autobuffer) {
        self.m_dma_active = false;
        return;
    }
    const auto autobuffer = self.m_control[kSport1Autobuffer];
    self.m_dma_ireg = static_cast<std::uint8_t>((autobuffer >> 9) & 7);
    const unsigned mreg = ((autobuffer >> 7) & 3) | (self.m_dma_ireg & 4);
    const std::int32_t increment = self.m_cpu.m[mreg];
    self.m_dma_length = self.m_cpu.l[self.m_dma_ireg];
    self.m_dma_increment = static_cast<std::uint32_t>(increment > 0 ? increment : 1);
    auto source = static_cast<std::uint16_t>(self.m_cpu.i[self.m_dma_ireg] & ~0x0f);
    self.m_dma_first_address = source;
    self.m_dma_last_address = source;
    // MAME's sound_tx_callback writes the aligned address directly to Ireg.
    // Applying DAG post-modify here advances/wraps the pointer a second time
    // and can make the SPORT reader start from the wrong part of local RAM.
    multiarcaderecomp::adsp2105::dag_write_i(self.m_cpu, self.m_dma_ireg, source);
    // MAME's DCS SPORT callback keeps the aligned start address as the
    // autobuffer origin. The ADSP DAG base is a separate circular-address
    // register and is not the DAC buffer base.
    self.m_dma_base = source;
    self.m_dma_sample_count = self.m_dma_length /
        (2u * std::max(1u, self.m_dma_increment));
    const std::uint32_t count = std::max(1u, self.m_dma_sample_count);
    const std::uint64_t sample_period_cycles =
        (self.m_control[kSport1Control] & 0x4000)
            ? 2ull * (self.m_control[kSport1Sclkdiv] + 1ull) * 16ull
            : 10'000'000ull / 31'250ull;
    self.m_dma_period_cycles = std::max<std::uint64_t>(1,
        sample_period_cycles * count);
    self.m_dma_cycle_phase = 0;
    self.m_dma_active = self.m_dma_length != 0 && increment > 0;
    self.m_dma_activation_cycle = self.m_cpu.cycles;
    ++self.m_dma_activation_generation;
}
std::uint32_t DcsAudio2K::sport_rx(void*, unsigned) noexcept { return 0; }
bool DcsAudio2K::mutable_program(void* opaque, std::uint16_t address) noexcept {
    return static_cast<DcsAudio2K*>(opaque)->program_word_was_written(address);
}

std::uint32_t DcsAudio2K::read_program(std::uint16_t address) const noexcept {
    address &= multiarcaderecomp::adsp2105::pc_mask;
    if (address < m_internal_program.size()) return m_internal_program[address];
    if (address >= 0x0800 && address < 0x2000)
        return m_external_program[address - 0x0800];
    return 0x00ffffff;
}

void DcsAudio2K::write_program(std::uint16_t address, std::uint32_t value) noexcept {
    address &= multiarcaderecomp::adsp2105::pc_mask;
    value &= 0x00ffffff;
    if (address == 0x014b && std::getenv("MULTIARCADERECOMP_ADSP_TRACE_PROGRAM_WRITE")) {
        static std::uint32_t trace_lines = 0;
        if (trace_lines < 256) {
            ++trace_lines;
            const auto source_pc = m_cpu.previous_pc & multiarcaderecomp::adsp2105::pc_mask;
            const auto source_opcode = read_program(source_pc);
            const auto& c = m_cpu.core;
            std::fprintf(stderr,
                "[DCS program write] target=%04X value=%06X source-PC=%04X "
                "source-OP=%06X I=%04X,%04X,%04X,%04X M=%04X,%04X,%04X,%04X "
                "L=%04X,%04X,%04X,%04X BASE=%04X,%04X,%04X,%04X "
                "AR=%04X AF=%04X AX=%04X/%04X AY=%04X/%04X PX=%02X\n",
                address, value, source_pc, source_opcode,
                m_cpu.i[4], m_cpu.i[5], m_cpu.i[6], m_cpu.i[7],
                static_cast<std::uint16_t>(m_cpu.m[4]),
                static_cast<std::uint16_t>(m_cpu.m[5]),
                static_cast<std::uint16_t>(m_cpu.m[6]),
                static_cast<std::uint16_t>(m_cpu.m[7]),
                m_cpu.l[4], m_cpu.l[5], m_cpu.l[6], m_cpu.l[7],
                m_cpu.base[4], m_cpu.base[5], m_cpu.base[6], m_cpu.base[7],
                static_cast<std::uint16_t>(c.ar),
                static_cast<std::uint16_t>(c.af),
                static_cast<std::uint16_t>(c.ax[0]),
                static_cast<std::uint16_t>(c.ax[1]),
                static_cast<std::uint16_t>(c.ay[0]),
                static_cast<std::uint16_t>(c.ay[1]), m_cpu.px);
        }
    }
    if (address < m_internal_program.size()) {
        m_internal_program[address] = value;
        m_program_written[address] = true;
    } else if (address >= 0x0800 && address < 0x2000) {
        const auto offset = static_cast<std::uint16_t>(address - 0x0800);
        m_external_program[offset] = value;
        if (offset < 0x0800) {
            ++m_shared_ram_write_counts[offset];
            if ((value >> 8) != 0) ++m_shared_ram_nonzero_write_counts[offset];
            ++m_shared_ram_writes;
            m_shared_ram_last_address = offset;
            m_shared_ram_last_value = static_cast<std::uint16_t>(value >> 8);
            m_shared_ram_last_pc = m_cpu.previous_pc;
            ++m_shared_ram_program_writes;
        }
        m_program_written[address] = true;
    }
}

std::uint16_t DcsAudio2K::read_data(std::uint16_t address) noexcept {
    address &= multiarcaderecomp::adsp2105::pc_mask;
    // MAME maps 0x0000-0x07ff with mirror mask 0x1800. All four windows
    // 0x0000, 0x0800, 0x1000 and 0x1800 therefore alias the same 2 Kiword
    // shared data/program RAM. The ROM bank begins only at 0x2000.
    if (address < kRomBase) {
        const auto offset = address & 0x07ff;
        return static_cast<std::uint16_t>(m_external_program[offset] >> 8);
    }
    if (address >= kRomBase && address < 0x3000) {
        const std::size_t word = static_cast<std::size_t>(m_sound_bank) * 0x1000 +
                                 (address - kRomBase);
        const std::size_t byte = word * 2;
        const auto value = byte + 1 < m_sound_region.size()
            ? static_cast<std::uint16_t>(m_sound_region[byte] |
                  (static_cast<std::uint16_t>(m_sound_region[byte + 1]) << 8))
            : std::uint16_t{0xffff};
        ++m_sound_rom_reads;
        if (value) ++m_sound_rom_nonzero_reads;
        if (value == 0xffff) ++m_sound_rom_ffff_reads;
        m_sound_rom_last_address = address;
        m_sound_rom_last_value = value;
        return value;
    }
    if (address >= kLatchBase && address < 0x3800) {
        ++m_input_reads;
        const auto result = m_input_latch;
        acknowledge_input();
        return result;
    }
    if (address >= kLocalRamBase && address < 0x3a00)
        return m_local_ram[address - kLocalRamBase];
    if (address >= kControlBase)
    {
        const auto index = address - kControlBase;
        if (index == 0x1c)
            return m_cpu.control[index];
        if (index == kProgramFlagData) {
            const auto value = static_cast<std::uint16_t>(
                (m_control[kProgramFlagControl] & m_control[kProgramFlagData]) |
                (m_progflags & ~m_control[kProgramFlagControl]));
            m_progflags ^= 0x0006;
            return value;
        }
        return m_control[index];
    }
    return 0xffff;
}

void DcsAudio2K::write_data(std::uint16_t address, std::uint16_t value) noexcept {
    address &= multiarcaderecomp::adsp2105::pc_mask;
    if (address < kRomBase) {
        const auto offset = address & 0x07ff;
        m_external_program[offset] = (m_external_program[offset] & 0x000000ffu) |
                                    (static_cast<std::uint32_t>(value) << 8);
        ++m_shared_ram_write_counts[offset];
        if (value != 0) ++m_shared_ram_nonzero_write_counts[offset];
        ++m_shared_ram_writes;
        ++m_shared_ram_data_writes;
        m_shared_ram_last_address = offset;
        m_shared_ram_last_value = value;
        m_shared_ram_last_pc = m_cpu.previous_pc;
        const auto distance = static_cast<std::uint16_t>(
            (offset - (m_dma_base & 0x07ff)) & 0x07ff);
        if (m_dma_active && distance < m_dma_length) {
            if (std::getenv("MULTIARCADERECOMP_ADSP_TRACE_SAMPLE_WRITES")) {
                static std::uint32_t trace_lines = 0;
                if (trace_lines < 128) {
                    ++trace_lines;
                    const auto& c = m_cpu.core;
                    std::fprintf(stderr,
                        "[DCS sample write] address=%04X value=%04X distance=%03X "
                        "PC=%04X OP=%06X I=%04X,%04X,%04X,%04X "
                        "M=%04X,%04X,%04X,%04X L=%04X,%04X,%04X,%04X "
                        "BASE=%04X,%04X,%04X,%04X AR=%04X AF=%04X "
                        "AX=%04X/%04X AY=%04X/%04X MR=%010llX\n",
                        address, value, distance, m_cpu.previous_pc,
                        read_program(m_cpu.previous_pc),
                        m_cpu.i[4], m_cpu.i[5], m_cpu.i[6], m_cpu.i[7],
                        static_cast<std::uint16_t>(m_cpu.m[4]),
                        static_cast<std::uint16_t>(m_cpu.m[5]),
                        static_cast<std::uint16_t>(m_cpu.m[6]),
                        static_cast<std::uint16_t>(m_cpu.m[7]),
                        m_cpu.l[4], m_cpu.l[5], m_cpu.l[6], m_cpu.l[7],
                        m_cpu.base[4], m_cpu.base[5], m_cpu.base[6], m_cpu.base[7],
                        static_cast<std::uint16_t>(c.ar),
                        static_cast<std::uint16_t>(c.af),
                        static_cast<std::uint16_t>(c.ax[0]),
                        static_cast<std::uint16_t>(c.ax[1]),
                        static_cast<std::uint16_t>(c.ay[0]),
                        static_cast<std::uint16_t>(c.ay[1]),
                        static_cast<unsigned long long>(c.mr));
                }
            }
            m_stream_last_write_address = offset;
            m_stream_last_write_value = value;
            m_stream_last_write_pc = m_cpu.previous_pc;
            m_stream_last_write_opcode = read_program(m_cpu.previous_pc);
            m_stream_last_write_i7 = m_cpu.i[7];
            m_stream_last_write_m7 = m_cpu.m[7];
            m_stream_last_write_l7 = m_cpu.l[7];
            m_stream_last_write_ax0 = m_cpu.core.ax[0];
            m_stream_last_write_ay0 = m_cpu.core.ay[0];
            m_stream_last_write_ar = m_cpu.core.ar;
        }
        return;
    }
    if (address >= kBankSelectBase && address < 0x3400) {
        const std::size_t banks = m_sound_region.size() / (0x1000 * 2);
        m_sound_bank = banks ? static_cast<std::uint16_t>((value & 0x07ff) % banks) : 0;
        ++m_sound_bank_writes;
        m_sound_bank_last_value = value;
        return;
    }
    if (address >= kLatchBase && address < 0x3800) {
        m_output_latch = value;
        m_output_full = true;
        ++m_output_writes;
        return;
    }
    if (address >= kLocalRamBase && address < 0x3a00) {
        const auto offset = static_cast<std::uint16_t>(address - kLocalRamBase);
        m_local_ram[offset] = value;
        ++m_local_ram_write_counts[offset];
        if (value != 0) ++m_local_ram_nonzero_write_counts[offset];
        ++m_local_ram_writes;
        m_local_ram_last_address = address;
        m_local_ram_last_value = value;
        m_local_ram_last_pc = m_cpu.previous_pc;
        return;
    }
    if (address >= kControlBase) {
        const auto index = static_cast<std::uint16_t>(address - kControlBase);
        const auto previous = m_control[index];
        m_control[index] = value;
        m_cpu.control[index] = value;
        if (index == 0x1b) {
            // MAME stores the timer scale as a cycle divisor (raw low byte + 1),
            // while the shared ADSP core stores the raw register and applies
            // +1 when advancing its timer.
            const auto scale = static_cast<std::uint16_t>((value & 0xff) + 1);
            if (previous != scale)
                m_cpu.timer_scale_counter = 0;
            m_control[index] = scale;
            m_cpu.control[index] = value & 0xff;
        } else if (index == 0x1c) {
            m_cpu.timer_scale_counter = 0;
        } else if (index == 0x1d && previous != value) {
            m_cpu.timer_scale_counter = 0;
        }
        if (index == kSystemControl) {
            ++m_syscontrol_writes;
            m_last_syscontrol_pc = m_cpu.previous_pc;
            m_last_syscontrol_value = value;
            if (value & 0x0200) {
                // MAME's SYSCONTROL reset pulses the ADSP and reloads its boot
                // page, then clears only SYSCONTROL. Board latches and RAM are
                // retained by this reset path.
                ++m_software_reboot_count;
                m_last_software_reboot_pc = m_cpu.previous_pc;
                m_last_software_reboot_value = value;
                m_dma_active = false;
                m_sport1_enabled = false;
                m_sport1_autobuffer = false;
                boot();
                m_control[kSystemControl] = 0;
            } else {
                m_sport1_enabled = (value & 0x0800) != 0;
                if (!m_sport1_enabled) m_dma_active = false;
            }
        } else if (index == kSport1Autobuffer) {
            m_sport1_autobuffer = (value & 0x0002) != 0;
        } else if (index == kProgramFlagControl) {
            // Control mask is retained in the register array.
        } else if (index == kSport1Control) {
            // DCS 2K's SPORT1 timing is serviced by the board audio scheduler.
        }
    }
}

void DcsAudio2K::boot() noexcept {
    if (m_sound_region.empty()) return;
    const std::size_t words = m_sound_region.size() / 2;
    const std::size_t banks = words / 0x1000;
    if (!banks) return;
    m_sound_bank %= static_cast<std::uint16_t>(banks);
    const std::size_t base_word = static_cast<std::size_t>(m_sound_bank) * 0x1000;
    std::uint8_t buffer[0x1000]{};
    for (std::size_t i = 0; i < sizeof(buffer); ++i) {
        const std::size_t byte = (base_word + i) * 2;
        if (byte < m_sound_region.size()) buffer[i] = m_sound_region[byte];
    }
    const std::size_t count = (static_cast<std::size_t>(buffer[3]) + 1) * 8;
    const std::size_t bounded_count = std::min(count, m_internal_program.size());
    std::fill(m_program_written.begin(), m_program_written.end(), false);
    for (std::size_t i = 0; i < bounded_count; ++i) {
        const std::size_t offset = i * 4;
        m_internal_program[i] = (static_cast<std::uint32_t>(buffer[offset]) << 16) |
                                (static_cast<std::uint32_t>(buffer[offset + 1]) << 8) |
                                buffer[offset + 2];
        m_boot_program_history.emplace_back(
            static_cast<std::uint16_t>(i), m_internal_program[i]);
    }
    const auto total_cycles = m_cpu.cycles;
    const auto native_instructions = m_cpu.native_instructions;
    const auto interpreted_instructions = m_cpu.interpreted_instructions;
    m_cpu = {};
    // A board reset restarts architectural state, but audio scheduling and
    // final native/interpreter counts remain monotonic for the whole run.
    m_cpu.cycles = total_cycles;
    m_cpu.native_instructions = native_instructions;
    m_cpu.interpreted_instructions = interpreted_instructions;
    m_cpu.loop = 0xffff;
    m_cpu.sstat = 0x55;
    m_cpu_stalled = false;
    ++m_boot_count;
}

void DcsAudio2K::acknowledge_input() noexcept {
    m_input_full = false;
    m_cpu.irq[2] = false;
    m_cpu.irq_latched[2] = false;
        m_cpu_stalled = false;
}
void DcsAudio2K::acknowledge_output() noexcept { m_output_full = false; }

} // namespace multiarcaderecomp::dcs
