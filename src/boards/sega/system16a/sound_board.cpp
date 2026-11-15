#include "sound_board.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace mamerecomp {
namespace {

constexpr std::uint32_t kSoundClockHz = 4'000'000;
constexpr std::uint32_t kMainClockHz = 10'000'000;
constexpr std::uint32_t kUpd7751ClockHz = 6'000'000;
constexpr std::uint32_t kSoundCyclesPerUpdMachineCycle =
    kSoundClockHz * Upd7751::clocks_per_machine_cycle / kUpd7751ClockHz;
static_assert(kSoundClockHz * Upd7751::clocks_per_machine_cycle % kUpd7751ClockHz == 0);
constexpr std::uint32_t kYm2151ClocksPerSample = 64;
constexpr std::size_t kAudioFlushSamples = 256;

std::uint64_t main_cycles_to_sound_cycles(std::uint64_t cycles) {
    // 4 MHz sound clock against a 10 MHz 68000 clock, without overflowing.
    return (cycles / 5) * 2 + ((cycles % 5) * 2) / 5;
}

} // namespace

System16ASound::System16ASound(std::span<const std::uint8_t> sound_rom,
    std::span<const std::uint8_t> upd7751_rom, std::span<const std::uint8_t> upd7751_data,
    SegaAudioSink audio_sink, bool stereo_spread, std::uint8_t master_volume)
    : m_rom(sound_rom.begin(), sound_rom.end()),
      m_upd7751_rom(upd7751_rom.begin(), upd7751_rom.end()),
      m_upd7751_data(upd7751_data.begin(), upd7751_data.end()),
      m_z80_bus([this](std::uint16_t address) { return memory_read(address); },
          [this](std::uint16_t address, std::uint8_t value) { memory_write(address, value); },
          [this](std::uint16_t port) { return port_read(static_cast<std::uint8_t>(port)); },
          [this](std::uint16_t port, std::uint8_t value) { port_write(static_cast<std::uint8_t>(port), value); }),
      m_cpu(m_z80_bus), m_ym2151(*this), m_stereo_spread(stereo_spread),
      m_master_volume(std::min<std::uint8_t>(master_volume, 100)),
      m_audio_sink(std::move(audio_sink)) {
    m_upd7751_expander.set_port_write_callback([this](unsigned port, std::uint8_t value) {
        update_upd7751_sample_address(port, value);
    });
    if (m_rom.size() != 0x8000) {
        throw std::invalid_argument("System 16A sound CPU ROM must be exactly 0x8000 bytes");
    }
    if (m_upd7751_rom.size() != 0x400 || m_upd7751_data.empty()) {
        throw std::invalid_argument("System 16A uPD7751 ROM regions have invalid sizes");
    }

    m_cpu.Reset();
    m_ym2151.reset();
    m_z80_bus.SetInstructionBoundary([this](std::uint64_t cycles) {
        advance_upd7751_to_sound_cycles(cycles);
        synchronize_ym2151(cycles);
    });

    Upd7751::Bus upd_bus;
    upd_bus.read_bus = [this]() { return upd7751_bus_read(); };
    upd_bus.write_bus = [](std::uint8_t) {};
    upd_bus.read_port = [this](unsigned port) { return upd7751_port_read(port); };
    upd_bus.write_port = [this](unsigned port, std::uint8_t value) { upd7751_port_write(port, value); };
    upd_bus.prog_line = [this](bool state) { upd7751_prog_write(state); };
    m_upd7751 = std::make_unique<Upd7751>(m_upd7751_rom, std::move(upd_bus));
    // The board holds /RESET active until the YM2151's CT1 line is released.
    m_upd7751->set_reset(true);

    if (m_audio_sink) m_audio_buffer.reserve(kAudioFlushSamples * 2);
}

System16ASound::~System16ASound() {
    try { flush_audio_samples(); } catch (...) { }
}

std::uint8_t System16ASound::memory_read(std::uint16_t address) {
    if (address < 0x8000) return m_rom[address];
    if (address == 0xe800) return read_sound_latch();
    if (address >= 0xf800) return m_ram[address - 0xf800];
    return 0xff;
}

void System16ASound::memory_write(std::uint16_t address, std::uint8_t value) {
    if (address >= 0xf800) m_ram[address - 0xf800] = value;
}

std::uint8_t System16ASound::port_read(std::uint8_t port) {
    const std::uint8_t page = port & 0xc0U;
    if (page == 0x00U) {
        synchronize_ym2151(m_cpu.BusCallbackCycles());
        return (port & 1U) ? m_ym2151.read(1) : 0xff;
    }
    if (page == 0xc0U) return read_sound_latch();
    return 0xff;
}

