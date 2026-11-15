#include "sound_board.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace mamerecomp {
namespace {
constexpr std::uint32_t kYmClock = 4'000'000;
constexpr std::uint32_t kYmClocksPerSample = 64;
constexpr std::size_t kFlushSamples = 256;
std::uint64_t main_to_z80(std::uint64_t c) { return c / 2; }
std::uint64_t z80_to_ym(std::uint64_t c) { return (c / 5) * 4 + (c % 5) * 4 / 5; }
}

System16BSound::System16BSound(std::span<const std::uint8_t> region, SegaAudioSink audio_sink, std::uint8_t volume,
    System16BBoard board, bool stereo_spread, std::uint8_t muted_ym_channels,
    bool suppress_slot0_channel2, bool suppress_slot3_channel5,
    bool suppress_slot4_channel6, bool suppress_slot5_channel7,
    std::function<bool(std::uint8_t)> sound_command_observer)
    : m_rom(region.begin(), region.end()),
      m_z80_bus([this](std::uint16_t address) { return memory_read(address); },
          [this](std::uint16_t address, std::uint8_t value) { memory_write(address, value); },
          [this](std::uint16_t port) { return port_read(static_cast<std::uint8_t>(port)); },
          [this](std::uint16_t port, std::uint8_t value) { port_write(static_cast<std::uint8_t>(port), value); }),
      m_cpu(m_z80_bus), m_ym2151(*this), m_master_volume(std::min<std::uint8_t>(volume, 100)),
      m_stereo_spread(stereo_spread), m_suppress_slot0_channel2(suppress_slot0_channel2),
      m_suppress_slot3_channel5(suppress_slot3_channel5),
      m_suppress_slot4_channel6(suppress_slot4_channel6),
      m_suppress_slot5_channel7(suppress_slot5_channel7),
      m_muted_ym_channels(muted_ym_channels),
      m_arranged_filtering_active(muted_ym_channels != 0 || suppress_slot0_channel2
          || suppress_slot3_channel5 || suppress_slot4_channel6 || suppress_slot5_channel7),
      m_rom_board(board), m_audio_sink(std::move(audio_sink)),
      m_sound_command_observer(std::move(sound_command_observer)) {
    if (m_rom.size() < 0x18000) throw std::invalid_argument("System 16B sound ROM region is too small for fixed and banked ROM");
    m_cpu.Reset();
    if (const char* trace_path = std::getenv("MAMERECOMP_YM2151_TRACE"); trace_path && *trace_path) {
        m_sound_trace.open(trace_path, std::ios::out | std::ios::trunc);
        if (!m_sound_trace) throw std::runtime_error(std::string("cannot open YM2151 trace file: ") + trace_path);
        m_sound_trace << "z80_cycle,z80_pc,event,register,value,channel,service,p1,p2,z80_ix,z80_sp,stack_return,voice_slot,voice_flags,voice_channel,voice_record\n";
    }
    if (const char* trace_path = std::getenv("MAMERECOMP_SOUND_COMMAND_TRACE"); trace_path && *trace_path) {
        m_sound_command_trace.open(trace_path, std::ios::out | std::ios::trunc);
        if (!m_sound_command_trace)
            throw std::runtime_error(std::string("cannot open sound command trace file: ") + trace_path);
        m_sound_command_trace << "maincpu_cycle,maincpu_pc,command,dispatch_index,z80_cycle,z80_pc\n";
    }
    m_ym2151.reset();
    m_upd7759.set_request_callback([this] { m_cpu.RaiseNmi(); ++m_upd_nmi_edges; });
    m_z80_bus.SetInstructionBoundary([this](std::uint64_t cycles) {
        synchronize_ym2151(z80_to_ym(cycles));
    });
    if (m_audio_sink) m_audio_buffer.reserve(kFlushSamples * 2);
}
System16BSound::~System16BSound() {
    try { flush_audio_samples(); } catch (...) { }
}
std::uint8_t System16BSound::memory_read(std::uint16_t a) {
    if (a < 0x8000) return m_rom[a];
    if (a < 0xe000) {
        const auto span = m_rom.size() - 0x10000;
        return m_rom[0x10000 + (std::size_t(m_bank) * 0x4000 + a - 0x8000) % span];
    }
    if (a == 0xe800) return m_sound_latch;
    if (a >= 0xf800) return m_ram[a - 0xf800];
    return 0xff;
}
void System16BSound::memory_write(std::uint16_t a, std::uint8_t v) { if (a >= 0xf800) m_ram[a - 0xf800] = v; }
std::uint8_t System16BSound::port_read(std::uint8_t p) {
    if ((p & 0xc0) == 0) { synchronize_ym2151(z80_to_ym(m_cpu.BusCallbackCycles())); return (p & 1) ? m_ym2151.read(1) : 0xff; }
    if ((p & 0xc0) == 0x80) return m_upd7759.idle() ? 0x80 : 0;
    if ((p & 0xc0) == 0xc0) return m_sound_latch;
    return 0xff;
}
void System16BSound::port_write(std::uint8_t p, std::uint8_t v) {
    const auto page = p & 0xc0;
    if (page == 0) {
        synchronize_ym2151(z80_to_ym(m_cpu.BusCallbackCycles()));
        if ((p & 1) == 0) {
            m_ym_address = v;
            m_ym2151.write(0, v);
        }
        else {
            auto chip_value = v;
            constexpr std::uint16_t voice_base = 0xf860;
            constexpr std::uint16_t voice_stride = 0x28;
            const bool in_voice_table = m_cpu.ix >= voice_base
                && m_cpu.ix < voice_base + voice_stride * 20;
            const auto voice_slot = in_voice_table
                ? static_cast<unsigned>((m_cpu.ix - voice_base) / voice_stride) : 0U;
            const bool is_slot0 = in_voice_table && voice_slot == 0;
            const bool is_slot3 = in_voice_table && voice_slot == 3;
            const bool is_slot4 = in_voice_table && voice_slot == 4;
            const bool is_slot5 = in_voice_table && voice_slot == 5;
            const bool is_slot12 = in_voice_table && voice_slot == 12;
            const bool is_slot13 = in_voice_table && voice_slot == 13;
            const int target_channel = m_ym_address == 0x08
                ? static_cast<int>(v & 7U)
                : (m_ym_address >= 0x20 ? static_cast<int>(m_ym_address & 7U) : -1);
            const bool suppress_voice_write =
                (m_suppress_slot0_channel2 && is_slot0 && target_channel == 1)
                || (m_suppress_slot3_channel5 && is_slot3 && target_channel == 4)
                || (m_suppress_slot4_channel6 && is_slot4 && target_channel == 5)
                || (m_suppress_slot5_channel7 && is_slot5 && target_channel == 6)
                || (m_suppress_chardecide_slots && is_slot12 && target_channel == 3)
                || (m_suppress_chardecide_slots && is_slot13 && target_channel == 4)
                // The captures identify the two voices as slots C/D on YM
                // channels 4/5. Filter their channel writes too, since IX can
                // point inside a slot while the Z80 routine walks its fields.
                || (m_suppress_chardecide_slots
                    && (target_channel == 3 || target_channel == 4));
            if (m_ym_address == 0x08 && (m_muted_ym_channels & (1U << (v & 7U))))
                chip_value = static_cast<std::uint8_t>(v & 0x07U);
            if (m_ym_address >= 0x20 && m_ym_address <= 0x27 && m_stereo_spread) {
                const auto channel = m_ym_address - 0x20;
                chip_value = static_cast<std::uint8_t>((v & 0x3fU) | ((channel & 1U) ? 0x80U : 0x40U));
                m_ym2151_pan_by_channel[channel] = chip_value & 0xc0U;
                ++m_ym2151_pan_overrides;
            } else if (m_ym_address >= 0x20 && m_ym_address <= 0x27) {
                m_ym2151_pan_by_channel[m_ym_address - 0x20] = v & 0xc0U;
            }
            if (m_sound_trace && m_sound_trace_rows < 250'000) {
                const int channel = m_ym_address == 0x08
                    ? static_cast<int>(v & 7U)
                    : (m_ym_address >= 0x20 ? static_cast<int>(m_ym_address & 7U) : -1);
                const auto stack_byte = [this](std::uint16_t address) -> std::uint8_t {
                    return address >= 0xf800 ? m_ram[address - 0xf800] : 0xff;
                };
                const auto stack_address = static_cast<std::uint16_t>(m_cpu.sp + 2U);
                const auto stack_return = static_cast<std::uint16_t>(stack_byte(stack_address)
                    | (static_cast<std::uint16_t>(stack_byte(static_cast<std::uint16_t>(stack_address + 1U))) << 8));
                constexpr std::uint16_t voice_count = 20;
                constexpr std::uint16_t voice_stride = 0x28;
                const auto ix = m_cpu.ix;
                const bool has_voice_slot = ix >= voice_base && ix < voice_base + voice_stride * voice_count
                    && ((ix - voice_base) % voice_stride) == 0;
                const auto slot = has_voice_slot ? static_cast<int>((ix - voice_base) / voice_stride) : -1;
                const auto slot_address = has_voice_slot ? static_cast<std::size_t>(ix - 0xf800) : 0U;
                m_sound_trace << m_cpu.cycles << ",0x" << std::hex << m_cpu.pc << ','
                              << (suppress_voice_write ? "SUPPRESSED" : "YM") << ",0x"
                              << static_cast<unsigned>(m_ym_address) << ",0x"
                              << static_cast<unsigned>(v) << ',';
                if (channel < 0) m_sound_trace << "--";
                else m_sound_trace << channel + 1;
                m_sound_trace << ",,,,0x" << ix << ",0x" << m_cpu.sp << ",0x" << stack_return << ',';
                if (slot < 0) m_sound_trace << "--,--,--,--\n";
                else {
                    m_sound_trace << slot << ",0x" << static_cast<unsigned>(m_ram[slot_address])
                                  << "," << static_cast<unsigned>(m_ram[slot_address + 1]) << ",";
                    if (m_ym_address == 0x08 && (v & 0x78U)) {
                        for (std::size_t byte = 0; byte < voice_stride; ++byte)
                            m_sound_trace << std::setw(2) << std::setfill('0')
                                          << static_cast<unsigned>(m_ram[slot_address + byte]);
                    } else m_sound_trace << "--";
                    m_sound_trace << '\n';
                }
                m_sound_trace << std::dec;
                ++m_sound_trace_rows;
            }
            if (suppress_voice_write) ++m_ym2151_suppressed_voice_writes;
            else {
                m_ym2151.write(1, chip_value); ++m_ym2151_writes;
                if (m_ym_address == 0x08 && (chip_value & 0x78)) ++m_ym2151_key_ons;
            }
        }
    } else if (page == 0x40) {
        // ROM select/address wiring varies between 171-5358, 171-5521 and 171-5797.
        std::uint32_t offs = 0;
        if (m_rom_board == System16BBoard::Sega5358) {
            if (!(v & 0x04)) offs = 0x00000;
            if (!(v & 0x08)) offs = 0x10000;
            if (!(v & 0x10)) offs = 0x20000;
            if (!(v & 0x20)) offs = 0x30000;
            offs += (v & 3) * 0x4000U;
        } else if (m_rom_board == System16BBoard::Sega5521) {
            offs = ((v & 8) >> 3) * 0x20000U + (v & 7) * 0x4000U;
        } else {
            offs = ((v & 8) >> 3) * 0x40000U + ((v & 0x10) >> 4) * 0x20000U + (v & 7) * 0x4000U;
        }
        offs %= static_cast<std::uint32_t>(m_rom.size() - 0x10000);
        m_bank = static_cast<std::uint8_t>((offs / 0x4000) & 0x1f);
        const bool old_md = m_md;
        m_md = (v & 0x80) == 0;
        const bool released = (v & 0x40) != 0;
        if (m_upd_reset_released && !released) m_upd7759.reset();
        if (m_upd7759.idle() && released && old_md && !m_md) m_upd7759.start();
        m_upd_reset_released = released;
    } else if (page == 0x80) {
        // Supply the shared uPD7759 FIFO port; its DRQ requests raise the Z80 NMI.
        m_upd7759_fifo = v;
        m_upd7759.write_port(v);
        ++m_upd7759_writes;
    }
}
void System16BSound::advance_to_main_cycles(std::uint64_t c) {
    const auto target = main_to_z80(c);
    const auto native_before = m_cpu.nativeInstructions;
    const auto interpreted_before = m_cpu.interpreted;
    const auto nmi_before = m_cpu.nmiServiced;
    m_cpu.Run(target, z80::Z80::Mode::Native);
    m_z80_instructions += (m_cpu.nativeInstructions - native_before) + (m_cpu.interpreted - interpreted_before);
    m_z80_nmi_serviced += m_cpu.nmiServiced - nmi_before;
    synchronize_ym2151(z80_to_ym(target));
}
void System16BSound::write_sound_latch(std::uint8_t v, std::uint64_t maincpu_cycle,
    std::uint32_t maincpu_pc) {
    m_sound_latch = v;
    const bool filter_chardecide_voices = m_sound_command_observer && m_sound_command_observer(v);
    const bool was_filtering_chardecide_voices = m_suppress_chardecide_slots;
    // Restrict the slot filter to the 0x9f command itself; 0x9e also drives
    // spell effects and must retain its native YM sound.
    m_suppress_chardecide_slots = filter_chardecide_voices;
    if (!was_filtering_chardecide_voices && m_suppress_chardecide_slots) {
        // The original voice may already be active by the time the arranged
        // replacement starts. Key off only the two YM channels used by the
        // captured character-decision slots (C/channel 4 and D/channel 5).
        synchronize_ym2151(z80_to_ym(m_cpu.cycles));
        m_ym2151.write(0, 0x08);
        m_ym2151.write(1, 0x03);
        m_ym2151.write(0, 0x08);
        m_ym2151.write(1, 0x04);
        std::cout << "Filtered native 0x9f voices: YM slots C/channel 4 and D/channel 5\n";
    }
    if (v == 0xc2 && m_arranged_filtering_active)
        m_restore_audio_after_deathadder_fade = true;
    if (v == 0x3f && m_restore_audio_after_deathadder_fade)
        m_waiting_for_deathadder_fade_steps = true;
    const bool deathadder_fade_complete = m_waiting_for_deathadder_fade_steps && v == 0x20;
    if (deathadder_fade_complete && m_arranged_filtering_active) {
        m_muted_ym_channels = 0;
        m_suppress_slot0_channel2 = false;
        m_suppress_slot3_channel5 = false;
        m_suppress_slot4_channel6 = false;
        m_suppress_slot5_channel7 = false;
        m_arranged_filtering_active = false;
        m_restore_audio_after_deathadder_fade = false;
        m_waiting_for_deathadder_fade_steps = false;
        std::cout << "Arranged YM filters restored to native audio at command 0x"
                  << std::hex << static_cast<unsigned>(v) << std::dec << '\n';
    }
    if (m_sound_command_trace) {
        m_sound_command_trace << std::dec << maincpu_cycle << ",0x" << std::hex << maincpu_pc
                              << ",0x" << static_cast<unsigned>(v)
                              << ",0x" << static_cast<unsigned>(v & 0x7fU)
                              << "," << std::dec << m_cpu.cycles << ",0x" << std::hex << m_cpu.pc
                              << '\n';
    }
    if (m_sound_trace && m_sound_trace_rows < 250'000) {
        m_sound_trace << m_cpu.cycles << ",0x" << std::hex << m_cpu.pc << ",LATCH,0x--,0x"
                      << static_cast<unsigned>(v) << ",--";
        for (unsigned field = 0; field < 10; ++field) m_sound_trace << ',';
        m_sound_trace << '\n' << std::dec;
        ++m_sound_trace_rows;
    }
    m_cpu.RaiseInterrupt();
}
void System16BSound::trace_inputs(std::uint8_t service, std::uint8_t p1, std::uint8_t p2) {
    if (!m_sound_trace) return;
    const std::array<std::uint8_t, 3> current{service, p1, p2};
    if (m_trace_inputs_valid && current == m_last_trace_inputs) return;
    m_last_trace_inputs = current;
    m_trace_inputs_valid = true;
    if (m_sound_trace_rows >= 250'000) return;
    m_sound_trace << m_cpu.cycles << ",0x" << std::hex << m_cpu.pc
                  << ",INPUT,--,--,--,0x" << static_cast<unsigned>(service)
                  << ",0x" << static_cast<unsigned>(p1)
                  << ",0x" << static_cast<unsigned>(p2) << ",,,,,,,\n" << std::dec;
    ++m_sound_trace_rows;
}
void System16BSound::synchronize_ym2151(std::uint64_t target) {
    while ((m_generated_ym_samples + 1) * kYmClocksPerSample <= target) {
        const auto clock = (++m_generated_ym_samples) * kYmClocksPerSample;
        m_ym_clock = clock;
        m_ym_timers.expire_due(clock, [this](std::uint32_t timer) {
            m_engine->engine_timer_expired(timer);
        });
        // uPD7759 runs at 640 kHz and updates its ADPCM output every four clocks.
        // One YM output sample represents 10.24 uPD clocks; retain the fraction.
        m_upd_clock_fraction += 256;
        while (m_upd_clock_fraction >= 25) { m_upd_clock_fraction -= 25; m_upd7759.advance(1); }
        ymfm::ym2151::output_data o{}; m_ym2151.generate(&o, 1); o.clamp16();
        // Golden Axe's System16B mix routes YM2151 outputs at 0.43.
        const int upd_pcm = m_upd7759.pcm_sample() * 48 / 100;
        const auto l = static_cast<std::int16_t>(std::clamp(o.data[0] * 43 / 100 + upd_pcm, -32768, 32767));
        const auto r = static_cast<std::int16_t>(std::clamp(o.data[1] * 43 / 100 + upd_pcm, -32768, 32767));
        m_audio_peak = std::max<std::uint16_t>(m_audio_peak, static_cast<std::uint16_t>(std::max(std::abs(int(l)), std::abs(int(r)))));
        m_stereo_difference_peak = std::max<std::uint16_t>(m_stereo_difference_peak,
            static_cast<std::uint16_t>(std::abs(int(l) - int(r))));
        if (m_audio_sink) { m_audio_buffer.push_back(std::int16_t(int(l) * m_master_volume / 100)); m_audio_buffer.push_back(std::int16_t(int(r) * m_master_volume / 100)); if (m_audio_buffer.size() >= kFlushSamples * 2) flush_audio_samples(); }
        ++m_audio_samples;
    }
    m_ym_clock = target;
}
void System16BSound::flush_audio_samples() {
    if (m_audio_sink && !m_audio_buffer.empty()) {
        m_audio_sink(m_audio_buffer, m_ym2151.sample_rate(kYmClock));
        m_audio_buffer.clear();
    }
}
void System16BSound::ymfm_set_timer(std::uint32_t t, std::int32_t d) { m_ym_timers.set_timer(t, d, m_ym_clock); }
void System16BSound::ymfm_set_busy_end(std::uint32_t c) { m_ym_timers.set_busy_end(c, m_ym_clock); }
bool System16BSound::ymfm_is_busy() { return m_ym_timers.is_busy(m_ym_clock); }
void System16BSound::ymfm_update_irq(bool v) { m_ym2151_irq=v; }
void System16BSound::ymfm_external_write(ymfm::access_class, std::uint32_t, std::uint8_t) { }
} // namespace mamerecomp
