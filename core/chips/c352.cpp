// license:BSD-3-Clause
// Adapted from MAME src/devices/sound/c352.cpp by R. Belmont, superctr, cync,
// and Hoot contributors. Standalone interface for MultiArcadeRecomp.
#include "c352.hpp"

#include <algorithm>

namespace multiarcaderecomp::chips {
namespace {
constexpr uint16_t BUSY = 0x8000;
constexpr uint16_t KEYON = 0x4000;
constexpr uint16_t KEYOFF = 0x2000;
constexpr uint16_t LOOPHIST = 0x0800;
constexpr uint16_t PHASERL = 0x0200;
constexpr uint16_t PHASEFL = 0x0100;
constexpr uint16_t PHASEFR = 0x0080;
constexpr uint16_t LDIR = 0x0040;
constexpr uint16_t LINK = 0x0020;
constexpr uint16_t NOISE = 0x0010;
constexpr uint16_t MULAW = 0x0008;
constexpr uint16_t FILTER = 0x0004;
constexpr uint16_t LOOP = 0x0002;
constexpr uint16_t REVERSE = 0x0001;
}

C352::C352(std::span<const uint8_t> rom) : rom_(rom.begin(), rom.end())
{
    int value = 0;
    for (int i = 0; i < 128; ++i) {
        mulaw_[i] = static_cast<int16_t>(value << 5);
        value += i < 16 ? 1 : i < 24 ? 2 : i < 48 ? 4 : i < 100 ? 8 : 16;
    }
    for (int i = 0; i < 128; ++i)
        mulaw_[i + 128] = static_cast<int16_t>(~mulaw_[i]);
}

uint16_t C352::read(uint16_t offset) const
{
    if (offset < 0x100) {
        const Voice& v = voices_[offset / 8];
        switch (offset & 7) {
        case 0: return v.vol_front;
        case 1: return v.vol_rear;
        case 2: return v.frequency;
        case 3: return v.flags;
        case 4: return v.wave_bank;
        case 5: return v.wave_start;
        case 6: return v.wave_end;
        case 7: return v.wave_loop;
        }
    }
    return offset == 0x200 ? control_ : 0;
}

void C352::write(uint16_t offset, uint16_t data, uint16_t mem_mask)
{
    if (offset < 0x100) {
        const uint16_t old = read(offset);
        const uint16_t value = (old & ~mem_mask) | (data & mem_mask);
        Voice& v = voices_[offset / 8];
        switch (offset & 7) {
        case 0: v.vol_front = value; break;
        case 1: v.vol_rear = value; break;
        case 2: v.frequency = value; break;
        case 3: v.flags = value; break;
        case 4: v.wave_bank = value; break;
        case 5: v.wave_start = value; break;
        case 6: v.wave_end = value; break;
        case 7: v.wave_loop = value; break;
        }
    } else if (offset == 0x200) {
        control_ = (control_ & ~mem_mask) | (data & mem_mask);
    } else if (offset == 0x202 && mem_mask == 0xffff) {
        for (Voice& v : voices_) {
            if (v.flags & KEYON) {
                v.pos = (uint32_t(v.wave_bank) << 16) | v.wave_start;
                v.sample = v.last_sample = 0;
                v.counter = 0xffff;
                v.flags = (v.flags | BUSY) & ~(KEYON | LOOPHIST);
                v.current_volume.fill(0);
            }
            if (v.flags & KEYOFF) {
                v.flags &= ~(BUSY | KEYOFF);
                v.counter = 0xffff;
            }
        }
    }
}

void C352::fetch_sample(Voice& v)
{
    v.last_sample = v.sample;
    if (v.flags & NOISE) {
        random_ = static_cast<uint16_t>((random_ >> 1) ^ (-(random_ & 1) & 0xfff6));
        v.sample = static_cast<int16_t>(random_);
    } else {
        const int8_t raw = static_cast<int8_t>(rom_.empty() ? 0 : rom_[v.pos % rom_.size()]);
        v.sample = (v.flags & MULAW) ? mulaw_[static_cast<uint8_t>(raw)]
                                     : static_cast<int16_t>(int(raw) * 256);
        const uint16_t pos = static_cast<uint16_t>(v.pos);
        if ((v.flags & (LOOP | REVERSE)) == (LOOP | REVERSE)) {
            if ((v.flags & LDIR) && pos == v.wave_loop) v.flags &= ~LDIR;
            else if (!(v.flags & LDIR) && pos == v.wave_end) v.flags |= LDIR;
            v.pos += (v.flags & LDIR) ? uint32_t(-1) : 1;
        } else if (pos == v.wave_end) {
            if ((v.flags & (LINK | LOOP)) == (LINK | LOOP)) {
                v.pos = (uint32_t(v.wave_start) << 16) | v.wave_loop;
                v.flags |= LOOPHIST;
            } else if (v.flags & LOOP) {
                v.pos = (v.pos & 0xff0000) | v.wave_loop;
                v.flags |= LOOPHIST;
            } else {
                v.flags = (v.flags | KEYOFF) & ~BUSY;
                v.sample = 0;
            }
        } else {
            v.pos += (v.flags & REVERSE) ? uint32_t(-1) : 1;
        }
    }
}

void C352::ramp_volume(Voice& v, unsigned channel, uint8_t target)
{
    if (v.current_volume[channel] < target) ++v.current_volume[channel];
    else if (v.current_volume[channel] > target) --v.current_volume[channel];
}

std::vector<int16_t> C352::render(size_t frames)
{
    std::vector<int16_t> output(frames * 2);
    for (size_t frame = 0; frame < frames; ++frame) {
        int32_t left = 0, right = 0;
        for (Voice& v : voices_) {
            int32_t sample = 0;
            if (v.flags & BUSY) {
                const uint32_t next = v.counter + v.frequency;
                if (next & 0x10000) fetch_sample(v);
                if ((next ^ v.counter) & 0x18000) {
                    ramp_volume(v, 0, v.vol_front >> 8);
                    ramp_volume(v, 1, v.vol_front & 0xff);
                    ramp_volume(v, 2, v.vol_rear >> 8);
                    ramp_volume(v, 3, v.vol_rear & 0xff);
                }
                v.counter = next & 0xffff;
                sample = v.sample;
                if (!(v.flags & FILTER))
                    sample = v.last_sample + (int32_t(v.counter) * (v.sample - v.last_sample) >> 16);
            }
            const int32_t fl = (v.flags & PHASEFL) ? -sample : sample;
            const int32_t fr = (v.flags & PHASEFR) ? -sample : sample;
            left += (fl * v.current_volume[0]) >> 8;
            right += (fr * v.current_volume[1]) >> 8;
        }
        output[frame * 2] = static_cast<int16_t>(std::clamp(left >> 3, -32768, 32767));
        output[frame * 2 + 1] = static_cast<int16_t>(std::clamp(right >> 3, -32768, 32767));
        ++samples_rendered_;
    }
    return output;
}

unsigned C352::active_voices() const
{
    unsigned count = 0;
    for (const Voice& v : voices_) count += (v.flags & BUSY) != 0;
    return count;
}

} // namespace multiarcaderecomp::chips