void System16ASound::port_write(std::uint8_t port, std::uint8_t value) {
    const std::uint8_t page = port & 0xc0U;
    if (page == 0x00U) {
        synchronize_ym2151(m_cpu.BusCallbackCycles());
        std::uint8_t chip_value = value;
        if ((port & 1U) == 0) {
            m_ym2151_address = value;
        } else {
            m_ym2151_last_value = value;
            if (m_ym2151_address >= 0x20 && m_ym2151_address <= 0x27) {
                m_ym2151_pan_by_channel[m_ym2151_address - 0x20] = value & 0xc0U;
                if (m_stereo_spread) {
                    // Optional presentation enhancement: hard-pan alternating
                    // YM2151 channels while preserving every non-pan bit.
                    const auto channel = m_ym2151_address - 0x20;
                    chip_value = static_cast<std::uint8_t>((value & 0x3fU) | ((channel & 1U) ? 0x80U : 0x40U));
                }
            }
            if (m_ym2151_address == 0x08 && (value & 0x78U) != 0) {
                ++m_ym2151_key_ons;
                ++m_ym2151_key_ons_by_channel[value & 0x07U];
            }
        }
        // CT output may reset the MCU and write its P1 port during this call.
        // The YM timeline was synchronized above; avoid re-entering generate().
        m_ym_write_in_progress = true;
        m_ym2151.write(port & 1U, chip_value);
        m_ym_write_in_progress = false;
        ++m_ym2151_writes;
    } else if (page == 0x80U) {
        m_upd7751_command = value;
        m_upd7751_command_index = value >> 5;
        const auto rom_count = m_upd7751_data.size() / 0x8000U;
        m_upd7751_rom_address &= 0x3fffU;
        m_upd7751_rom_address |= std::uint32_t(value & 1U) << 14;
        if (!(value & 0x02U) && rom_count >= 1) m_upd7751_rom_address |= 0x00000U;
        if (!(value & 0x04U) && rom_count >= 2) m_upd7751_rom_address |= 0x08000U;
        if (!(value & 0x08U) && rom_count >= 3) m_upd7751_rom_address |= 0x10000U;
        if (!(value & 0x10U) && rom_count >= 4) m_upd7751_rom_address |= 0x18000U;
        ++m_upd7751_commands;
    }
}

void System16ASound::advance_to_main_cycles(std::uint64_t main_cycles) {
    const auto target = main_cycles_to_sound_cycles(main_cycles);
    const auto native_before = m_cpu.nativeInstructions;
    const auto interpreted_before = m_cpu.interpreted;
    m_cpu.Run(target, z80::Z80::Mode::Native);
    m_z80_instructions += (m_cpu.nativeInstructions - native_before) + (m_cpu.interpreted - interpreted_before);
    advance_upd7751_to_sound_cycles(m_cpu.cycles);
    advance_upd7751_to_sound_cycles(target);
    synchronize_ym2151(target);
}

void System16ASound::write_sound_latch(std::uint8_t value) {
    m_sound_latch = value;
}

std::uint8_t System16ASound::upd7751_bus_read() const {
    return m_upd7751_rom_address < m_upd7751_data.size()
        ? m_upd7751_data[m_upd7751_rom_address] : 0xff;
}

std::uint8_t System16ASound::upd7751_port_read(unsigned port) const {
    if (port == 1) return m_upd7751_dac.value();
    if (port == 2) {
        return static_cast<std::uint8_t>(0x80U | ((m_upd7751_command_index & 7U) << 4)
            | (m_upd7751_expander.read_p2() & 0x0fU));
    }
    return 0xff;
}

void System16ASound::upd7751_port_write(unsigned port, std::uint8_t value) {
    if (port == 1) {
        // MAME's DAC updates its sound stream before changing the held value.
        // Flush to the MCU write timestamp first so this value only affects
        // subsequent samples, rather than retroactively changing the interval.
        if (m_upd7751 && !m_ym_write_in_progress)
            synchronize_ym2151(m_upd7751->machine_cycles() * kSoundCyclesPerUpdMachineCycle);
        m_upd7751_dac.write(value);
        ++m_upd7751_p1_writes;
    } else if (port == 2) {
        m_upd7751_expander.write_p2(value);
    }
}

void System16ASound::upd7751_prog_write(bool state) {
    m_upd7751_expander.set_prog(state);
}

void System16ASound::update_upd7751_sample_address(unsigned port, std::uint8_t value) {
    // Board wiring: D8243 P4-P7 drive the low 14 address bits of the sample ROM.
    const unsigned shift = port * 4U;
    const std::uint32_t mask = (0x0fU << shift) & 0x3fffU;
    m_upd7751_rom_address = (m_upd7751_rom_address & ~mask)
        | ((std::uint32_t(value) << shift) & mask);
}

