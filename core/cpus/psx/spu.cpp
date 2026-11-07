#include "spu.h"
#include "spu_adsr_rates.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

namespace mamerecomp::psx {
namespace {
constexpr unsigned kIrqAddress = 0xd2;
constexpr unsigned kTransferAddress = 0xd3;
constexpr unsigned kData = 0xd4;
constexpr unsigned kControl = 0xd5;
constexpr unsigned kStatus = 0xd7;
constexpr unsigned kKeyOnLow = 0xc4;
constexpr unsigned kKeyOnHigh = 0xc5;
constexpr unsigned kKeyOffLow = 0xc6;
constexpr unsigned kKeyOffHigh = 0xc7;
constexpr unsigned kVoiceOnLow = 0xce;
constexpr unsigned kVoiceOnHigh = 0xcf;
constexpr uint16_t kIrqEnable = 1u << 6;
constexpr uint16_t kIrqStatus = 1u << 6;
constexpr uint16_t kDmaRequest = 1u << 7;
constexpr uint16_t kDmaWrite = 1u << 8;
constexpr uint16_t kDmaRead = 1u << 9;
constexpr uint16_t kDmaModeMask = 3u << 4;
constexpr uint64_t kSpuSampleRate = 44100;
constexpr int kAdpcmFilter[5][2] = {{0, 0}, {60, 0}, {115, -52},
                                    {98, -55}, {122, -60}};
int16_t clamp_sample(int32_t sample)
{
    return static_cast<int16_t>(std::clamp(sample, -32768, 32767));
}
template <size_t N>
float envelope_rate(const std::array<double, N> &table, unsigned index)
{
    if (index >= N || table[index] <= 0.0) return 0.0f;
    return static_cast<float>(1.0 / (table[index] * kSpuSampleRate));
}
}

Spu::Spu(Irq irq) : irq_(std::move(irq))
{
    // MAME's SPU reset defaults: transfer control is idle, and all voice
    // masks/status begin clear. RAM is zero-filled by the ZN machine config.
}

uint16_t Spu::read_register(unsigned index) const
{
    index &= 0xff;
    if (index == kStatus) {
        uint16_t status = registers_[kStatus];
        status &= static_cast<uint16_t>(~(kDmaRequest | kDmaWrite | kDmaRead));
        const uint16_t mode = registers_[kControl] & kDmaModeMask;
        if (mode == (1u << 4)) status |= kDmaRequest | kDmaWrite;
        else if (mode == (2u << 4)) status |= kDmaRequest | kDmaWrite;
        else if (mode == (3u << 4)) status |= kDmaRequest | kDmaRead;
        return status;
    }
    return registers_[index];
}

void Spu::update_irq(bool active)
{
    if (irq_active_ == active) return;
    irq_active_ = active;
    if (std::getenv("MAMERECOMP_ZN_TRACE_SPU"))
        std::cerr << "ZN SPU IRQ9 " << (active ? "assert" : "clear")
                  << " status=0x" << std::hex << registers_[kStatus]
                  << " irq_addr=0x" << registers_[kIrqAddress]
                  << " control=0x" << registers_[kControl] << std::dec << '\n';
    if (irq_) irq_(active);
}

uint16_t Spu::read_ram_word()
{
    const uint32_t address = transfer_byte_address_ & 0x7fffeu;
    const uint16_t result = uint16_t(ram_[address]) | (uint16_t(ram_[address + 1]) << 8);
    transfer_byte_address_ = (address + 2) & 0x7ffffu;
    registers_[kTransferAddress] = static_cast<uint16_t>(transfer_byte_address_ >> 3);
    return result;
}

void Spu::write_ram_word(uint16_t value)
{
    const uint32_t address = transfer_byte_address_ & 0x7fffeu;
    ram_[address] = uint8_t(value);
    ram_[address + 1] = uint8_t(value >> 8);
    transfer_byte_address_ = (address + 2) & 0x7ffffu;
    registers_[kTransferAddress] = static_cast<uint16_t>(transfer_byte_address_ >> 3);
}

uint32_t Spu::read(uint32_t byte_offset, unsigned size)
{
    if (size != 1 && size != 2 && size != 4) return 0xffffffffu;
    uint32_t result = 0;
    for (unsigned consumed = 0; consumed < size;) {
        const uint32_t at = (byte_offset + consumed) & 0x3ff;
        const unsigned reg = (at >> 1) & 0xff;
        const uint16_t word = reg == kData ? read_ram_word() : read_register(reg);
        const unsigned lane = at & 1;
        const unsigned count = std::min(2u - lane, size - consumed);
        for (unsigned i = 0; i < count; ++i)
            result |= uint32_t((word >> ((lane + i) * 8)) & 0xff) << ((consumed + i) * 8);
        consumed += count;
    }
    return result;
}

void Spu::write_register(unsigned index, uint16_t value)
{
    index &= 0xff;
    if (index == kStatus) return; // MAME treats SPU status as read-only.
    registers_[index] = value;
    if (index == kTransferAddress) {
        transfer_byte_address_ = (uint32_t(value) << 3) & 0x7ffffu;
    } else if (index == kData) {
        write_ram_word(value);
    } else if (index == kControl) {
        registers_[kStatus] = static_cast<uint16_t>((registers_[kStatus] & ~0x3fu) | (value & 0x3fu));
        if (!(value & kIrqEnable)) {
            registers_[kStatus] &= static_cast<uint16_t>(~kIrqStatus);
            update_irq(false);
        }
    } else if (index == kKeyOnLow || index == kKeyOnHigh) {
        const unsigned base = index == kKeyOnLow ? 0 : 16;
        for (unsigned bit = 0; bit < 16 && base + bit < 24; ++bit) {
            if (!(value & (1u << bit))) continue;
            const unsigned voice = base + bit;
            const unsigned voice_base = voice * 8;
            voice_address_[voice] = (uint32_t(registers_[voice_base + 3]) << 3) & 0x7ffffu;
            voice_fraction_[voice] = 0;
            voice_sample_in_block_[voice] = 0;
            voice_block_address_[voice] = voice_address_[voice] & 0x7fff0u;
            voice_history_1_[voice] = voice_history_2_[voice] = 0;
            voice_envelope_[voice] = 0.0f;
            voice_envelope_stage_[voice] = 0;
            const uint16_t adsr1 = registers_[voice_base + 4];
            const unsigned attack = (adsr1 >> 8) & 0x7f;
            voice_envelope_delta_[voice] = (adsr1 & 0x8000u)
                ? envelope_rate(spu_rates::pos_exp_rate_seconds, attack)
                : envelope_rate(spu_rates::linear_rate_seconds, attack);
            decode_voice_block(voice);
            registers_[voice < 16 ? kVoiceOnLow : kVoiceOnHigh] |=
                static_cast<uint16_t>(1u << (voice < 16 ? voice : voice - 16));
        }
    } else if (index == kKeyOffLow || index == kKeyOffHigh) {
        const unsigned first_voice = index == kKeyOffLow ? 0 : 16;
        for (unsigned bit = 0; bit < 16 && first_voice + bit < 24; ++bit) {
            if (!(value & (1u << bit))) continue;
            const unsigned voice = first_voice + bit;
            const uint16_t adsr2 = registers_[voice * 8 + 5];
            const unsigned release = adsr2 & 0x1f;
            voice_envelope_stage_[voice] = 3;
            voice_envelope_delta_[voice] = -((adsr2 & 0x20u)
                ? envelope_rate(spu_rates::exp_release_rate_seconds, release)
                : envelope_rate(spu_rates::linear_release_rate_seconds, release));
        }
    }
}

void Spu::write(uint32_t byte_offset, unsigned size, uint32_t value)
{
    if (size != 1 && size != 2 && size != 4) return;
    // PSX SPU is a 16-bit register bus. CPU byte/word writes preserve the
    // other lane of the selected register, as MAME's mem_mask handler does.
    for (unsigned consumed = 0; consumed < size;) {
        const uint32_t at = (byte_offset + consumed) & 0x3ff;
        const unsigned reg = (at >> 1) & 0xff;
        const unsigned lane = at & 1;
        const unsigned count = std::min(2u - lane, size - consumed);
        const uint16_t old = registers_[reg];
        uint16_t mask = 0;
        uint16_t incoming = 0;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned shift = (lane + i) * 8;
            mask |= static_cast<uint16_t>(0xffu << shift);
            incoming |= static_cast<uint16_t>(((value >> ((consumed + i) * 8)) & 0xffu) << shift);
        }
        if (std::getenv("MAMERECOMP_ZN_TRACE_SPU") &&
            (reg == kIrqAddress || reg == kTransferAddress || reg == kControl ||
             reg == kKeyOnLow || reg == kKeyOnHigh || reg == kKeyOffLow || reg == kKeyOffHigh)) {
            static unsigned logged = 0;
            if (logged++ < 512)
                std::cerr << "ZN SPU reg[0x" << std::hex << reg << "] 0x" << old
                          << " -> 0x" << ((old & ~mask) | incoming) << std::dec << '\n';
        }
        write_register(reg, static_cast<uint16_t>((old & ~mask) | incoming));
        consumed += count;
    }
}

