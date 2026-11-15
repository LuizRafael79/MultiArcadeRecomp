#include "oki6295.hpp"

#include <algorithm>

namespace multiarcaderecomp::chips {
namespace {
constexpr std::array<int, 49> kStepTable{
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
    279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552,
};
constexpr std::array<int, 8> kIndexShift{-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 16> kVolume{32, 22, 16, 11, 8, 6, 4, 3,
                                       2, 0, 0, 0, 0, 0, 0, 0};
}

Oki6295::Oki6295(std::span<const std::uint8_t> rom, RomRead read, void* context)
    : rom_(rom), read_(read), context_(context) {}

void Oki6295::reset()
{
    voices_ = {};
    pending_sample_ = -1;
}

std::uint8_t Oki6295::rom_read(std::uint32_t address) const
{
    if (read_) return read_(context_, address);
    return address < rom_.size() ? rom_[address] : 0xff;
}

std::uint32_t Oki6295::sample_address(std::uint32_t offset) const
{
    return (std::uint32_t(rom_read(offset)) << 16) |
           (std::uint32_t(rom_read(offset + 1)) << 8) |
           std::uint32_t(rom_read(offset + 2));
}

void Oki6295::write(std::uint8_t value)
{
    if (pending_sample_ >= 0) {
        const unsigned voice_mask = value >> 4;
        const std::uint32_t table = static_cast<std::uint32_t>(pending_sample_) * 8;
        const std::uint32_t start = sample_address(table) & 0x3ffff;
        const std::uint32_t end = sample_address(table + 3) & 0x3ffff;
        if (start < end && end < 0x40000) {
            for (unsigned index = 0; index < voices_.size(); ++index) {
                if (!(voice_mask & (1u << index)) || voices_[index].playing) continue;
                voices_[index] = {true, start, 0, (end - start + 1) * 2,
                                  0, 0, static_cast<std::uint8_t>(value & 15)};
            }
        }
        pending_sample_ = -1;
    } else if (value & 0x80) {
        pending_sample_ = value & 0x7f;
    } else {
        const unsigned voice_mask = (value >> 3) & 15;
        for (unsigned index = 0; index < voices_.size(); ++index)
            if (voice_mask & (1u << index)) voices_[index].playing = false;
    }
}

std::uint8_t Oki6295::read() const
{
    std::uint8_t status = 0xf0;
    for (unsigned index = 0; index < voices_.size(); ++index)
        if (voices_[index].playing) status |= static_cast<std::uint8_t>(1u << index);
    return status;
}

unsigned Oki6295::active_voices() const
{
    unsigned active = 0;
    for (const auto& voice : voices_) active += voice.playing;
    return active;
}

void Oki6295::clock_nibble()
{
    for (auto& voice : voices_) {
        if (!voice.playing) continue;
        if (voice.nibble >= voice.nibble_count) {
            voice.playing = false;
            continue;
        }
        const std::uint8_t byte = rom_read(voice.start + voice.nibble / 2);
        const unsigned nibble = (voice.nibble & 1) ? byte & 15 : byte >> 4;
        const int step = kStepTable[voice.step];
        int delta = ((2 * static_cast<int>(nibble & 7) + 1) * step) >> 3;
        if (nibble & 8) delta = -delta;
        voice.signal = std::clamp(voice.signal + delta, -2048, 2047);
        voice.step = std::clamp(voice.step + kIndexShift[nibble & 7], 0, 48);
        if (++voice.nibble >= voice.nibble_count) voice.playing = false;
    }
}

std::int16_t Oki6295::output() const
{
    int mix = 0;
    for (const auto& voice : voices_)
        if (voice.playing) mix += voice.signal * kVolume[voice.volume] / 32;
    return static_cast<std::int16_t>(std::clamp(mix, -2048, 2047));
}

} // namespace multiarcaderecomp::chips