void System16ASound::advance_upd7751_to_sound_cycles(std::uint64_t z80_cycles) {
    if (!m_upd7751) return;
    const auto target_machine_cycles = z80_cycles / kSoundCyclesPerUpdMachineCycle;
    while (m_upd7751->machine_cycles() < target_machine_cycles) {
        m_upd7751->step();
        ++m_upd7751_instructions;
    }
    // P1 writes flush the YM stream at their own timestamp. The caller flushes
    // the remaining interval after the MCU catches up, avoiding a YM sync per
    // interpreted instruction (hundreds of thousands per emulated second).
}

void System16ASound::set_nmi_line(bool asserted) {
    if (asserted && !m_nmi_line_asserted) {
        m_cpu.RaiseNmi();
        ++m_nmi_edges;
    }
    m_nmi_line_asserted = asserted;
}

std::uint8_t System16ASound::read_sound_latch() {
    ++m_sound_latch_reads;
    if (m_latch_read_callback) m_latch_read_callback();
    return m_sound_latch;
}

void System16ASound::synchronize_ym2151(std::uint64_t z80_cycles) {
    const std::uint64_t target = z80_cycles;
    while ((m_generated_ym_samples + 1) * kYm2151ClocksPerSample <= target) {
        const auto sample_clock = (m_generated_ym_samples + 1) * kYm2151ClocksPerSample;
        m_ym_clock = sample_clock;
        m_ym_timers.expire_due(sample_clock, [this](std::uint32_t timer) {
            m_engine->engine_timer_expired(timer);
        });

        ymfm::ym2151::output_data output{};
        m_ym2151.generate(&output, 1);
        // YM2151 exposes two independently panned outputs. Preserve native
        // L/R for the host stereo output; MAME's board config routes both to a
        // mono center speaker, but that is a board-level output choice.
        output.clamp16();
        // MAME routes YM2151 at 0.43 and the mono 8-bit DAC at 0.4. The SDL
        // output uses 0.20 for now and a gentle AC-coupling high-pass: P1 is
        // held at 0xff during reset/idle, while the real board's analog path
        // blocks its DC component. Keep this DAC source mono and centered.
        const auto dac_input = m_upd7751_dac.signed_sample() * 20 / 100;
        const auto dac = m_upd7751_dac_filter.process(dac_input);
        const auto left = static_cast<std::int16_t>(std::clamp((output.data[0] * 43) / 100 + dac, -32768, 32767));
        const auto right = static_cast<std::int16_t>(std::clamp((output.data[1] * 43) / 100 + dac, -32768, 32767));
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak,
            static_cast<std::uint16_t>(std::max(std::abs(static_cast<int>(left)), std::abs(static_cast<int>(right)))));
        m_stereo_difference_peak = std::max<std::uint16_t>(m_stereo_difference_peak,
            static_cast<std::uint16_t>(std::abs(static_cast<int>(left) - static_cast<int>(right))));
        if (m_audio_sink) {
            m_audio_buffer.push_back(static_cast<std::int16_t>((static_cast<std::int32_t>(left) * m_master_volume) / 100));
            m_audio_buffer.push_back(static_cast<std::int16_t>((static_cast<std::int32_t>(right) * m_master_volume) / 100));
            if (m_audio_buffer.size() >= kAudioFlushSamples * 2) flush_audio_samples();
        }
        ++m_generated_ym_samples;
        ++m_audio_samples;
    }
    m_ym_clock = target;
}

void System16ASound::flush_audio_samples() {
    if (m_audio_sink && !m_audio_buffer.empty()) {
        m_audio_sink(m_audio_buffer, m_ym2151.sample_rate(kSoundClockHz));
        m_audio_buffer.clear();
    }
}

void System16ASound::ymfm_set_timer(std::uint32_t timer, std::int32_t duration_in_clocks) {
    m_ym_timers.set_timer(timer, duration_in_clocks, m_ym_clock);
}

void System16ASound::ymfm_set_busy_end(std::uint32_t clocks) {
    m_ym_timers.set_busy_end(clocks, m_ym_clock);
}

bool System16ASound::ymfm_is_busy() {
    return m_ym_timers.is_busy(m_ym_clock);
}

void System16ASound::ymfm_update_irq(bool asserted) {
    m_ym2151_irq = asserted;
}

void System16ASound::ymfm_external_write(ymfm::access_class type, std::uint32_t address, std::uint8_t data) {
    if (type == ymfm::ACCESS_IO && address == 0) {
        m_upd7751_control = data;
        if (m_upd7751) {
            m_upd7751->set_reset((data & 0x01U) == 0);
            m_upd7751->set_irq((data & 0x02U) == 0);
        }
    }
}

} // namespace mamerecomp
