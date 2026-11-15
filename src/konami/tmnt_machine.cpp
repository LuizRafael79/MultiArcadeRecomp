#include "tmnt_machine.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>
#include <neogeorecomp/neogeo.h>
#include <neogeorecomp/cpu.h>
#include "cpus/m68000/runtime/recomp_bus.h"
#include "cpus/m68000/runtime/recomp_dispatch.h"
#include "cpus/m68000/runtime/musashi_fallback.h"
#include "cpus/m68000/runtime/cpu_runtime.h"
#include <neogeorecomp/bus.h>
#include "z80.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <cstdio>

namespace multiarcaderecomp::konami {
namespace {
struct Load {
    const char* file;
    const char* region;
    size_t size;
    size_t destination;
    unsigned group;
    unsigned stride;
    uint32_t crc;
};

// Geometry and CRCs transcribed from ROM_START(tmnt) in MAME's
// src/mame/konami/tmnt.cpp.
constexpr std::array loads{
    Load{"963-x23.j17", "maincpu", 0x20000, 0x00000, 1, 2, 0xa9549004},
    Load{"963-x24.k17", "maincpu", 0x20000, 0x00001, 1, 2, 0xe5cc9067},
    Load{"963-x21.j15", "maincpu", 0x10000, 0x40000, 1, 2, 0x5789cf92},
    Load{"963-x22.k15", "maincpu", 0x10000, 0x40001, 1, 2, 0x0a74e277},
    Load{"963e20.g13", "audiocpu", 0x08000, 0, 1, 1, 0x1692a6d6},
    Load{"963a28.h27", "k052109", 0x80000, 0, 2, 4, 0xdb4769a8},
    Load{"963a29.k27", "k052109", 0x80000, 2, 2, 4, 0x8069cd2e},
    Load{"963a17.h4", "k051960", 0x80000, 0, 2, 4, 0xb5239a44},
    Load{"963a15.k4", "k051960", 0x80000, 2, 2, 4, 0x1f324eed},
    Load{"963a18.h6", "k051960", 0x80000, 0x100000, 2, 4, 0xdd51adef},
    Load{"963a16.k6", "k051960", 0x80000, 0x100002, 2, 4, 0xd4bd9984},
    Load{"963a30.g7", "proms", 0x100, 0, 1, 1, 0xabd82680},
    Load{"963a31.g19", "proms", 0x100, 0x100, 1, 1, 0xf8004a1c},
    Load{"963a26.c13", "k007232", 0x20000, 0, 1, 1, 0xe2ac3063},
    Load{"963a27.d18", "upd", 0x20000, 0, 1, 1, 0x2dfd674b},
    Load{"963a25.d5", "title", 0x80000, 0, 1, 1, 0xfca078c7},
};

uint32_t crc32_bytes(const std::vector<uint8_t>& bytes)
{
    return static_cast<uint32_t>(::crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}

// MIA and TMNT store the graphics in packed-pixel order. Their init routine
// in MAME rewires each 32-bit word into the planar bit order expected by the
// K052109 and K051960. Keep the ROM bytes transformed once at load time so
// the device decoders can use the same plane layouts as MAME.
void chunky_to_planar(std::vector<uint8_t>& region)
{
    constexpr std::array<unsigned, 32> source_bit{
        31, 27, 23, 19, 15, 11, 7, 3,
        30, 26, 22, 18, 14, 10, 6, 2,
        29, 25, 21, 17, 13, 9, 5, 1,
        28, 24, 20, 16, 12, 8, 4, 0
    };
    for (size_t offset = 0; offset + 3 < region.size(); offset += 4) {
        const uint32_t input = uint32_t(region[offset]) |
            (uint32_t(region[offset + 1]) << 8) |
            (uint32_t(region[offset + 2]) << 16) |
            (uint32_t(region[offset + 3]) << 24);
        uint32_t output = 0;
        for (unsigned bit = 0; bit < source_bit.size(); ++bit)
            output |= ((input >> source_bit[bit]) & 1u) << (31 - bit);
        region[offset] = static_cast<uint8_t>(output);
        region[offset + 1] = static_cast<uint8_t>(output >> 8);
        region[offset + 2] = static_cast<uint8_t>(output >> 16);
        region[offset + 3] = static_cast<uint8_t>(output >> 24);
    }
}

void unscramble_tmnt_sprite_rom(std::vector<uint8_t>& sprites,
                                const std::vector<uint8_t>& prom)
{
    constexpr uint8_t bit_pick[10][8] = {
        {3,3,3,3,3,3,3,3}, {0,0,5,5,5,5,5,5},
        {1,1,0,0,0,7,7,7}, {2,2,1,1,1,0,0,9},
        {4,4,2,2,2,1,1,0}, {5,6,4,4,4,2,2,1},
        {6,5,6,6,6,4,4,2}, {7,7,7,7,8,6,6,4},
        {8,8,8,8,7,8,8,6}, {9,9,9,9,9,9,9,8}
    };
    const size_t words = sprites.size() / 4;
    std::vector<uint8_t> source = sprites;
    for (size_t address = 0; address < words; ++address) {
        const unsigned entry = prom[(address & 0x7f800) >> 11] & 7;
        size_t source_word = address & 0x7fc00;
        for (unsigned bit = 0; bit < 10; ++bit)
            source_word |= ((address >> bit_pick[bit][entry]) & 1u) << bit;
        if (source_word >= words) continue;
        std::copy_n(source.begin() + source_word * 4, 4, sprites.begin() + address * 4);
    }
}
}

const std::vector<uint8_t>& TmntRomSet::at(std::string_view region) const
{
    const auto found = regions.find(std::string(region));
    if (found == regions.end())
        throw std::runtime_error("missing TMNT ROM region " + std::string(region));
    return found->second;
}

TmntRomSet load_tmnt_romset(const std::filesystem::path& zip)
{
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
    if (!reader) throw std::runtime_error("could not allocate TMNT ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open TMNT ROM ZIP " + zip.string());

    std::array<std::vector<uint8_t>, loads.size()> files;
    archive_entry* entry = nullptr;
    int result;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* path = archive_entry_pathname(entry);
        const std::string name = path ? std::filesystem::path(path).filename().string() : "";
        size_t index = loads.size();
        for (size_t i = 0; i < loads.size(); ++i)
            if (name == loads[i].file) { index = i; break; }
        if (index == loads.size() || !files[index].empty()) {
            archive_read_data_skip(reader.get());
            continue;
        }
        if (archive_entry_size(entry) != static_cast<la_int64_t>(loads[index].size))
            throw std::runtime_error(name + " has an unexpected size");
        auto& bytes = files[index];
        bytes.resize(loads[index].size);
        size_t offset = 0;
        while (offset < bytes.size()) {
            const la_ssize_t n = archive_read_data(reader.get(), bytes.data() + offset, bytes.size() - offset);
            if (n <= 0) throw std::runtime_error("truncated TMNT ROM " + name);
            offset += static_cast<size_t>(n);
        }
        if (crc32_bytes(bytes) != loads[index].crc)
            throw std::runtime_error(name + " CRC does not match MAME ROM_START metadata");
    }
    if (result != ARCHIVE_EOF)
        throw std::runtime_error("error reading TMNT ROM ZIP " + zip.string());

