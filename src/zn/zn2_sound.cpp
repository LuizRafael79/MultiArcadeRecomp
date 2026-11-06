#include "zn2_sound.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

namespace mamerecomp::zn {

Zn2SoundCpu::Zn2SoundCpu(CapcomZn2Board &board,
                         const mamerecomp::z80::Z80Code *aot_code)
    : board_(board), cpu_(*this), aot_code_(aot_code)
{
    cpu_.Reset();
    profile_timing_ = std::getenv("MAMERECOMP_ZN_PROFILE_RUNTIME") != nullptr;
    mamerecomp::z80::SetZ80Code(aot_code_);
    qsound_start(&qsound_, 60'000'000);
    samples_ = board_.qsound_samples();
    qsound_.rom_data = samples_.data();
    qsound_.rom_mask = samples_.size() - 1;
    qsound_.ready_flag = 0x80;
}

uint8_t Zn2SoundCpu::Read(uint16_t address)
{
    if (address == 0xd007) {
        sync_qsound(cpu_.BusCallbackCycles());
        return qsound_r(&qsound_);
    }
    return board_.sound_program_read(address);
}
void Zn2SoundCpu::Write(uint16_t address, uint8_t value)
{
    board_.sound_program_write(address, value);
    if (address >= 0xd000 && address <= 0xd002) {
        sync_qsound(cpu_.BusCallbackCycles());
        qsound_w(&qsound_, static_cast<uint8_t>(address - 0xd000), value);
    }
}
uint8_t Zn2SoundCpu::In(uint16_t port)
{
    const uint8_t value = board_.sound_port_read(static_cast<uint8_t>(port));
    if ((port & 0xff) == 0) board_.acknowledge_sound();
    return value;
}
void Zn2SoundCpu::Out(uint16_t port, uint8_t value)
{ board_.sound_port_write(static_cast<uint8_t>(port), value); }

void Zn2SoundCpu::sync_qsound(uint64_t sound_cycles)
{
    constexpr uint64_t sample_rate = 60'000'000 / 2 / 1248;
    const uint64_t target = sound_cycles * sample_rate / 8'000'000;
    while (generated_samples_ < target) {
        const int count = static_cast<int>(std::min<uint64_t>(target - generated_samples_, qsound_left_.size()));
        int16_t *outputs[]{qsound_left_.data(), qsound_right_.data()};
        qsound_stream_update(&qsound_, outputs, count);
        const size_t old_size = audio_queue_.size();
        audio_queue_.resize(old_size + size_t(count) * 2);
        for (int i = 0; i < count; ++i) {
            const int16_t left = qsound_left_[size_t(i)];
            const int16_t right = qsound_right_[size_t(i)];
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
    // MAME's coh3002c config clocks the CXD8661R at 100 MHz and the QSound
    // Z80 at 8 MHz. Both runtimes advance one scheduler tick per instruction.
    constexpr uint64_t psx_cycle_hz = 100'000'000;
    constexpr uint64_t sound_clock = 8'000'000;
    constexpr uint64_t irq_period = sound_clock / 250;
    const auto mode = mamerecomp::z80::HasZ80Code()
        ? mamerecomp::z80::Z80::Mode::Native
        : mamerecomp::z80::Z80::Mode::Interpreter;
    const uint64_t target = (psx_cycles * sound_clock) / psx_cycle_hz;
    if (board_.sound_pending()) {
        cpu_.RaiseNmi();
        board_.acknowledge_sound();
    }
    while (next_irq_ <= target) {
        if (next_irq_ > cpu_.cycles)
            cpu_.Run(next_irq_, mode);
        cpu_.SetInterruptLine(true);
        next_irq_ += irq_period;
    }
    if (target > cpu_.cycles)
        cpu_.Run(target, mode);
    sync_qsound(cpu_.cycles);
    if (profile_timing_)
        profile_runtime_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - profile_start).count());
}

uint64_t Zn2SoundCpu::next_main_event_cycle() const
{
    constexpr uint64_t psx_cycle_hz = 100'000'000;
    constexpr uint64_t sound_clock = 8'000'000;
    const __uint128_t numerator = __uint128_t(next_irq_) * psx_cycle_hz;
    return static_cast<uint64_t>((numerator + sound_clock - 1) / sound_clock);
}

} // namespace mamerecomp::zn