void Spu::dma_write(std::span<const uint32_t> words)
{
    for (uint32_t word : words) {
        write_ram_word(static_cast<uint16_t>(word));
        write_ram_word(static_cast<uint16_t>(word >> 16));
    }
}

std::vector<uint32_t> Spu::dma_read(size_t words)
{
    std::vector<uint32_t> out(words);
    for (auto &word : out) {
        const uint16_t lo = read_ram_word();
        const uint16_t hi = read_ram_word();
        word = uint32_t(lo) | (uint32_t(hi) << 16);
    }
    registers_[kTransferAddress] = static_cast<uint16_t>(transfer_byte_address_ >> 3);
    return out;
}

void Spu::advance_voice_samples(uint64_t samples)
{
    const uint32_t irq_address = (uint32_t(registers_[kIrqAddress]) << 3) & 0x7ffffu;
    const bool irq_enabled = (registers_[kControl] & kIrqEnable) &&
                             !(registers_[kStatus] & kIrqStatus);
    const int32_t master_l = static_cast<int16_t>(registers_[0xc0]);
    const int32_t master_r = static_cast<int16_t>(registers_[0xc1]);
    if (samples > (std::numeric_limits<size_t>::max() - audio_.size()) / 2) return;
    audio_.reserve(audio_.size() + static_cast<size_t>(samples) * 2);
    for (uint64_t out = 0; out < samples; ++out) {
        int32_t left = 0, right = 0;
        for (unsigned voice = 0; voice < 24; ++voice) {
            const unsigned mask_index = voice < 16 ? kVoiceOnLow : kVoiceOnHigh;
            const unsigned bit = voice < 16 ? voice : voice - 16;
            if (!(registers_[mask_index] & (1u << bit))) continue;
            const unsigned base = voice * 8;
            const uint32_t pitch = registers_[base + 2] & 0x3fffu;
            const unsigned sample = voice_sample_in_block_[voice];
            const int32_t s0 = voice_decoded_[voice][sample];
            const int32_t s1 = voice_decoded_[voice][std::min<unsigned>(sample + 1, 27u)];
            const int32_t raw_sample = s0 +
                (((s1 - s0) * int32_t(voice_fraction_[voice])) >> 12);
            const int32_t decoded = static_cast<int32_t>(raw_sample *
                std::clamp(voice_envelope_[voice], 0.0f, 1.0f));
            const int32_t vol_l = static_cast<int16_t>(registers_[base]);
            const int32_t vol_r = static_cast<int16_t>(registers_[base + 1]);
            left += (decoded * vol_l) >> 15;
            right += (decoded * vol_r) >> 15;

            voice_envelope_[voice] = std::clamp(
                voice_envelope_[voice] + voice_envelope_delta_[voice], 0.0f, 1.0f);
            const uint16_t adsr1 = registers_[base + 4];
            const uint16_t adsr2 = registers_[base + 5];
            if (voice_envelope_stage_[voice] == 0 && voice_envelope_[voice] >= 1.0f) {
                voice_envelope_stage_[voice] = 1;
                voice_envelope_delta_[voice] = -envelope_rate(
                    spu_rates::decay_rate_seconds, (adsr1 >> 4) & 0x0f);
            } else if (voice_envelope_stage_[voice] == 1 &&
                       voice_envelope_[voice] <= float((adsr1 & 0x0f) + 1) / 16.0f) {
                voice_envelope_stage_[voice] = 2;
                const unsigned rate = (adsr2 >> 6) & 0x7f;
                float delta;
                if (adsr2 & 0x8000u) {
                    if (adsr2 & 0x4000u)
                        delta = -envelope_rate(spu_rates::neg_exp_rate_seconds, rate);
                    else
                        delta = envelope_rate(spu_rates::pos_exp_rate_seconds, rate);
                } else {
                    delta = envelope_rate(spu_rates::linear_rate_seconds, rate);
                    if (adsr2 & 0x4000u) delta = -delta;
                }
                voice_envelope_delta_[voice] = delta;
            } else if ((voice_envelope_stage_[voice] == 2 &&
                        ((voice_envelope_delta_[voice] < 0.0f && voice_envelope_[voice] <= 0.0f) ||
                         (voice_envelope_delta_[voice] > 0.0f && voice_envelope_[voice] >= 1.0f))) ||
                       (voice_envelope_stage_[voice] == 3 && voice_envelope_[voice] <= 0.0f)) {
                if (voice_envelope_stage_[voice] == 2 && voice_envelope_delta_[voice] > 0.0f) {
                    voice_envelope_delta_[voice] = 0.0f;
                } else {
                    registers_[mask_index] &= static_cast<uint16_t>(~(1u << bit));
                    continue;
                }
            }

            const uint64_t phase = uint64_t(voice_fraction_[voice]) + pitch;
            voice_fraction_[voice] = static_cast<uint32_t>(phase & 0xfffu);
            unsigned advance = static_cast<unsigned>(phase >> 12);
            while (advance--) {
                if (irq_enabled && voice_block_address_[voice] == irq_address &&
                    voice_sample_in_block_[voice] == 0) {
                    registers_[kStatus] |= kIrqStatus;
                    update_irq(true);
                }
                if (++voice_sample_in_block_[voice] < 28) continue;
                voice_sample_in_block_[voice] = 0;
                const uint8_t flags = ram_[(voice_block_address_[voice] + 1) & 0x7ffffu];
                if (flags & 1u) {
                    if (!(flags & 2u)) {
                        registers_[mask_index] &= static_cast<uint16_t>(~(1u << bit));
                        continue;
                    }
                    voice_block_address_[voice] = (uint32_t(registers_[base + 7]) << 3) & 0x7fff0u;
                } else {
                    voice_block_address_[voice] = (voice_block_address_[voice] + 16) & 0x7fff0u;
                }
                voice_address_[voice] = voice_block_address_[voice];
                decode_voice_block(voice);
            }
        }
        // MAME's sound_stream_update always consumes generate() output and
        // applies the master-volume registers. It does not suppress voice
        // samples based on SPUCNT bits 15/14. Gating here dropped every sample
        // for SFEX2PA, which writes SPUCNT=0xc000 while keying on SPU voices.
        audio_.push_back(clamp_sample((left * master_l) >> 15));
        audio_.push_back(clamp_sample((right * master_r) >> 15));
    }
}