    TmntRomSet out;
    out.regions.emplace("maincpu", std::vector<uint8_t>(0x60000, 0xff));
    out.regions.emplace("audiocpu", std::vector<uint8_t>(0x10000, 0xff));
    out.regions.emplace("k052109", std::vector<uint8_t>(0x100000, 0xff));
    out.regions.emplace("k051960", std::vector<uint8_t>(0x200000, 0xff));
    out.regions.emplace("proms", std::vector<uint8_t>(0x200, 0xff));
    out.regions.emplace("k007232", std::vector<uint8_t>(0x20000, 0xff));
    out.regions.emplace("upd", std::vector<uint8_t>(0x20000, 0xff));
    out.regions.emplace("title", std::vector<uint8_t>(0x80000, 0xff));
    for (size_t i = 0; i < loads.size(); ++i) {
        if (files[i].empty())
            throw std::runtime_error(std::string("missing ROM ") + loads[i].file + " in " + zip.string());
        auto& region = out.regions.at(loads[i].region);
        for (size_t byte = 0; byte < files[i].size(); ++byte) {
            const size_t destination = loads[i].destination + (byte / loads[i].group) * loads[i].stride + byte % loads[i].group;
            if (destination >= region.size()) throw std::runtime_error(std::string(loads[i].file) + " exceeds its MAME region");
            region[destination] = files[i][byte];
        }
    }
    chunky_to_planar(out.regions.at("k052109"));
    chunky_to_planar(out.regions.at("k051960"));
    unscramble_tmnt_sprite_rom(out.regions.at("k051960"), out.regions.at("proms"));
    return out;
}

uint16_t TmntBoard::read_word(const std::vector<uint8_t>& bytes, size_t offset)
{
    if (offset + 1 >= bytes.size()) return 0xffff;
    return static_cast<uint16_t>((uint16_t(bytes[offset]) << 8) | bytes[offset + 1]);
}

void TmntBoard::write_word(std::vector<uint8_t>& bytes, size_t offset,
                           uint16_t value, uint16_t mem_mask)
{
    if (offset + 1 >= bytes.size()) return;
    const uint16_t old = read_word(bytes, offset);
    const uint16_t merged = static_cast<uint16_t>((old & ~mem_mask) | (value & mem_mask));
    bytes[offset] = static_cast<uint8_t>(merged >> 8);
    bytes[offset + 1] = static_cast<uint8_t>(merged);
}

TmntBoard::TmntBoard(TmntRomSet roms) : roms_(std::move(roms)), ym2151_(*this)
{
    inputs_.fill(0xffff);
    inputs_[5] = 0xff42; // MAME's factory defaults: two lives, normal difficulty, demo sound on.
    k007232_voices_[1].volume = {0, 255};
    upd7759_.set_rom(roms_.at("upd"));
    ym2151_.reset();
}

uint16_t TmntBoard::main_read(uint32_t address, uint16_t mem_mask) const
{
    address &= 0x00fffffe;
    if (address < 0x060000) return read_word(roms_.at("maincpu"), address);
    if (address >= 0x060000 && address <= 0x063ffe)
        return read_word(work_ram_, address - 0x060000);
    if (address >= 0x080000 && address <= 0x080ffe) {
        const size_t offset = (address - 0x080000) >> 1;
        return (mem_mask & 0x00ff) ? palette_ram_[offset] : 0xffff;
    }
    if (address >= 0x0a0000 && address <= 0x0a0018) {
        switch (address) {
        case 0x0a0000: return inputs_[0];
        case 0x0a0002: return inputs_[1];
        case 0x0a0004: return inputs_[2];
        case 0x0a0006: return inputs_[3];
        case 0x0a0010: return inputs_[4];
        case 0x0a0012: return inputs_[5];
        case 0x0a0014: return inputs_[6];
        case 0x0a0018: return inputs_[7];
        default: break;
        }
    }
    if (address >= 0x100000 && address <= 0x107ffe) {
        const size_t offset = (address - 0x100000) >> 1;
        const size_t mapped = ((offset & 0x3000) >> 1) | (offset & 0x07ff);
        const size_t chip_offset = mapped + ((mem_mask & 0xff00) ? 0 : 0x2000);
        if (char_rom_read_) {
            uint32_t code = (chip_offset & 0x1fff) >> 5;
            uint8_t color = rom_subbank_;
            const uint8_t bank = char_banks_[(color & 0x0c) >> 2] >> 2;
            code |= (uint32_t(color & 3) << 8) | (uint32_t(color & 0x10) << 6) |
                    (uint32_t(color & 0x0c) << 9) | (uint32_t(bank) << 13);
            const auto& chars = roms_.at("k052109");
            const size_t rom_offset = (size_t(code) << 5) + (chip_offset & 0x1f);
            const uint8_t data = chars[rom_offset & (chars.size() - 1)];
            return (mem_mask & 0xff00) ? uint16_t(data) << 8 : data;
        }
        if (mem_mask & 0xff00) return uint16_t(tile_ram_[mapped]) << 8;
        return tile_ram_[mapped + 0x2000];
    }
    if (address >= 0x140400 && address <= 0x1407fe)
        return read_word(sprite_ram_, address - 0x140400);
    if (address >= 0x140000 && address <= 0x140006) {
        const size_t offset = address - 0x140000;
        const uint16_t high = (mem_mask & 0xff00) ? uint16_t(sprite_regs_[offset]) << 8 : 0;
        const uint16_t low = (mem_mask & 0x00ff) ? sprite_regs_[offset + 1] : 0;
        return high | low;
    }
    return 0xffff;
}

void TmntBoard::main_write(uint32_t address, uint16_t value, uint16_t mem_mask)
{
    address &= 0x00fffffe;
    if (address >= 0x060000 && address <= 0x063ffe)
        write_word(work_ram_, address - 0x060000, value, mem_mask);
    else if (address >= 0x080000 && address <= 0x080ffe && (mem_mask & 0x00ff))
        palette_ram_[(address - 0x080000) >> 1] = static_cast<uint8_t>(value);
    else if (address == 0x0a0000 && (mem_mask & 0x00ff)) {
        const uint8_t data = static_cast<uint8_t>(value);
        if ((last_control_ & 8) && !(data & 8)) sound_irq_pending_ = true;
        last_control_ = data & 8;
        irq5_enabled_ = (data & 0x20) != 0;
        if (!irq5_enabled_) irq5_pending_ = false;
        char_rom_read_ = (data & 0x80) != 0;
    }
    else if (address == 0x0a0008 && (mem_mask & 0x00ff))
        sound_latch_ = static_cast<uint8_t>(value);
    else if (address == 0x0c0000 && (mem_mask & 0x00ff))
        priority_ = static_cast<uint8_t>((value >> 2) & 3);
    else if (address >= 0x100000 && address <= 0x107ffe) {
        const size_t offset = (address - 0x100000) >> 1;
        const size_t mapped = ((offset & 0x3000) >> 1) | (offset & 0x07ff);
        if (mem_mask & 0xff00) {
            tile_ram_[mapped] = static_cast<uint8_t>(value >> 8);
            if (mapped == 0x1c00) addrmap_ = static_cast<uint8_t>(value >> 8);
            else if (mapped == 0x1c80) scroll_control_ = static_cast<uint8_t>(value >> 8);
            else if (mapped == 0x1d80) {
                const uint8_t data = static_cast<uint8_t>(value >> 8);
                char_banks_[0] = data & 0x0f;
                char_banks_[1] = (data >> 4) & 0x0f;
            } else if (mapped == 0x1f00) {
                const uint8_t data = static_cast<uint8_t>(value >> 8);
                char_banks_[2] = data & 0x0f;
                char_banks_[3] = (data >> 4) & 0x0f;
            } else if (mapped == 0x1e80) tile_flip_ = static_cast<uint8_t>(value >> 8) & 7;
            else if (mapped == 0x1e00) rom_subbank_ = static_cast<uint8_t>(value >> 8);
        }
        else if (mem_mask & 0x00ff) tile_ram_[mapped + 0x2000] = static_cast<uint8_t>(value);
    }
    else if (address >= 0x140400 && address <= 0x1407fe)
        write_word(sprite_ram_, address - 0x140400, value, mem_mask);
    else if (address >= 0x140000 && address <= 0x140006) {
        const size_t offset = address - 0x140000;
        if (mem_mask & 0xff00) sprite_regs_[offset] = static_cast<uint8_t>(value >> 8);
        if (mem_mask & 0x00ff) sprite_regs_[offset + 1] = static_cast<uint8_t>(value);
    }
}

void TmntBoard::set_sound_sync_callback(std::function<void(uint64_t)> callback)
{
    sound_sync_callback_ = std::move(callback);
}

void TmntBoard::sync_sound_to_main_cycles(uint64_t main_cpu_cycles)
{
    if (!sound_sync_callback_) return;
    constexpr uint64_t main_cpu_clock_hz = 24'000'000 / 3;
    constexpr uint64_t sound_cpu_clock_hz = 3'579'545;
    const uint64_t sound_cycles = main_cpu_cycles * sound_cpu_clock_hz / main_cpu_clock_hz;
    sound_sync_callback_(sound_cycles);
}

void TmntBoard::set_input(unsigned port, uint16_t value)
{
    if (port >= inputs_.size()) throw std::out_of_range("TMNT input port index");
    inputs_[port] = value;
}

void TmntBoard::video_vblank()
{
    if (irq5_enabled_) irq5_pending_ = true;
    if (sprite_regs_[0] & 1) irq5_pending_ = true;
    if (!(sprite_regs_[0] & 0x10)) sprite_buffer_ = sprite_ram_;
}

uint8_t TmntBoard::sound_read(uint16_t address)
{
    if (address < 0x8000) return roms_.at("audiocpu")[address];
    if (address >= 0x8000 && address <= 0x87ff) return sound_ram_[address - 0x8000];
    if (address == 0x9000) return sres_control_;
    if (address == 0xa000) return sound_latch_;
    if (address >= 0xb000 && address <= 0xb00d) {
        const unsigned reg = address - 0xb000;
        if (reg == 5 || reg == 11) {
            auto& voice = k007232_voices_[reg == 11];
            if (voice.start < roms_.at("k007232").size()) {
                voice.playing = true;
                voice.address = voice.start;
                voice.counter = 0x1000;
            }
        }
        return 0;
    }
    if (address == 0xc000 || address == 0xc001)
        return ym2151_.read(address - 0xc000);
    if (address == 0xf000) return upd7759_.idle() ? 1 : 0;
    return 0xff;
}

void TmntBoard::sound_write(uint16_t address, uint8_t value)
{
    if (address >= 0x8000 && address <= 0x87ff) sound_ram_[address - 0x8000] = value;
    else if (address == 0x9000) {
        const bool play_title = (value & 4) != 0;
        if (play_title && !title_sample_playing_) {
            title_sample_position_ = 0;
            title_sample_phase_ = 0;
        }
        title_sample_playing_ = play_title;
        const bool reset_line = (value & 2) != 0;
        if (upd_reset_line_ && !reset_line) upd7759_.reset();
        upd_reset_line_ = reset_line;
        sres_control_ = value;
    }
    else if (address >= 0xb000 && address <= 0xb00d) {
        const unsigned reg = address - 0xb000;
        k007232_registers_[reg] = value;
        if (reg == 12) {
            k007232_voices_[0].volume = {uint8_t((value >> 4) * 0x11), 0};
            k007232_voices_[1].volume = {0, uint8_t((value & 0x0f) * 0x11)};
        } else if (reg == 13) {
            // Loop flags are consumed by the two independent PCM channels.
        } else if (reg < 12) {
            const unsigned voice_index = reg >= 6 ? 1 : 0;
            const unsigned base = voice_index ? 6 : 0;
            auto& voice = k007232_voices_[voice_index];
            if (reg - base <= 1)
                voice.step = static_cast<uint16_t>(((k007232_registers_[base + 1] & 0x0f) << 8) |
                                                   k007232_registers_[base]);
            else if (reg - base >= 2 && reg - base <= 4)
                voice.start = (uint32_t(k007232_registers_[base + 4] & 1) << 16) |
                    (uint32_t(k007232_registers_[base + 3]) << 8) | k007232_registers_[base + 2];
            else if (reg - base == 5 && voice.start < roms_.at("k007232").size()) {
                voice.playing = true;
                voice.address = voice.start;
                voice.counter = 0x1000;
            }
        }
    }
    else if (address == 0xc000) {
        ym_address_ = value;
        ym_registers_[0] = value;
        ym2151_.write(0, value);
    }
    else if (address == 0xc001) {
        ym_registers_[1] = value;
        ym2151_.write(1, value);
    }
    else if (address == 0xd000) upd7759_.write_port(value);
    else if (address == 0xe000) {
        const bool start_line = (value & 1) == 0;
        if (upd_start_line_ && !start_line && upd_reset_line_)
            upd7759_.start();
        upd_start_line_ = start_line;
    }
}

void TmntBoard::advance_sound_to_cycles(uint64_t cycles)
{
    constexpr uint64_t cycles_per_sample = 64;
    if (cycles <= sound_cycles_) return;
    const uint64_t target_sample = cycles / cycles_per_sample;
    const size_t count = target_sample > generated_sound_samples_
        ? static_cast<size_t>(target_sample - generated_sound_samples_) : 0;
    const auto& title = roms_.at("title");
    for (size_t i = 0; i < count; ++i) {
        const uint64_t sample_clock = (generated_sound_samples_ + i + 1) * cycles_per_sample;
        for (unsigned timer = 0; timer < ym_timer_active_.size(); ++timer) {
            if (ym_timer_active_[timer] && ym_timer_deadlines_[timer] <= sample_clock) {
                ym_timer_active_[timer] = false;
                m_engine->engine_timer_expired(timer);
            }
        }
        sound_cycles_ = sample_clock;
        // The uPD7759 runs from a 640 kHz clock; keep its state synchronized
        // with the common YM2151 output cadence (3.579545 MHz / 64).
        upd_clock_fraction_ += 640'000ull * cycles_per_sample;
        while (upd_clock_fraction_ >= 3'579'545ull) {
            upd_clock_fraction_ -= 3'579'545ull;
            upd7759_.advance(1);
        }
        if ((sample_clock & 0x7f) == 0) {
            int left = 0, right = 0;
            const auto& pcm = roms_.at("k007232");
            for (auto& voice : k007232_voices_) {
                if (!voice.playing) continue;
                uint32_t address = voice.address & 0x1ffff;
                while (voice.counter <= voice.step) {
                    if (address >= pcm.size() || (pcm[address++] & 0x80)) {
                        if (k007232_registers_[13] & (1u << (&voice - k007232_voices_.data())))
                            address = voice.start;
                        else {
                            voice.playing = false;
                            break;
                        }
                    }
                    voice.counter += 0x1000 - voice.step;
                }
                voice.address = address;
                if (!voice.playing) continue;
                voice.counter -= 32;
                const int value = (pcm[address & 0x1ffff] & 0x7f) - 0x40;
                left += value * voice.volume[0] * 2;
                right += value * voice.volume[1] * 2;
            }
            k007232_left_ = left;
            k007232_right_ = right;
        }
        ymfm::ym2151::output_data fm{};
        ym2151_.generate(&fm, 1);
        fm.clamp16();
        const int upd_mix = upd7759_.pcm_sample() * 30 / 100;
        // MAME routes both outputs of these chips to the board's single mono
        // speaker. Preserve that sum and duplicate it for the host stereo API.
        int mono_mix = (int(fm.data[0]) + int(fm.data[1])) * 40 / 100
            + (k007232_left_ + k007232_right_) * 15 / 100 + upd_mix;
        if (title_sample_playing_ && title_sample_position_ < 0x40000) {
            const size_t offset = size_t(title_sample_position_) * 2;
            const int16_t title_sample = ymfm::decode_fp(
                static_cast<int16_t>((uint16_t(title[offset]) |
                (uint16_t(title[offset + 1]) << 8)) >> 3));
            mono_mix += title_sample * 25 / 100;
            title_sample_phase_ += 20'000;
            if (title_sample_phase_ >= 55'930) {
                title_sample_phase_ -= 55'930;
                ++title_sample_position_;
                if (title_sample_position_ >= 0x40000) title_sample_playing_ = false;
            }
        }
        const auto output = static_cast<int16_t>(std::clamp(mono_mix, -32768, 32767));
        audio_queue_.push_back(output);
        audio_queue_.push_back(output);
    }
    generated_sound_samples_ = target_sample;
    sound_cycles_ = cycles;
}

std::vector<int16_t> TmntBoard::take_audio_samples()
{
    std::vector<int16_t> result;
    result.swap(audio_queue_);
    return result;
}

void TmntBoard::ymfm_set_timer(uint32_t timer, int32_t duration)
{
    if (timer >= ym_timer_active_.size()) return;
    if (duration < 0) {
        ym_timer_active_[timer] = false;
        return;
    }
    ym_timer_active_[timer] = true;
    ym_timer_deadlines_[timer] = sound_cycles_ + static_cast<uint32_t>(duration);
}

void TmntBoard::ymfm_set_busy_end(uint32_t clocks)
{ ym_busy_until_ = sound_cycles_ + clocks; }

bool TmntBoard::ymfm_is_busy()
{ return sound_cycles_ < ym_busy_until_; }

void TmntBoard::ymfm_update_irq(bool asserted)
{ ym_irq_ = asserted; }

void TmntBoard::ymfm_external_write(ymfm::access_class, uint32_t, uint8_t)
{
}

uint8_t TmntBoard::tile_pixel(unsigned layer, unsigned tile, unsigned x, unsigned y) const
{
    if (layer >= 3 || tile >= 0x800 || x >= 8 || y >= 8) return 0;
    const size_t attr_offset = layer * 0x800 + tile;
    uint8_t attr = tile_ram_[attr_offset];
    const uint32_t code_base = tile_ram_[0x2000 + attr_offset] |
        (uint32_t(tile_ram_[0x4000 + attr_offset]) << 8);
    const uint8_t bank_select = (attr >> 2) & 3;
    const uint8_t bank = char_banks_[bank_select];
    if (!(addrmap_ & 0x40)) attr = static_cast<uint8_t>((attr & 0xf3) | ((bank & 3) << 2));
    const uint32_t code = code_base | (uint32_t(attr & 3) << 8) |
        (uint32_t(attr & 0x10) << 6) | (uint32_t(attr & 0x0c) << 9) |
        (uint32_t(bank >> 2) << 13);
    if ((tile_flip_ & 4) && (attr & 2)) y = 7 - y;
    const auto& chars = roms_.at("k052109");
    const size_t address = (size_t(code) * 32 + size_t(y) * 4) & (chars.size() - 1);
    const unsigned bit = 7 - x;
    // MAME assigns output pens 8,4,2,1 in layout plane order
    // {24,16,8,0}; the first ROM plane is the most significant pen bit.
    return static_cast<uint8_t>((((chars[address + 3] >> bit) & 1) << 3) |
        (((chars[address + 2] >> bit) & 1) << 2) |
        (((chars[address + 1] >> bit) & 1) << 1) |
        ((chars[address] >> bit) & 1));
}

TmntFrame TmntBoard::render_frame() const
{
    TmntFrame frame;
    frame.palette_indices.resize(size_t(frame.width) * frame.height);
    frame.shadow_pixels.assign(frame.palette_indices.size(), 0);
    constexpr std::array<unsigned, 3> palette_base{0, 32, 40};
    auto draw_layer = [&](unsigned layer, bool opaque) {
        const bool scrollable = layer != 0;
        const size_t scroll_base = layer == 1 ? 0x1800 : 0x3800;
        const size_t x_base = layer == 1 ? 0x1a00 : 0x3a00;
        const unsigned control = scrollable
            ? ((scroll_control_ >> ((layer - 1) * 3)) & 7) : 0;
        constexpr std::array<unsigned, 4> rows_table{1, 1, 32, 256};
        const unsigned rows = scrollable ? rows_table[control & 3] : 1;
        const bool column_scroll = scrollable && (control & 4);
        const unsigned base_x = scrollable
            ? tile_ram_[x_base] | (unsigned(tile_ram_[x_base + 1]) << 8) : 0;
        const unsigned base_y = scrollable ? tile_ram_[scroll_base + 12] : 0;
        for (unsigned y = 0; y < frame.height; ++y) {
            for (unsigned x = 0; x < frame.width; ++x) {
                unsigned sx = x + (layer == 0 ? 96 : 90) + base_x;
                unsigned sy = y + 16 + base_y;
                if (scrollable && rows > 1) {
                    const unsigned row_mask = rows == 256 ? 0xff : 0xf8;
                    const size_t row_offset = x_base + 2 * (sy & row_mask);
                    sx = x + 90 +
                        tile_ram_[row_offset] + (unsigned(tile_ram_[row_offset + 1]) << 8);
                }
                if (column_scroll) {
                    const unsigned column = (sx >> 3) & 0x3f;
                    sy = y + tile_ram_[scroll_base + column];
                }
                sx &= 0x1ff;
                sy &= 0xff;
                if (tile_flip_ & 1) { sx = 0x1ff - sx; sy = 0xff - sy; }
                const unsigned tile = ((sy >> 3) & 31) * 64 + ((sx >> 3) & 63);
                const uint8_t pen = tile_pixel(layer, tile, sx & 7, sy & 7);
                if (!pen && !opaque) continue;
                const uint8_t attr = tile_ram_[layer * 0x800 + tile];
                const unsigned color = palette_base[layer] + ((attr & 0xe0) >> 5);
                frame.palette_indices[size_t(y) * frame.width + x] =
                    static_cast<uint16_t>(color * 16 + pen);
            }
        }
    };
    constexpr std::array<unsigned, 8> xoffset{0, 1, 4, 5, 16, 17, 20, 21};
    constexpr std::array<unsigned, 8> yoffset{0, 2, 8, 10, 32, 34, 40, 42};
    constexpr std::array<unsigned, 8> widths{1, 2, 1, 2, 4, 2, 4, 8};
    constexpr std::array<unsigned, 8> heights{1, 1, 2, 2, 2, 4, 4, 8};
    constexpr std::array<unsigned, 64> zoom_y_table{
        0x00,0x01,0x03,0x05,0x07,0x09,0x0a,0x0c,0x0e,0x0f,0x11,0x12,0x14,0x15,0x16,0x18,
        0x19,0x1a,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,
        0x2a,0x2b,0x2c,0x2d,0x2e,0x2e,0x2f,0x30,0x31,0x31,0x32,0x33,0x34,0x34,0x35,0x36,
        0x36,0x37,0x38,0x38,0x39,0x39,0x3a,0x3b,0x3b,0x3c,0x3c,0x3d,0x3d,0x3e,0x3e,0x3f};
    auto sprite_pixel = [&roms = roms_](unsigned code, unsigned x, unsigned y) {
        const auto& gfx = roms.at("k051960");
        const size_t address = size_t(code) * 128 + (y & 7) * 4 + ((y & 8) ? 64 : 0) + ((x & 8) ? 32 : 0);
        if (address + 3 >= gfx.size()) return uint8_t(0);
        const unsigned bit = 7 - (x & 7);
        return static_cast<uint8_t>((((gfx[address + 3] >> bit) & 1) << 3) |
            (((gfx[address + 2] >> bit) & 1) << 2) |
            (((gfx[address + 1] >> bit) & 1) << 1) |
            ((gfx[address] >> bit) & 1));
    };
    auto draw_sprites = [&] {
      for (unsigned order = 0; order < 128; ++order) {
        int sprite = -1;
        for (unsigned i = 0; i < 128; ++i) {
            const size_t base = size_t(i) * 8;
            if ((sprite_buffer_[base] & 0x80) && (sprite_buffer_[base] & 0x7f) == order) {
                sprite = static_cast<int>(base);
                break;
            }
        }
        if (sprite < 0) continue;
        const size_t base = static_cast<size_t>(sprite);
        const uint8_t b1 = sprite_buffer_[base + 1];
        const uint8_t color_attr = sprite_buffer_[base + 3];
        uint32_t code = sprite_buffer_[base + 2] + ((b1 & 0x1f) << 8);
        code |= (color_attr & 0x10) << 9;
        const unsigned color = 16 + (color_attr & 0x0f);
        const unsigned size = (b1 & 0xe0) >> 5;
        const unsigned w = widths[size], h = heights[size];
        if (w >= 2) code &= ~1u;
        if (h >= 2) code &= ~2u;
        if (w >= 4) code &= ~4u;
        if (h >= 4) code &= ~8u;
        if (w >= 8) code &= ~16u;
        if (h >= 8) code &= ~32u;
        const uint8_t b4 = sprite_buffer_[base + 4], b6 = sprite_buffer_[base + 6];
        int ox = ((int(b6) << 8) | sprite_buffer_[base + 7]) & 0x1ff;
        int oy = 256 - (((int(b4) << 8) | sprite_buffer_[base + 5]) & 0x1ff);
        bool flip_x = (b6 & 2) != 0, flip_y = (b4 & 2) != 0;
        const unsigned zoom_x_byte = (b6 & 0xfc) >> 2;
        uint32_t zoom_x = 512 * (128 - zoom_x_byte);
        unsigned zoom_y = 128 - zoom_y_table[(b4 & 0xfc) >> 2];
        for (unsigned i = 0; i < 3; ++i) if (h <= (1u << i)) zoom_y = (zoom_y + 1) / 2;
        zoom_y *= 8 / h;
        uint32_t zoom_y_fixed = (0x10000 / 128) * zoom_y;
        if (sprite_regs_[0] & 8) {
            ox = 512 - int((zoom_x * w) >> 12) - ox;
            oy = 256 - int((zoom_y_fixed * h) >> 12) - oy;
            flip_x = !flip_x;
            flip_y = !flip_y;
        }
        for (unsigned ty = 0; ty < h; ++ty) {
            const int y0 = oy + int((zoom_y_fixed * ty + 0x800) >> 12);
            const int y1 = oy + int((zoom_y_fixed * (ty + 1) + 0x800) >> 12);
            unsigned row = code + (flip_y ? yoffset[h - 1 - ty] : yoffset[ty]);
            for (unsigned tx = 0; tx < w; ++tx) {
                const int x0 = ox + int((zoom_x * tx + 0x800) >> 12);
                const int x1 = ox + int((zoom_x * (tx + 1) + 0x800) >> 12);
                const unsigned tile_code = row + (flip_x ? xoffset[w - 1 - tx] : xoffset[tx]);
                for (int py = y0; py < y1; ++py) {
                    if (py < 16 || py >= 240) continue;
                    unsigned src_y = unsigned((uint64_t(py - y0) * 16) / std::max(1, y1 - y0));
                    if (flip_y) src_y = 15 - src_y;
                    for (int px = x0; px < x1; ++px) {
                        const int screen_x = (px & 0x1ff) - 96;
                        if (screen_x < 0 || screen_x >= 320) continue;
                        unsigned src_x = unsigned((uint64_t(px - x0) * 16) / std::max(1, x1 - x0));
                        if (flip_x) src_x = 15 - src_x;
                        const uint8_t pen = sprite_pixel(tile_code, src_x, src_y);
                        if (!pen) continue;
                        const size_t out = size_t(py - 16) * frame.width + screen_x;
                        if ((color_attr & 0x80) && pen == 15) frame.shadow_pixels[out] = 1;
                        else frame.palette_indices[out] = static_cast<uint16_t>(color * 16 + pen);
                    }
                }
            }
        }
      }
    };
    draw_layer(2, true);
    if (priority_ & 1) draw_sprites();
    draw_layer(1, false);
    if (!(priority_ & 1)) draw_sprites();
    draw_layer(0, false);
    return frame;
}

std::vector<uint8_t> TmntBoard::frame_rgb(const TmntFrame& frame) const
{
    const size_t pixels = size_t(frame.width) * frame.height;
    if (frame.palette_indices.size() != pixels || frame.shadow_pixels.size() != pixels)
        throw std::runtime_error("TMNT framebuffer has inconsistent dimensions");
    std::vector<uint8_t> rgb(pixels * 3);
    for (size_t i = 0; i < pixels; ++i) {
        const size_t palette_offset = size_t(frame.palette_indices[i]) * 2;
        // The shared palette RAM follows the 68000's big-endian byte order;
        // xBGR555 then maps bits 0..4 to red, 5..9 to green, and 10..14 to blue.
        const uint16_t raw = static_cast<uint16_t>((uint16_t(palette_ram_[palette_offset]) << 8) |
                                                    palette_ram_[palette_offset + 1]);
        const unsigned scale = frame.shadow_pixels[i] ? 3 : 1;
        const unsigned divisor = frame.shadow_pixels[i] ? 4 : 1;
        const unsigned r = ((raw & 0x1f) * 255 / 31) * scale / divisor;
        const unsigned g = (((raw >> 5) & 0x1f) * 255 / 31) * scale / divisor;
        const unsigned b = (((raw >> 10) & 0x1f) * 255 / 31) * scale / divisor;
        rgb[i * 3] = static_cast<uint8_t>(r);
        rgb[i * 3 + 1] = static_cast<uint8_t>(g);
        rgb[i * 3 + 2] = static_cast<uint8_t>(b);
    }
    return rgb;
}

namespace {
bool tmnt_read8(void* context, uint32_t address, uint8_t* value)
{
    auto& board = *static_cast<TmntBoard*>(context);
    const uint16_t mask = (address & 1) ? 0x00ff : 0xff00;
    const auto word = board.main_read(address & ~1u, mask);
    *value = (address & 1) ? uint8_t(word) : uint8_t(word >> 8);
    return true;
}
bool tmnt_read16(void* context, uint32_t address, uint16_t* value)
{ *value = static_cast<TmntBoard*>(context)->main_read(address); return true; }
bool tmnt_write8(void* context, uint32_t address, uint8_t value)
{
    if ((address & 0x00fffffeu) == 0x0a0000 || (address & 0x00fffffeu) == 0x0a0008)
        static_cast<TmntBoard*>(context)->sync_sound_to_main_cycles(static_cast<uint64_t>(ng_cycles));
    if (address & 1) static_cast<TmntBoard*>(context)->main_write(address & ~1u, value, 0x00ff);
    else static_cast<TmntBoard*>(context)->main_write(address, uint16_t(value) << 8, 0xff00);
    return true;
}
bool tmnt_write16(void* context, uint32_t address, uint16_t value)
{
    if ((address & 0x00fffffeu) == 0x0a0000 || (address & 0x00fffffeu) == 0x0a0008)
        static_cast<TmntBoard*>(context)->sync_sound_to_main_cycles(static_cast<uint64_t>(ng_cycles));
    static_cast<TmntBoard*>(context)->main_write(address, value);
    return true;
}
uint16_t tmnt_exception_read16(uint32_t address) { return ng_r16(address); }
void tmnt_exception_write16(uint32_t address, uint16_t value) { ng_w16(address, value); }
}

TmntMainCpu::TmntMainCpu(TmntBoard& board, const ng_func_entry_t* entries, size_t entry_count)
    : board_(board), entries_(entries), entry_count_(entry_count)
{
    if (!entries_ || entry_count_ == 0) throw std::runtime_error("TMNT 68000 AOT table is empty");
    const M68kBusCallbacks bus{&board_, tmnt_read8, tmnt_read16, tmnt_write8, tmnt_write16};
    multiarcaderecomp_m68k_bus_bind(&bus);
    multiarcade_recomp_m68k_set_exception_bus(tmnt_exception_read16, tmnt_exception_write16);
    reset();
}

TmntMainCpu::~TmntMainCpu()
{
    multiarcaderecomp_m68k_bus_bind(nullptr);
}

void TmntMainCpu::reset()
{
    ng_cpu = {};
    ng_cpu.a[7] = (uint32_t(board_.main_read(0)) << 16) | board_.main_read(2);
    ng_cpu.pc = ((uint32_t(board_.main_read(4)) << 16) | board_.main_read(6)) & 0x00ffffffu;
    ng_cpu.s = 1;
    ng_cpu.ipl = 7;
    ng_cycles = 0;
    ng_next_event = 0;
    ng_icount = 0;
}

int TmntMainCpu::execute_until(uint64_t cycle_deadline)
{
    const int64_t start = ng_cycles;
    const int64_t target = static_cast<int64_t>(cycle_deadline);
    if (target <= start) return 0;
    while (ng_cycles < target) {
        if (board_.irq5_pending() && ng_cpu.ipl < 5) {
            const auto irq = m68k_recomp_accept_interrupt(5);
            if (irq.status != M68K_RECOMP_DISPATCH_OK)
                throw std::runtime_error("TMNT 68000 could not enter its level-5 interrupt");
        }
        if (ng_cpu.stopped) { ng_cycles = target; break; }
        ng_next_event = target;
        const uint64_t instruction_count_before = ng_icount;
        const auto result = m68k_recomp_dispatch_once(entries_, entry_count_);
        if (result.status == M68K_RECOMP_DISPATCH_NO_ENTRY) {
            multiarcaderecomp_m68k_bus_clear_fault();
            const int consumed = multiarcaderecomp_m68k_musashi_step();
            ng_cycles += consumed > 0 ? consumed : 1;
            ++ng_icount;
            ++fallback_instructions_;
            continue;
        }
        if (result.status != M68K_RECOMP_DISPATCH_OK) {
            char message[160];
            std::snprintf(message, sizeof(message), "TMNT 68000 dispatch failed at PC=0x%06X (status %u)",
                result.pc, static_cast<unsigned>(result.status));
            throw std::runtime_error(message);
        }
        aot_instructions_ += ng_icount - instruction_count_before;
    }
    return static_cast<int>(ng_cycles - start);
}

uint32_t TmntMainCpu::pc() const { return ng_cpu.pc; }
uint64_t TmntMainCpu::cycles() const { return static_cast<uint64_t>(ng_cycles); }

} // namespace multiarcaderecomp::konami
