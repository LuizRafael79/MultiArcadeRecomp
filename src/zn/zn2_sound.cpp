#include "zn2_sound.hpp"
#include "zn2_sound_timing.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace mamerecomp::zn {

Zn2SoundCpu::Zn2SoundCpu(CapcomZn2Board &board,
                         const mamerecomp::z80::Z80Code *aot_code,
                         mamerecomp::chips::QSoundMode qsound_mode,
                         std::span<const std::uint16_t> qsound_firmware,
                         std::span<const mamerecomp::dsp16::Dsp16ACompiledEntry> dsp_aot_program,
                         mamerecomp::dsp16::Dsp16ARunHandler dsp_aot_run)
    : board_(board), cpu_(*this),
      aot_code_(std::getenv("MAMERECOMP_ZN2_Z80_INTERPRETER_ONLY") ? nullptr : aot_code),
      qsound_(mamerecomp::chips::QSoundConfig{60'000'000, qsound_mode, qsound_firmware})
{
    cpu_.Reset();
    profile_timing_ = std::getenv("MAMERECOMP_ZN_PROFILE_RUNTIME") != nullptr;
    mamerecomp::z80::SetZ80Code(aot_code_);
    samples_ = board_.qsound_samples();
    qsound_.set_sample_rom(samples_);
    qsound_.reset();
    qsound_.set_dsp_aot_program(dsp_aot_program, dsp_aot_run);
}

uint8_t Zn2SoundCpu::Read(uint16_t address)
{
    if (address == 0xd007) {
        // MAME's qsound_r() is a passive latch read. The Z80 runs before the
        // QSound/DSP in each scheduler slice, so do not catch the DSP up from
        // inside this callback; run_to_main_cycles() advances it after the Z80
        // reaches the slice boundary.
        const std::uint8_t value = qsound_.read_status();
        if (std::getenv("MAMERECOMP_ZN_TRACE_QSOUND_STATUS")) {
            static std::uint64_t traced = 0;
            static const std::uint64_t after_cycles = [] {
                const char *text = std::getenv(
                    "MAMERECOMP_ZN_TRACE_QSOUND_STATUS_AFTER_CYCLES");
                return text ? std::strtoull(text, nullptr, 0) : 0ull;
            }();
            const std::uint64_t cycle = cpu_.BusCallbackCycles();
            if (cycle >= after_cycles && traced++ < 65'536)
                std::cerr << "ZN2 QSOUND_STATUS cycle=" << cycle
                          << " pc=0x" << std::hex << cpu_.pc
                          << " value=0x" << unsigned(value)
                          << " dsp_cycle=" << std::dec << qsound_.dsp_cycles()
                          << " dsp_pc=0x" << std::hex << qsound_.dsp_pc()
                          << " next_irq=" << std::dec << next_irq_ << '\n';
        }
        return value;
    }
    return board_.sound_program_read(address);
}
void Zn2SoundCpu::Write(uint16_t address, uint8_t value)
{
    board_.sound_program_write(address, value);
    if (address >= 0xd000 && address <= 0xd002) {
        if (address == 0xd002) {
            // MAME lowers DSP-ready in qsound_w(), then applies set_cmd via
            // scheduler().synchronize() after this Z80 instruction retires.
            // Lower ready in the write callback before any device catch-up,
            // while deferring payload/IRQ delivery to InstructionBoundary.
            qsound_.begin_command_write();
            pending_qsound_command_ = value;
            qsound_command_pending_ = true;
        } else {
            // MAME's D000/D001 handlers only update m_new_data. They do not
            // synchronize the stream or advance the DSP.
            qsound_.write(static_cast<uint8_t>(address - 0xd000), value);
        }
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 4096)
                std::cerr << "ZN2 QSound write offset=0x" << std::hex
                          << unsigned(address - 0xd000) << " value=0x" << unsigned(value)
                          << " z80_pc=0x" << cpu_.pc << " cycle=" << std::dec
                          << cpu_.cycles << '\n';
        }
    }
}
void Zn2SoundCpu::InstructionBoundary(uint64_t cycles)
{
    if (!qsound_command_pending_)
        return;

    // qsound_w() lowers ready synchronously, but MAME defers set_cmd() and the
    // DSP IRQ until the scheduler synchronization boundary. Advance the DSP
    // to this Z80 instruction's retirement time before making that IRQ visible;
    // otherwise it is asserted at the previous DSP timestamp retroactively.
    sync_qsound(cycles);
    qsound_.write(2, pending_qsound_command_);
    qsound_.run_clocks(0);
    qsound_command_pending_ = false;
}
uint8_t Zn2SoundCpu::In(uint16_t port)
{
    const uint8_t value = board_.sound_port_read(static_cast<uint8_t>(port));
    if ((port & 0xff) == 0) {
        if (std::getenv("MAMERECOMP_ZN_TRACE_SOUND_COMMANDS")) {
            static unsigned traced = 0;
            if (traced++ < 512)
                std::cerr << "ZN2 sound latch read value=0x" << std::hex
                          << unsigned(value) << " z80_pc=0x" << cpu_.pc
                          << " cycle=" << std::dec << cpu_.cycles << '\n';
        }
        board_.acknowledge_sound();
        sound_nmi_delivered_ = false;
    }
    return value;
}
void Zn2SoundCpu::Out(uint16_t port, uint8_t value)
{ board_.sound_port_write(static_cast<uint8_t>(port), value); }