void Spu::decode_voice_block(unsigned voice)
{
    const uint32_t address = voice_block_address_[voice] & 0x7fff0u;
    const uint8_t header = ram_[address];
    const unsigned shift = std::min<unsigned>(header & 0x0f, 12);
    const unsigned filter = std::min<unsigned>(header >> 4, 4);
    const int f0 = kAdpcmFilter[filter][0], f1 = kAdpcmFilter[filter][1];
    for (unsigned i = 0; i < 28; ++i) {
        const uint8_t packed = ram_[(address + 2 + i / 2) & 0x7ffffu];
        const int32_t nibble = (i & 1) ? (packed >> 4) : (packed & 0x0f);
        const int32_t signed_nibble = (nibble & 8) ? nibble - 16 : nibble;
        int32_t decoded = (signed_nibble << 12) >> shift;
        decoded += ((voice_history_1_[voice] * f0 + voice_history_2_[voice] * f1 + 32) >> 6);
        const int16_t sample = clamp_sample(decoded);
        voice_decoded_[voice][i] = sample;
        voice_history_2_[voice] = voice_history_1_[voice];
        voice_history_1_[voice] = sample;
    }
}

void Spu::advance(uint64_t cpu_cycles, uint64_t cpu_clock_hz)
{
    if (!cpu_clock_hz) return;
    // The PSX memory callback passes the CPU's cumulative cycle counter on
    // every instruction. Advance only the newly elapsed interval, matching
    // RootTimers::advance and Sio0::advance, or sample work grows quadratically.
    if (cpu_cycles <= elapsed_cpu_cycles_) return;
    const uint64_t delta = cpu_cycles - elapsed_cpu_cycles_;
    elapsed_cpu_cycles_ = cpu_cycles;
    const uint64_t numerator = delta * kSpuSampleRate + cycle_remainder_;
    const uint64_t samples = numerator / cpu_clock_hz;
    cycle_remainder_ = numerator % cpu_clock_hz;
    if (samples) advance_voice_samples(samples);
}

uint64_t Spu::cycles_until_sample(uint64_t cpu_clock_hz) const
{
    if (!cpu_clock_hz) return UINT64_MAX;
    const uint64_t numerator_needed = cpu_clock_hz - cycle_remainder_;
    return std::max<uint64_t>(1, (numerator_needed + kSpuSampleRate - 1) /
                                  kSpuSampleRate);
}

bool Spu::irq_timing_active() const
{
    if (!(registers_[kControl] & kIrqEnable) ||
        (registers_[kStatus] & kIrqStatus))
        return false;
    return registers_[kVoiceOnLow] != 0 || registers_[kVoiceOnHigh] != 0;
}

std::vector<int16_t> Spu::take_audio()
{
    std::vector<int16_t> out;
    out.swap(audio_);
    return out;
}

} // namespace mamerecomp::psx