void Zn2SoundCpu::sync_qsound(uint64_t sound_cycles)
{
    // Keep the fractional QSound sample rate (60 MHz / 2 / 1248) when
    // mapping Z80 time to output frames. Flooring it to 24,038 Hz here makes
    // the host frame counter drift from the DSP's serial sample boundaries.
    const uint64_t target = qsound_target_frames(sound_cycles);
    const uint64_t target_dsp_clocks = sound_cycles * 60'000'000ull / 8'000'000ull;
    if (target_dsp_clocks > generated_dsp_clocks_) {
        const auto start = profile_timing_
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        qsound_.run_clocks(target_dsp_clocks - generated_dsp_clocks_);
        if (profile_timing_)
            profile_dsp_ns_ += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count());
        generated_dsp_clocks_ = target_dsp_clocks;
    }
    while (generated_samples_ < target) {
        const int count = static_cast<int>(std::min<uint64_t>(target - generated_samples_, qsound_stereo_.size() / 2));
        auto output = std::span<int16_t>(qsound_stereo_.data(), size_t(count) * 2);
        const auto render_start = profile_timing_
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        qsound_.render(output);
        if (profile_timing_)
            profile_qsound_render_ns_ += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - render_start).count());
        const size_t old_size = audio_queue_.size();
        audio_queue_.resize(old_size + size_t(count) * 2);
        for (int i = 0; i < count; ++i) {
            const int16_t left = qsound_stereo_[size_t(i) * 2];
            const int16_t right = qsound_stereo_[size_t(i) * 2 + 1];
            audio_queue_[old_size + size_t(i) * 2] = left;
            audio_queue_[old_size + size_t(i) * 2 + 1] = right;
            audio_peak_ = std::max(audio_peak_, static_cast<uint32_t>(std::max(std::abs(int(left)), std::abs(int(right)))));
        }
        generated_samples_ += count;
    }
}

std::vector<int16_t> Zn2SoundCpu::take_audio()
{
    std::vector<int16_t> samples;
    samples.swap(audio_queue_);
    return samples;
}

void Zn2SoundCpu::run_to_main_cycles(uint64_t psx_cycles)
{
    const auto profile_start = profile_timing_
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    // MAME clocks the CXD8661R at 100 MHz, but its PSX core converts four
    // crystal clocks into one execute cycle. cpu_.cycles shares that execute
    // timebase, so pace the 8 MHz Z80 against 25 MHz here.
    constexpr uint64_t psx_cycle_hz = 25'000'000;
    constexpr uint64_t sound_clock = 8'000'000;
    constexpr uint64_t irq_period = sound_clock / 250;
    const auto mode = mamerecomp::z80::HasZ80Code()
        ? mamerecomp::z80::Z80::Mode::Native
        : mamerecomp::z80::Z80::Mode::Interpreter;
    const uint64_t target = (psx_cycles * sound_clock) / psx_cycle_hz;
    const auto run_z80_to = [&](uint64_t cycle) {
        if (cycle <= cpu_.cycles) return;
        const auto start = profile_timing_
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        cpu_.Run(cycle, mode);
        if (profile_timing_)
            profile_z80_ns_ += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count());
    };
    if (board_.sound_pending() && !sound_nmi_delivered_) {
        cpu_.RaiseNmi();
        sound_nmi_delivered_ = true;
    }
    while (next_irq_ <= target) {
        run_z80_to(next_irq_);
        // MAME completes the current execute-list slice (Z80, then QSound's
        // DSP) before dispatching the 250 Hz timer. Catch the DSP up to this
        // boundary before asserting the timer IRQ on the Z80.
        sync_qsound(next_irq_);
        cpu_.SetInterruptLine(true);
        next_irq_ += irq_period;
    }
    run_z80_to(target);
    sync_qsound(cpu_.cycles);
    if (profile_timing_)
        profile_runtime_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - profile_start).count());
}

uint64_t Zn2SoundCpu::next_main_event_cycle() const
{
    constexpr uint64_t main_clock = 25'000'000;
    constexpr uint64_t z80_clock = 8'000'000;
    constexpr uint64_t dsp_clock = 60'000'000;
    constexpr uint64_t dsp_clocks_per_sample = 2 * 1248;
    const auto ceil_div = [](__uint128_t numerator, uint64_t denominator) {
        return static_cast<uint64_t>((numerator + denominator - 1) / denominator);
    };

    // Keep the shared Z80/DSP scheduler observable at the DSP serial-frame
    // boundary as well as at the board's periodic IRQ. The status read itself
    // remains passive, matching qsound_r().
    const uint64_t next_sample_dsp_clock =
        (generated_dsp_clocks_ / dsp_clocks_per_sample + 1) *
        dsp_clocks_per_sample;
    const uint64_t next_sample_z80_cycle = ceil_div(
        __uint128_t(next_sample_dsp_clock) * z80_clock, dsp_clock);
    const uint64_t next_sample_main_cycle = ceil_div(
        __uint128_t(next_sample_z80_cycle) * main_clock, z80_clock);
    const uint64_t next_irq_main_cycle = ceil_div(
        __uint128_t(next_irq_) * main_clock, z80_clock);
    return std::min(next_sample_main_cycle, next_irq_main_cycle);
}

} // namespace mamerecomp::zn
