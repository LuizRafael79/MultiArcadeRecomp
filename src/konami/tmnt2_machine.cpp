#include "tmnt2_machine.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

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

constexpr Load tmnt2_loads[] = {
    {"063uaa02.8e", "maincpu", 0x20000, 0x00000, 1, 2, 0x58d5c93d},
    {"063uaa03.8g", "maincpu", 0x20000, 0x00001, 1, 2, 0x0541fec9},
    {"063uaa04.10e", "maincpu", 0x20000, 0x40000, 1, 2, 0x1d441a7d},
    {"063uaa05.10g", "maincpu", 0x20000, 0x40001, 1, 2, 0x9c428273},
    {"063b01.2f", "audiocpu", 0x10000, 0, 1, 1, 0x364f548a},
    {"063b12.16k", "k052109", 0x80000, 0, 2, 4, 0xd3283d19},
    {"063b11.12k", "k052109", 0x80000, 2, 2, 4, 0x6ebc0c15},
    {"063b09.7l", "k053245", 0x100000, 0, 2, 4, 0x2d7a9d2a},
    {"063b07.3l", "k053245", 0x100000, 2, 2, 4, 0xd9bee7bf},
    {"063b10.7k", "k053245", 0x80000, 0x200000, 2, 4, 0xf2dd296e},
    {"063b08.3k", "k053245", 0x80000, 0x200002, 2, 4, 0x3b1ae36f},
    {"063b06.1d", "k053260", 0x200000, 0, 1, 1, 0x1e510aa5},
    {"tmnt2_uaa.nv", "eeprom", 0x80, 0, 1, 1, 0x284357bd}
};

constexpr Load ssriders_loads[] = {
    {"064eac02.8e", "maincpu", 0x40000, 0, 1, 2, 0x5a5425f4},
    {"064eac03.8g", "maincpu", 0x40000, 1, 1, 2, 0x093c00fb},
    {"064eab04.10e", "maincpu", 0x20000, 0x80000, 1, 2, 0xef2315bd},
    {"064eab05.10g", "maincpu", 0x20000, 0x80001, 1, 2, 0x51d6fbc4},
    {"064e01.2f", "audiocpu", 0x10000, 0, 1, 1, 0x44b9bc52},
    {"064e12.16k", "k052109", 0x80000, 0, 2, 4, 0xe2bdc619},
    {"064e11.12k", "k052109", 0x80000, 2, 2, 4, 0x2d8ca8b0},
    {"064e09.7l", "k053245", 0x100000, 0, 2, 4, 0x4160c372},
    {"064e07.3l", "k053245", 0x100000, 2, 2, 4, 0x64dd673c},
    {"064e06.1d", "k053260", 0x100000, 0, 1, 1, 0x59810df9},
    {"ssriders_eac.nv", "eeprom", 0x80, 0, 1, 1, 0xf6d641a7}
};

struct Profile {
    const char* id;
    size_t main_size;
    size_t tile_size;
    size_t sprite_size;
    size_t sample_size;
    const Load* loads;
    size_t count;
};

constexpr Profile profiles[] = {
    {"tmnt2", 0x100000, 0x100000, 0x400000, 0x200000,
     tmnt2_loads, std::size(tmnt2_loads)},
    {"ssriders", 0x0c0000, 0x100000, 0x200000, 0x100000,
     ssriders_loads, std::size(ssriders_loads)}
};

const Profile& profile_for(std::string_view id)
{
    for (const auto& profile : profiles)
        if (id == profile.id) return profile;
    throw std::runtime_error("unsupported Konami TMNT2-board set: " + std::string(id));
}

uint32_t crc32_bytes(const std::vector<uint8_t>& bytes)
{
    return static_cast<uint32_t>(::crc32(0, bytes.data(), static_cast<uInt>(bytes.size())));
}
}

const std::vector<uint8_t>& Tmnt2RomSet::at(std::string_view region) const
{
    const auto found = regions.find(std::string(region));
    if (found == regions.end())
        throw std::runtime_error("missing Konami ROM region " + std::string(region));
    return found->second;
}

Tmnt2RomSet load_tmnt2_romset(std::string_view game,
                               const std::filesystem::path& zip)
{
    const auto& profile = profile_for(game);
    const auto* loads = profile.loads;
    const size_t count = profile.count;
    std::unique_ptr<archive, decltype(&archive_read_free)> reader(
        archive_read_new(), archive_read_free);
    if (!reader) throw std::runtime_error("could not allocate Konami ZIP reader");
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_zip(reader.get());
    if (archive_read_open_filename(reader.get(), zip.c_str(), 10240) != ARCHIVE_OK)
        throw std::runtime_error("could not open Konami ROM ZIP " + zip.string());

    std::vector<std::vector<uint8_t>> files(count);
    archive_entry* entry = nullptr;
    int result;
    while ((result = archive_read_next_header(reader.get(), &entry)) == ARCHIVE_OK) {
        const char* path = archive_entry_pathname(entry);
        const std::string name = path ? std::filesystem::path(path).filename().string() : "";
        size_t index = count;
        for (size_t i = 0; i < count; ++i)
            if (name == loads[i].file) { index = i; break; }
        if (index == count || !files[index].empty()) {
            archive_read_data_skip(reader.get());
            continue;
        }
        if (archive_entry_size(entry) != static_cast<la_int64_t>(loads[index].size))
            throw std::runtime_error(name + " has an unexpected size");
        auto& bytes = files[index];
        bytes.resize(loads[index].size);
        size_t offset = 0;
        while (offset < bytes.size()) {
            const la_ssize_t n = archive_read_data(reader.get(), bytes.data() + offset,
                                                   bytes.size() - offset);
            if (n <= 0) throw std::runtime_error("truncated Konami ROM " + name);
            offset += static_cast<size_t>(n);
        }
        if (crc32_bytes(bytes) != loads[index].crc)
            throw std::runtime_error(name + " CRC does not match MAME ROM_START metadata");
    }
    if (result != ARCHIVE_EOF)
        throw std::runtime_error("error reading Konami ROM ZIP " + zip.string());

    Tmnt2RomSet out;
    out.regions.emplace("maincpu", std::vector<uint8_t>(profile.main_size, 0xff));
    out.regions.emplace("audiocpu", std::vector<uint8_t>(0x10000, 0xff));
    out.regions.emplace("k052109", std::vector<uint8_t>(profile.tile_size, 0xff));
    out.regions.emplace("k053245", std::vector<uint8_t>(profile.sprite_size, 0xff));
    out.regions.emplace("k053260", std::vector<uint8_t>(profile.sample_size, 0xff));
    out.regions.emplace("eeprom", std::vector<uint8_t>(0x80, 0xff));
    for (size_t i = 0; i < count; ++i) {
        if (files[i].empty())
            throw std::runtime_error(std::string("missing ROM ") + loads[i].file + " in " + zip.string());
        auto& region = out.regions.at(loads[i].region);
        const auto& source = files[i];
        const auto& load = loads[i];
        for (size_t byte = 0; byte < source.size(); ++byte) {
            const size_t destination = load.destination + (byte / load.group) * load.stride + byte % load.group;
            if (destination >= region.size())
                throw std::runtime_error(std::string(load.file) + " exceeds its MAME ROM region");
            region[destination] = source[byte];
        }
    }
    return out;
}

Tmnt2Board::Tmnt2Board(std::string_view game, Tmnt2RomSet roms)
    : tmnt2_(game == "tmnt2"), screen_width_(game == "tmnt2" ? 304 : 288),
      visible_x_(game == "tmnt2" ? 8 : 16), roms_(std::move(roms)),
      eeprom_(multiarcaderecomp::devices::SerialEeprom::Protocol::Er5911_8Bit,
              roms_.at("eeprom")),
      ym2151_(*this)
{
    if (!tmnt2_ && game != "ssriders")
        throw std::runtime_error("ROM set and TMNT2-board profile do not match");
    ym2151_.reset();
}

uint16_t Tmnt2Board::read_word(const std::vector<uint8_t>& bytes, size_t offset) const
{
    if (offset + 1 >= bytes.size()) return 0xffff;
    return static_cast<uint16_t>((uint16_t(bytes[offset]) << 8) | bytes[offset + 1]);
}

void Tmnt2Board::write_word(std::vector<uint8_t>& bytes, size_t offset,
                            uint16_t value, uint16_t mem_mask)
{
    if (offset + 1 >= bytes.size()) return;
    const uint16_t old = read_word(bytes, offset);
    const uint16_t merged = static_cast<uint16_t>((old & ~mem_mask) | (value & mem_mask));
    bytes[offset] = static_cast<uint8_t>(merged >> 8);
    bytes[offset + 1] = static_cast<uint8_t>(merged);
}

uint16_t Tmnt2Board::protection_read() const
{
    // The MAME handler samples absolute work-RAM addresses 0x105a0a and
    // 0x1058fc. The work RAM window starts at 0x104000, so these are offsets
    // 0x1a0a and 0x18fc (not 0x0a0a/0x08fc).
    const uint16_t data = read_word(work_ram_, 0x1a0a);
    switch (read_word(work_ram_, 0x18fc)) {
    case 0x100b: return 0x0064;
    case 0x6003: return data & 0x000f;
    case 0x6004: return data & 0x001f;
    case 0x6000: return data & 0x0001;
    case 0x0000:
    case 0x6007: return data & 0x00ff;
    case 0x8abc: {
        int result = -int(read_word(work_ram_, 0x1818));
        result = ((result / 8 - 4) & 0x1f) * 0x40;
        result += ((int(read_word(work_ram_, 0x1cb0)) +
                    int(read_word(work_ram_, 0x00c8)) - 6) / 8 + 12) & 0x3f;
        return static_cast<uint16_t>(result);
    }
    default: return 0xffff;
    }
}

uint16_t Tmnt2Board::tmnt2_protection_read_word(uint32_t word_address) const
{
    const uint32_t address = word_address * 2;
    if (address <= 0x07fffe)
        return read_word(roms_.at("maincpu"), address);
    if (address >= 0x104000 && address <= 0x107ffe)
        return read_word(work_ram_, address - 0x104000);
    if (address >= 0x180000 && address <= 0x183ffe)
        return read_word(sprite_ram_, address - 0x180000);
    return 0;
}

void Tmnt2Board::tmnt2_protection_write_word(uint32_t word_address, uint16_t value)
{
    const uint32_t address = word_address * 2;
    if (address >= 0x104000 && address <= 0x107ffe)
        write_word(work_ram_, address - 0x104000, value, 0xffff);
    else if (address >= 0x180000 && address <= 0x183ffe) {
        const size_t offset = (address - 0x180000) >> 1;
        write_word(sprite_ram_, offset * 2, value, 0xffff);
        if (!(offset & 0x0031)) {
            const size_t chip_offset = ((offset & 0x000e) >> 1) |
                                       ((offset & 0x1fc0) >> 3);
            write_word(sprite_device_ram_, chip_offset * 2, value, 0xffff);
        }
    }
}

void Tmnt2Board::run_tmnt2_protection_blit()
{
    const auto reg = [this](size_t index) {
        return read_word(protection_ram_, index * 2);
    };
    if ((reg(8) & 0xff00) != 0x8200) return;

    const uint32_t source = (uint32_t(reg(0)) | (uint32_t(reg(1) & 0x00ff) << 16)) >> 1;
    const uint32_t destination = (uint32_t(reg(2)) | (uint32_t(reg(3) & 0x00ff) << 16)) >> 1;
    const uint32_t modifier = (uint32_t(reg(4)) | (uint32_t(reg(5) & 0x00ff) << 16)) >> 1;
    const bool zlock = (reg(8) & 0x00ff) == 1;
    std::array<uint16_t, 4> src{};
    std::array<uint16_t, 24> mod{};
    for (unsigned i = 0; i < src.size(); ++i)
        src[i] = tmnt2_protection_read_word(source + i);
    for (unsigned i = 0; i < mod.size(); ++i)
        mod[i] = tmnt2_protection_read_word(modifier + i);

    const uint16_t flags = src[1];
    uint16_t attr1 = (flags >> 2) & 0x3f00;
    uint16_t attr2 = flags & 0x0380;
    const uint16_t base_color = flags & 0x001f;
    const uint16_t modified_color = mod[0x2a / 2] >> 8;
    const uint16_t color = (base_color != 0x0f && modified_color <= 0x1f && !zlock)
        ? modified_color : base_color;
    int x = static_cast<int16_t>(src[2]);
    int y = static_cast<int16_t>(src[3]);
    const uint16_t object_flags = mod[0];
    attr2 |= object_flags & 0x0060;
    const bool keep_aspect = (object_flags & 0x0014) == 0x0014;
    if (object_flags & 0x8000) attr1 |= 0x8000;
    if (keep_aspect) attr1 |= 0x4000;
    if (object_flags & 0x4000) { attr1 ^= 0x1000; x = -x; }

    const int xmod = static_cast<int16_t>(mod[6]);
    const int ymod = static_cast<int16_t>(mod[7]);
    const int zmod = static_cast<int16_t>(mod[8]);
    const int xzoom = mod[0x1c / 2];
    const int yzoom = keep_aspect ? xzoom : mod[0x1e / 2];
    const bool locked = (object_flags & 0x003b) == 0x0020;
    if (!locked) {
        const int z = xzoom - 0x4f00;
        if (z > 0) {
            const int shift = z >> 8;
            x += static_cast<int>(std::pow(shift, 1.891292) * x / 599.250121);
        } else if (z < 0) {
            const int scale = (z >> 3) + (z >> 4) + (z >> 5) + (z >> 6) + xzoom;
            x = scale > 0 ? x * scale / 0x4f00 : 0;
        }
    }
    if (!locked) {
        const int z = yzoom - 0x4f00;
        if (z > 0) {
            const int shift = z >> 8;
            y += static_cast<int>(std::pow(shift, 1.891292) * y / 599.250121);
        } else if (z < 0) {
            const int scale = (z >> 3) + (z >> 4) + (z >> 5) + (z >> 6) + yzoom;
            y = scale > 0 ? y * scale / 0x4f00 : 0;
        }
    }
    if (!zlock) y += zmod;
    x += xmod;
    y += ymod;

    tmnt2_protection_write_word(destination, attr1);
    tmnt2_protection_write_word(destination + 2, src[0]);
    tmnt2_protection_write_word(destination + 4, static_cast<uint16_t>(y));
    tmnt2_protection_write_word(destination + 6, static_cast<uint16_t>(x));
    tmnt2_protection_write_word(destination + 12, attr2 | color);
}

uint16_t Tmnt2Board::main_read(uint32_t address, uint16_t mem_mask)
{
    address &= 0x00fffffe;
    const auto& program = roms_.at("maincpu");
    if (address + 1 < 0x100000) return read_word(program, address);
    if (address >= 0x104000 && address <= 0x107ffe)
        return read_word(work_ram_, address - 0x104000);
    if (address >= 0x140000 && address <= 0x140ffe)
        return read_word(palette_ram_, address - 0x140000);
    if (address >= 0x180000 && address <= 0x183ffe) {
        const size_t offset = (address - 0x180000) >> 1;
        if (offset & 0x0031) return read_word(sprite_ram_, offset * 2);
        const size_t chip_offset = ((offset & 0x000e) >> 1) | ((offset & 0x1fc0) >> 3);
        return read_word(sprite_device_ram_, chip_offset * 2);
    }
    if (address >= 0x1c0000 && address <= 0x1c0006)
        return player_inputs_[(address - 0x1c0000) >> 1];
    if (address == 0x1c0100)
        return static_cast<uint16_t>(0xff00 | (coin_inputs_ & 0x00ff));
    if (address == 0x1c0102) {
        // MAME's EEPROM port also carries NVBLK on bit 3 (active low).
        // The other inputs are inactive at their configured polarities:
        // OBJMPX is active high, while IPL0, unused bits, and service are
        // active low. Match the board's 6 MHz pixel timing (384x264).
        constexpr uint64_t cycles_per_frame = 1024u * 264u;
        constexpr uint64_t vblank_start = 1024u * 240u;
        const bool in_vblank = (main_cycles_ % cycles_per_frame) >= vblank_start;
        const uint16_t vblank_bit = in_vblank ? 0 : 0x0008;
        const uint16_t port_value = static_cast<uint16_t>(0xfff8 | vblank_bit |
            (eeprom_.data_out() ? 0x01 : 0) | (eeprom_.ready() ? 0x02 : 0));
        return port_value;
    }
    if (address >= 0x1c0500 && address <= 0x1c057e)
        return read_word(protection_ram_, address - 0x1c0500);
    if (!tmnt2_ && address == 0x1c0800) return protection_read();
    if (address >= 0x5a0000 && address <= 0x5a001e) {
        // A1 is not connected on the K053244 register window. The MAME
        // handler aligns this offset before fetching its high/low byte pair.
        const unsigned offset = ((address - 0x5a0000) >> 1) & ~1u;
        return static_cast<uint16_t>((uint16_t(sprite_device_read(offset)) << 8) |
                                     sprite_device_read(offset + 1));
    }
    if (address >= 0x5c0600 && address <= 0x5c0602)
        return sample_ports_[2 + ((address - 0x5c0600) >> 1)] | 0xff00;
    if (address >= 0x600000 && address <= 0x603ffe) {
        // MAME's k052109_word_r drives the high byte for UDS and selects
        // the second 0x2000-byte half for LDS (A13 is 68000 UDS).
        const size_t offset = (address - 0x600000) >> 1;
        if (mem_mask & 0xff00) {
            const uint8_t value = tile_rom_read_enabled_
                ? tile_rom_read(offset) : tile_ram_[offset];
            return static_cast<uint16_t>(value << 8);
        }
        const size_t chip_offset = offset + 0x2000;
        const uint8_t value = tile_rom_read_enabled_ ? tile_rom_read(chip_offset)
                                                      : tile_ram_[chip_offset];
        return value;
    }
    return 0xffff;
}

void Tmnt2Board::main_write(uint32_t address, uint16_t value, uint16_t mem_mask)
{
    address &= 0x00fffffe;
    if (address >= 0x104000 && address <= 0x107ffe)
        write_word(work_ram_, address - 0x104000, value, mem_mask);
    else if (address >= 0x140000 && address <= 0x140ffe)
        write_word(palette_ram_, address - 0x140000, value, mem_mask);
    else if (address >= 0x180000 && address <= 0x183ffe) {
        const size_t offset = (address - 0x180000) >> 1;
        write_word(sprite_ram_, offset * 2, value, mem_mask);
        if (!(offset & 0x0031)) {
            const size_t chip_offset = ((offset & 0x000e) >> 1) | ((offset & 0x1fc0) >> 3);
            write_word(sprite_device_ram_, chip_offset * 2, value, mem_mask);
        }
    }
    else if (address == 0x1c0200) {
        if (mem_mask & 0x00ff) {
            eeprom_control_ = static_cast<uint8_t>(value);
            dim_control_ = eeprom_control_ & 0x18;
            sprite_rom_bank_ = (eeprom_control_ >> 5) & 1;
            eeprom_.write_lines((eeprom_control_ & 0x01) != 0,
                                (eeprom_control_ & 0x02) != 0,
                                (eeprom_control_ & 0x04) != 0);
        }
    } else if (address == 0x1c0300) {
        if (mem_mask & 0x00ff) {
            graphics_control_ = static_cast<uint8_t>(value);
            tile_rom_read_enabled_ = (graphics_control_ & 0x08) != 0;
        }
    } else if (address == 0x1c0400) {
        watchdog_touched_ = true;
    } else if (address >= 0x1c0500 && address <= 0x1c057e) {
        write_word(protection_ram_, address - 0x1c0500, value, mem_mask);
    } else if (address >= 0x1c0800 && address <= 0x1c081e && tmnt2_) {
        write_word(protection_ram_, address - 0x1c0800, value, mem_mask);
        if (address == 0x1c0818 && (mem_mask & 0xff00))
            run_tmnt2_protection_blit();
    } else if (address >= 0x5a0000 && address <= 0x5a001e) {
        const unsigned offset = ((address - 0x5a0000) >> 1) & ~1u;
        if (mem_mask & 0xff00) sprite_device_write(offset, static_cast<uint8_t>(value >> 8));
        if (mem_mask & 0x00ff) sprite_device_write(offset + 1, static_cast<uint8_t>(value));
    } else if (address >= 0x5c0600 && address <= 0x5c0602) {
        if (mem_mask & 0x00ff)
            sample_ports_[(address - 0x5c0600) >> 1] = static_cast<uint8_t>(value);
    } else if (address == 0x5c0604) {
        // MAME's ssriders_soundkludge_w asserts the Z80's held IM1 line;
        // this address is not a command latch on this board.
        sound_irq_pending_ = true;
    } else if (address >= 0x5c0700 && address <= 0x5c071e) {
        if (mem_mask & 0x00ff) priority_regs_[(address - 0x5c0700) >> 1] = static_cast<uint8_t>(value) & 0x3f;
    } else if (address >= 0x600000 && address <= 0x603ffe) {
        const size_t offset = (address - 0x600000) >> 1;
        // The board's A13/data buffering means one byte reaches the chip for
        // each 68000 write. Match MAME's handler: UDS wins if both lanes are
        // active; LDS selects the chip's second 0x2000-byte window.
        const size_t chip_offset = offset + ((mem_mask & 0xff00) ? 0 : 0x2000);
        if (mem_mask & 0xff00) tile_ram_[chip_offset] = static_cast<uint8_t>(value >> 8);
        else if (mem_mask & 0x00ff) tile_ram_[chip_offset] = static_cast<uint8_t>(value);
        // These are the K052109's live control registers. Keep the full 0x6000
        // RAM image too: MAME stores register writes there for bus reads.
        if ((mem_mask & 0xff00) && chip_offset == 0x1c00)
            tile_addrmap_ = static_cast<uint8_t>(value >> 8);
        if ((mem_mask & 0xff00) && chip_offset == 0x1c80)
            tile_scroll_control_ = static_cast<uint8_t>(value >> 8);
        if ((mem_mask & 0xff00) && chip_offset == 0x1d00) {
            const uint8_t next = static_cast<uint8_t>(value >> 8);
            if ((tile_irq_control_ & ~next) & 0x04) main_irq4_pending_ = false;
            tile_irq_control_ = next;
        }
        const uint8_t chip_data = (mem_mask & 0xff00)
            ? static_cast<uint8_t>(value >> 8) : static_cast<uint8_t>(value);
        if (chip_offset == 0x1e00 || chip_offset == 0x3e00)
            tile_rom_subbank_ = chip_data;
        if (mem_mask & 0xff00) {
            if (chip_offset == 0x1d80) {
                tile_char_banks_[0] = chip_data & 0x0f;
                tile_char_banks_[1] = (chip_data >> 4) & 0x0f;
            } else if (chip_offset == 0x1f00) {
                tile_char_banks_[2] = chip_data & 0x0f;
                tile_char_banks_[3] = (chip_data >> 4) & 0x0f;
            } else if (chip_offset == 0x1e80) {
                tile_flip_enable_ = chip_data & 7;
            }
        } else if (mem_mask & 0x00ff) {
            if (chip_offset == 0x3d80) {
                tile_char_banks2_[0] = static_cast<uint8_t>(value) & 0x0f;
                tile_char_banks2_[1] = (static_cast<uint8_t>(value) >> 4) & 0x0f;
            } else if (chip_offset == 0x3f00) {
                tile_char_banks2_[2] = static_cast<uint8_t>(value) & 0x0f;
                tile_char_banks2_[3] = (static_cast<uint8_t>(value) >> 4) & 0x0f;
            }
        }
    }
}

uint8_t Tmnt2Board::sound_read(uint16_t address)
{
    if (address < 0xf000) return roms_.at("audiocpu")[address];
    if (address <= 0xf7ff) return audio_ram_[address - 0xf000];
    if (address == 0xf800 || address == 0xf801)
        return ym2151_.read(address - 0xf800);
    if (address >= 0xfa00 && address <= 0xfa2f) {
        const unsigned reg = address - 0xfa00;
        if (reg <= 1) return sample_ports_[reg];
        if (reg == 0x29) {
            uint8_t status = 0;
            for (unsigned i = 0; i < 4; ++i)
                if (sample_voice_keyon_[i]) status |= 1u << i;
            return status;
        }
        if (reg == 0x2e && (sample_mode_ & 1)) {
            // K053260 mode bit 0 exposes voice 0's sample-ROM address
            // counter on this read-only register; MAME's device increments
            // the 16-bit position after each side-effecting read.
            auto& voice = sample_voices_[0];
            const auto& samples = roms_.at("k053260");
            // The K053260 has a 21-bit sample ROM address and a 16-bit
            // per-voice position counter. Keep the programmed start bank
            // when adding the counter; masking the sum to 16 bits makes the
            // sound CPU's startup ROM checksum reread bank zero forever.
            const uint32_t address = voice.start + voice.position;
            const uint8_t value = samples[address % samples.size()];
            voice.position = (voice.position + 1) & 0xffff;
            return value;
        }
        return 0;
    }
    return 0xff;
}

void Tmnt2Board::sound_write(uint16_t address, uint8_t value)
{
    if (address >= 0xf000 && address <= 0xf7ff) {
        audio_ram_[address - 0xf000] = value;
    }
    else if (address == 0xf800 || address == 0xf801) {
        const unsigned port = address - 0xf800;
        ym2151_regs_[port] = value;
        if (!port) {
            ym_address_ = value;
            ym2151_.write(0, value);
        } else ym2151_.write(1, value);
    }
    else if (address >= 0xfa00 && address <= 0xfa2f) {
        const unsigned reg = address - 0xfa00;
        if (reg == 0x02 || reg == 0x03) {
            sample_ports_[reg] = value;
        } else if (reg >= 0x08 && reg <= 0x27) {
            sample_regs_[reg] = value;
            const unsigned voice_index = (reg - 8) / 8;
            const unsigned field = (reg - 8) & 7;
            auto& voice = sample_voices_[voice_index];
            if (field == 0) voice.pitch = (voice.pitch & 0x0f00) | value;
            else if (field == 1) voice.pitch = (voice.pitch & 0x00ff) | ((value << 8) & 0x0f00);
            else if (field == 2) voice.length = (voice.length & 0xff00) | value;
            else if (field == 3) voice.length = (voice.length & 0x00ff) | (uint16_t(value) << 8);
            else if (field == 4) voice.start = (voice.start & 0x1fff00) | value;
            else if (field == 5) voice.start = (voice.start & 0x1f00ff) | (uint32_t(value) << 8);
            else if (field == 6) voice.start = (voice.start & 0x00ffff) | ((uint32_t(value) << 16) & 0x1f0000);
            else if (field == 7) voice.volume = value & 0x7f;
        } else if (reg == 0x28) {
            const uint8_t rising = value & ~sample_keyon_;
            for (unsigned voice = 0; voice < 4; ++voice) {
                auto& state = sample_voices_[voice];
                state.reverse = (value & (1u << (voice + 4))) != 0;
                if (rising & (1u << voice)) {
                    state.position = state.kadpcm ? 1 : 0;
                    state.counter = 0x1000 - 64;
                    state.output = 0;
                    state.playing = true;
                } else if (!(value & (1u << voice))) {
                    state.position = 0;
                    state.output = 0;
                    state.playing = false;
                }
                sample_voice_keyon_[voice] = state.playing;
            }
            sample_keyon_ = value;
            sample_regs_[reg] = value;
        } else if (reg == 0x2a) {
            sample_regs_[reg] = value;
            for (unsigned voice = 0; voice < 4; ++voice) {
                sample_voices_[voice].loop = (value & (1u << voice)) != 0;
                sample_voices_[voice].kadpcm = (value & (1u << (voice + 4))) != 0;
            }
        } else if (reg == 0x2c || reg == 0x2d || reg == 0x2f) {
            sample_regs_[reg] = value;
            if (reg == 0x2c) {
                sample_voices_[0].pan = value & 7;
                sample_voices_[1].pan = (value >> 3) & 7;
            } else if (reg == 0x2d) {
                sample_voices_[2].pan = value & 7;
                sample_voices_[3].pan = (value >> 3) & 7;
            }
            if (reg == 0x2f) sample_mode_ = value;
        }
    }
    else if (address == 0xfc00) {
        // This acknowledges the K053260 SH1 NMI and arms the next edge after
        // MAME's four-Z80-cycle inhibit window; it does not acknowledge IM1.
        sound_nmi_pending_ = false;
        sound_nmi_line_asserted_ = false;
        sound_nmi_armed_ = true;
        sound_nmi_blocked_until_ = sound_cpu_cycles_ + 4;
    }
}

std::vector<int16_t> Tmnt2Board::generate_k053260_audio(size_t sample_count)
{
    static constexpr int pan[8][2] = {
        {0, 0}, {127, 0}, {116, 52}, {104, 73},
        {90, 90}, {73, 104}, {52, 116}, {0, 127}
    };
    static constexpr int8_t adpcm[16] = {
        0, 1, 2, 4, 8, 16, 32, 64, -128, -64, -32, -16, -8, -4, -2, -1
    };
    const auto& samples = roms_.at("k053260");
    std::vector<int16_t> output(sample_count * 2, 0);
    if (!(sample_mode_ & 2) || samples.empty()) return output;
    for (size_t i = 0; i < sample_count; ++i) {
        int mixed[2] = {0, 0};
        for (auto& voice : sample_voices_) {
            if (!voice.playing) continue;
            voice.counter = static_cast<uint16_t>(voice.counter + 64);
            while (voice.counter >= 0x1000) {
                voice.counter = static_cast<uint16_t>(voice.counter - 0x1000 + voice.pitch);
                const uint32_t bytepos = ++voice.position >> (voice.kadpcm ? 1 : 0);
                if (bytepos > voice.length) {
                    if (voice.loop) {
                        voice.position = 0;
                        voice.output = 0;
                    } else {
                        voice.playing = false;
                        break;
                    }
                }
                const int64_t address = int64_t(voice.start) +
                    (voice.reverse ? -int64_t(bytepos) : int64_t(bytepos));
                uint8_t data = 0xff;
                if (address >= 0 && address < int64_t(samples.size()))
                    data = samples[static_cast<size_t>(address)];
                if (voice.kadpcm) {
                    const uint8_t nibble = static_cast<uint8_t>(
                        ((voice.position & 1) ? data >> 4 : data) & 0x0f);
                    voice.output = static_cast<int8_t>(voice.output + adpcm[nibble]);
                } else {
                    voice.output = static_cast<int8_t>(data);
                }
            }
            if (!voice.playing) continue;
            mixed[0] += (int(voice.output) * voice.volume * pan[voice.pan][0]) >> 6;
            mixed[1] += (int(voice.output) * voice.volume * pan[voice.pan][1]) >> 6;
        }
        output[i * 2] = static_cast<int16_t>(std::clamp(mixed[0], -32768, 32767));
        output[i * 2 + 1] = static_cast<int16_t>(std::clamp(mixed[1], -32768, 32767));
    }
    for (unsigned voice = 0; voice < 4; ++voice)
        sample_voice_keyon_[voice] = sample_voices_[voice].playing;
    return output;
}

void Tmnt2Board::advance_sound_to_cycles(uint64_t z80_cycles)
{
    synchronize_sound(z80_cycles);
}

std::vector<int16_t> Tmnt2Board::take_audio_samples()
{
    std::vector<int16_t> result;
    result.swap(audio_queue_);
    return result;
}

void Tmnt2Board::synchronize_sound(uint64_t target_cycles)
{
    constexpr uint64_t z80_clock = 8'000'000;
    constexpr uint64_t sound_chip_clock = 3'579'545;
    if (target_cycles <= sound_cpu_cycles_) return;
    const uint64_t target_chip_cycles = (target_cycles / z80_clock) * sound_chip_clock +
        ((target_cycles % z80_clock) * sound_chip_clock) / z80_clock;
    while (next_k053260_tick_ <= target_chip_cycles) {
        const uint64_t tick_cpu_cycles = (next_k053260_tick_ / sound_chip_clock) * z80_clock +
            ((next_k053260_tick_ % sound_chip_clock) * z80_clock) / sound_chip_clock;
        // The Z80 NMI is wired to K053260 SH1, not TIM2. SH1 is asserted
        // during state 0 of the four-state, 16-chip-clock timer. The sound
        // program must clear/arm it through 0xfc00, as in MAME's
        // sound_arm_nmi_w; TIM2's unrelated 112-edge output is not connected
        // to the Z80 on this board.
        if (k053260_timer_state_ == 0 && sound_nmi_armed_ &&
            !sound_nmi_line_asserted_ && tick_cpu_cycles >= sound_nmi_blocked_until_) {
            sound_nmi_pending_ = true;
            sound_nmi_line_asserted_ = true;
        }
        k053260_timer_state_ = (k053260_timer_state_ + 1) & 3;
        next_k053260_tick_ += 16;
    }
    const uint64_t target_samples = target_chip_cycles / 64;
    const size_t count = target_samples > generated_sound_samples_
        ? static_cast<size_t>(target_samples - generated_sound_samples_) : 0;
    std::vector<int16_t> k053260 = generate_k053260_audio(count);
    for (size_t i = 0; i < count; ++i) {
        const uint64_t sample_clock = (generated_sound_samples_ + i + 1) * 64;
        for (unsigned timer = 0; timer < ym_timer_active_.size(); ++timer) {
            if (ym_timer_active_[timer] && ym_timer_deadlines_[timer] <= sample_clock) {
                ym_timer_active_[timer] = false;
                m_engine->engine_timer_expired(timer);
            }
        }
        sound_cycles_ = sample_clock;
        ymfm::ym2151::output_data fm{};
        ym2151_.generate(&fm, 1);
        fm.clamp16();
        // MAME routes each K053260 channel at 0.75 and the YM2151 channels
        // at unity for TMNT2.
        const int left = fm.data[0] + int(k053260[i * 2]) * 75 / 100;
        const int right = fm.data[1] + int(k053260[i * 2 + 1]) * 75 / 100;
        audio_queue_.push_back(static_cast<int16_t>(std::clamp(left, -32768, 32767)));
        audio_queue_.push_back(static_cast<int16_t>(std::clamp(right, -32768, 32767)));
    }
    generated_sound_samples_ = target_samples;
    sound_cycles_ = target_chip_cycles;
    sound_cpu_cycles_ = target_cycles;
}

void Tmnt2Board::ymfm_set_timer(uint32_t timer, int32_t duration)
{
    if (timer >= ym_timer_active_.size()) return;
    if (duration < 0) {
        ym_timer_active_[timer] = false;
        return;
    }
    ym_timer_active_[timer] = true;
    ym_timer_deadlines_[timer] = sound_cycles_ + static_cast<uint32_t>(duration);
}

void Tmnt2Board::ymfm_set_busy_end(uint32_t clocks)
{
    ym_busy_until_ = sound_cycles_ + clocks;
}

bool Tmnt2Board::ymfm_is_busy()
{
    return sound_cycles_ < ym_busy_until_;
}

void Tmnt2Board::ymfm_update_irq(bool asserted)
{
    ym_irq_ = asserted;
}

void Tmnt2Board::ymfm_external_write(ymfm::access_class, uint32_t, uint8_t)
{
}

void Tmnt2Board::set_input(unsigned port, uint16_t value)
{
    if (port == 0) {
        coin_inputs_ = value;
        return;
    }
    if (port >= 1 && port <= 4) {
        player_inputs_[port - 1] = value;
        return;
    }
    throw std::out_of_range("TMNT2-family input port index");
}

void Tmnt2Board::video_vblank()
{
    if (tile_irq_control_ & 0x04) main_irq4_pending_ = true;
}

uint8_t Tmnt2Board::tile_pixel(unsigned layer, unsigned tile,
                               unsigned x, unsigned y) const
{
    if (layer > 2 || tile >= 0x800 || x >= 8 || y >= 8) return 0;
    const size_t tilemap = layer * 0x800;
    const uint8_t attr = tile_ram_[tilemap + tile];
    const size_t code_ram = 0x2000 + tilemap + tile;
    uint32_t code = tile_ram_[code_ram] + 256u * tile_ram_[code_ram + 0x2000];
    const uint8_t bank_select = (attr >> 2) & 3;
    const uint8_t bank_reg = tile_char_banks_[bank_select];
    uint8_t color = attr;
    if (!(tile_addrmap_ & 0x40))
        color = static_cast<uint8_t>((color & 0xf3) | ((bank_reg & 3) << 2));
    const uint32_t bank = bank_reg >> 2;
    // tmnt2_base_state::tile_callback in MAME.
    code |= (uint32_t(color & 3) << 8) |
            (uint32_t(color & 0x10) << 6) |
            (uint32_t(color & 0x0c) << 9) | (bank << 13);
    if ((tile_flip_enable_ & 4) && (color & 2)) y = 7 - y;
    const auto& chars = roms_.at("k052109");
    const size_t address = (size_t(code) * 32 + size_t(y) * 4) & (chars.size() - 1);
    const unsigned bit = 7 - x;
    // MAME's K052109 decoder visits plane offsets {24,16,8,0} and assigns
    // weights {8,4,2,1}; the first byte in each row is pen bit 3.
    return static_cast<uint8_t>((((chars[address + 3] >> bit) & 1) << 3) |
        (((chars[address + 2] >> bit) & 1) << 2) |
        (((chars[address + 1] >> bit) & 1) << 1) |
        ((chars[address] >> bit) & 1));
}

uint8_t Tmnt2Board::tile_rom_read(size_t offset) const
{
    const auto& chars = roms_.at("k052109");
    if (chars.empty()) return 0xff;
    offset &= 0x1fff;
    uint32_t code = static_cast<uint32_t>(offset >> 5);
    const uint8_t color = tile_rom_subbank_;
    const unsigned bank_index = (color & 0x0c) >> 2;
    unsigned bank = tile_char_banks_[bank_index] >> 2;
    bank |= tile_char_banks2_[bank_index] >> 2;
    code |= (uint32_t(color & 0x03) << 8) |
            (uint32_t(color & 0x10) << 6) |
            (uint32_t(color & 0x0c) << 9) | (bank << 13);
    const size_t address = ((size_t(code) << 5) + (offset & 0x1f)) & (chars.size() - 1);
    return chars[address];
}

uint8_t Tmnt2Board::sprite_device_read(unsigned reg)
{
    if (reg >= sprite_regs_.size()) return 0;
    if (reg == 6) {
        sprite_buffer_ = sprite_device_ram_;
        return 0;
    }
    if (reg == 7) {
        for (size_t i = 0; i < sprite_buffer_.size(); i += 16)
            sprite_buffer_[i] = sprite_buffer_[i + 1] = 0;
        return 0;
    }
    if (reg >= 0x0c) {
        const auto& gfx = roms_.at("k053245");
        const uint32_t bank = uint32_t(sprite_rom_bank_ ? 4 : 0) << 19;
        const uint32_t address = bank |
            (uint32_t(sprite_regs_[11] & 7) << 18) |
            (uint32_t(sprite_regs_[8]) << 10) |
            (uint32_t(sprite_regs_[9]) << 2) |
            ((reg & 3) ^ 1);
        return gfx[address & (gfx.size() - 1)];
    }
    return 0;
}

void Tmnt2Board::sprite_device_write(unsigned reg, uint8_t value)
{
    if (reg >= sprite_regs_.size()) return;
    sprite_regs_[reg] = value;
    if (reg == 6) sprite_buffer_ = sprite_device_ram_;
    else if (reg == 7) {
        for (size_t i = 0; i < sprite_buffer_.size(); i += 16)
            sprite_buffer_[i] = sprite_buffer_[i + 1] = 0;
    }
}

uint8_t Tmnt2Board::priority(unsigned ci) const
{
    return ci < priority_regs_.size() ? priority_regs_[ci] : 0;
}

uint8_t Tmnt2Board::palette_base(unsigned ci) const
{
    if (ci == 0) return static_cast<uint8_t>(32 * (priority_regs_[9] & 3));
    if (ci == 1) return static_cast<uint8_t>(32 * ((priority_regs_[9] >> 2) & 3));
    if (ci == 2) return static_cast<uint8_t>(32 * ((priority_regs_[9] >> 4) & 3));
    if (ci == 3) return static_cast<uint8_t>(16 * (priority_regs_[10] & 7));
    if (ci == 4) return static_cast<uint8_t>(16 * ((priority_regs_[10] >> 3) & 7));
    return 0;
}

Tmnt2Frame Tmnt2Board::render_tile_layers() const
{
    Tmnt2Frame frame;
    frame.width = screen_width_;
    constexpr unsigned height = 224;
    const size_t pixels = size_t(frame.width) * height;
    frame.height = height;
    frame.palette_indices.resize(pixels);
    frame.priorities.assign(pixels, 0);
    frame.shadow_pixels.assign(pixels, 0);

    const unsigned bg_base = palette_base(0);
    std::fill(frame.palette_indices.begin(), frame.palette_indices.end(),
              static_cast<uint16_t>(bg_base * 16));

    struct Layer { unsigned ram_layer; unsigned ci; unsigned priority_rank; int dx; };
    // TMNT2 inherits the Lightning Fighters wiring: FIX uses CI2, B uses CI4,
    // and A uses CI3 (see lgtnfght_state::screen_update_lgtnfght in MAME).
    std::array<Layer, 3> layers{{{0, 2, 1, 96}, {1, 4, 2, 90}, {2, 3, 4, 90}}};
    std::stable_sort(layers.begin(), layers.end(), [this](const Layer& a, const Layer& b) {
        // konami_sortlayers3's swap pass with std::less leaves the layers in
        // descending order. MAME then writes priority codes 1, 2, 4 in this
        // order, so both draw order and bitmap ranks must match.
        return priority(a.ci) > priority(b.ci);
    });

    for (unsigned layer_order = 0; layer_order < layers.size(); ++layer_order) {
        const Layer& layer = layers[layer_order];
        const unsigned tilemap = layer.ram_layer * 0x800;
        const bool scrolling_layer = layer.ram_layer != 0;
        const unsigned scroll_base = layer.ram_layer == 1 ? 0x1800 : 0x3800;
        const unsigned x_base = layer.ram_layer == 1 ? 0x1a00 : 0x3a00;
        const unsigned scroll_mode = scrolling_layer
            ? ((tile_scroll_control_ >> ((layer.ram_layer - 1) * 3)) & 7) : 0;
        constexpr unsigned rows_table[4] = {1, 1, 32, 256};
        const unsigned rows = scrolling_layer ? rows_table[scroll_mode & 3] : 1;
        const bool column_scroll = scrolling_layer && (scroll_mode & 4);
        const unsigned base_xscroll = scrolling_layer
            ? tile_ram_[x_base] | (unsigned(tile_ram_[x_base + 1]) << 8) : 0;
        const unsigned base_yscroll = scrolling_layer ? tile_ram_[scroll_base + 12] : 0;
        const unsigned colors = palette_base(layer.ci);
        for (unsigned y = 0; y < frame.height; ++y) {
            const unsigned base_source_y = (y + 16 + base_yscroll) & 0xff;
            for (unsigned x = 0; x < frame.width; ++x) {
                unsigned source_y = base_source_y;
                unsigned xscroll = base_xscroll;
                if (scrolling_layer && rows > 1) {
                    const unsigned mask = rows == 256 ? 0xff : 0xf8;
                    const unsigned row = (source_y & mask) * 2;
                    xscroll = tile_ram_[x_base + row] |
                              (unsigned(tile_ram_[x_base + row + 1]) << 8);
                }
                unsigned yscroll = base_yscroll;
                unsigned source_x = (x + visible_x_ + xscroll + layer.dx) & 0x1ff;
                if (column_scroll) {
                    // source_x already includes the base X scroll. MAME's
                    // column table is addressed by that final tile column.
                    const unsigned column = (source_x >> 3) & 0x3f;
                    yscroll = tile_ram_[scroll_base + column];
                    source_y = (y + yscroll) & 0xff;
                }
                if (tile_flip_enable_ & 1) {
                    source_x = 0x1ff - source_x;
                    source_y = 0xff - source_y;
                }
                const unsigned tile_x = source_x >> 3;
                const unsigned tile_y = source_y >> 3;
                const unsigned tile = ((tile_y & 31) * 64 + (tile_x & 63));
                const uint8_t pen = tile_pixel(layer.ram_layer, tile,
                                               source_x & 7, source_y & 7);
                if (!pen) continue;
                const size_t out = size_t(y) * frame.width + x;
                const uint8_t attr = tile_ram_[tilemap + tile];
                const uint8_t color = static_cast<uint8_t>(
                    colors + ((attr & 0xe0) >> 5));
                frame.palette_indices[out] = static_cast<uint16_t>(color * 16 + pen);
                // tilemap_t::draw updates the bitmap as
                // (old_priority & priority_mask) | priority.  K052109's
                // default priority mask is 0xff, so overlapping layers
                // accumulate their individual bits here.
                frame.priorities[out] |= static_cast<uint8_t>(1u << layer_order);
            }
        }
    }
    return frame;
}

void Tmnt2Board::draw_sprites(Tmnt2Frame& frame) const
{
    if (frame.width != screen_width_ || frame.height != 224 ||
        frame.palette_indices.size() != size_t(frame.width) * frame.height ||
        frame.priorities.size() != frame.palette_indices.size() ||
        frame.shadow_pixels.size() != frame.palette_indices.size()) return;
    const auto& gfx = roms_.at("k053245");
    std::array<int, 128> list;
    list.fill(-1);
    for (size_t offset = 0; offset < sprite_buffer_.size() / 2; offset += 8) {
        const uint16_t control = read_word(sprite_buffer_, offset * 2);
        if (!(control & 0x8000)) continue;
        const unsigned order = control & 0x7f;
        if (list[order] < 0) list[order] = static_cast<int>(offset);
    }
    std::array<unsigned, 3> layer_ci{2, 4, 3};
    std::stable_sort(layer_ci.begin(), layer_ci.end(), [this](unsigned a, unsigned b) {
        return priority(a) > priority(b);
    });
    const uint8_t layer_pri0 = priority(layer_ci[0]);
    const uint8_t layer_pri1 = priority(layer_ci[1]);
    const uint8_t layer_pri2 = priority(layer_ci[2]);
    const unsigned colorbase = palette_base(1);
    const unsigned flipregs = sprite_regs_[5];
    const int sprite_x_offset = (int(sprite_regs_[0]) << 8) | sprite_regs_[1];
    const int sprite_y_offset = (int(sprite_regs_[2]) << 8) | sprite_regs_[3];

    auto pixel = [&gfx](unsigned code, unsigned x, unsigned y) -> uint8_t {
        constexpr size_t tile_bytes = 128;
        const size_t address = size_t(code) * tile_bytes + (y & 7) * 4 +
                               ((y & 8) ? 64 : 0) + ((x & 8) ? 32 : 0);
        if (address + 3 >= gfx.size()) return 0;
        const unsigned bit = 7 - (x & 7);
        // MAME's K053244 sprite layout uses plane offsets {24,16,8,0},
        // assigned pen weights {8,4,2,1} by drawgfx.cpp.
        return static_cast<uint8_t>((((gfx[address + 3] >> bit) & 1) << 3) |
            (((gfx[address + 2] >> bit) & 1) << 2) |
            (((gfx[address + 1] >> bit) & 1) << 1) |
            ((gfx[address] >> bit) & 1));
    };

    for (int order = 127; order >= 0; --order) {
        const int offset = list[order];
        if (offset < 0) continue;
        const uint16_t word0 = read_word(sprite_buffer_, size_t(offset) * 2);
        uint32_t code = read_word(sprite_buffer_, size_t(offset + 1) * 2);
        code = (code & 0xffe1) | ((code & 0x0010) >> 2) | ((code & 0x0008) << 1) |
               ((code & 0x0004) >> 1) | ((code & 0x0002) << 2);
        const uint16_t word6 = read_word(sprite_buffer_, size_t(offset + 6) * 2);
        unsigned color = word6 & 0xff;
        const bool shadow = (word6 & 0x0080) != 0;
        const unsigned sprite_pri = 0x20 | ((color & 0x60) >> 2);
        unsigned priority_mask = 0;
        if (sprite_pri > layer_pri2) {
            priority_mask = 0xf0;
            if (sprite_pri > layer_pri1) {
                priority_mask |= 0xcc;
                if (sprite_pri > layer_pri0) priority_mask |= 0xaa;
            }
        }
        color = colorbase + (color & 0x1f);
        const unsigned size = (word0 & 0x0f00) >> 8;
        const unsigned width_tiles = 1u << (size & 3);
        const unsigned height_tiles = 1u << ((size >> 2) & 3);
        const unsigned raw_zoom_y = read_word(sprite_buffer_, size_t(offset + 4) * 2);
        if (raw_zoom_y > 0x2000) continue;
        const uint32_t zoom_y = raw_zoom_y ? (0x400000u + raw_zoom_y / 2) / raw_zoom_y : 0x80000u;
        const unsigned raw_zoom_x = (word0 & 0x4000)
            ? raw_zoom_y : read_word(sprite_buffer_, size_t(offset + 5) * 2);
        if (raw_zoom_x > 0x2000) continue;
        const uint32_t zoom_x = raw_zoom_x ? (0x400000u + raw_zoom_x / 2) / raw_zoom_x : 0x80000u;

        int ox = int(read_word(sprite_buffer_, size_t(offset + 3) * 2)) + sprite_x_offset - 96;
        int oy = int(read_word(sprite_buffer_, size_t(offset + 2) * 2));
        bool flip_x = (word0 & 0x1000) != 0;
        bool flip_y = (word0 & 0x2000) != 0;
        const bool mirror_x = (word6 & 0x0100) != 0;
        const bool mirror_y = (word6 & 0x0200) != 0;
        if (flipregs & 1) { ox = 320 - ox; if (!mirror_x) flip_x = !flip_x; }
        if (flipregs & 2) { oy = -oy; if (!mirror_y) flip_y = !flip_y; }
        ox = (ox + 0x5d) & 0x3ff;
        if (ox >= 768) ox -= 1024;
        oy = (-(oy + sprite_y_offset + 0x07)) & 0x3ff;
        if (oy >= 640) oy -= 1024;
        ox -= static_cast<int>((zoom_x * width_tiles) >> 13);
        oy -= static_cast<int>((zoom_y * height_tiles) >> 13);

        for (unsigned ty = 0; ty < height_tiles; ++ty) {
            const int dy0 = oy + static_cast<int>((zoom_y * ty + 0x800) >> 12);
            const int dy1 = oy + static_cast<int>((zoom_y * (ty + 1) + 0x800) >> 12);
            const int dest_h = std::max(0, dy1 - dy0);
            for (unsigned tx = 0; tx < width_tiles; ++tx) {
                const int dx0 = ox + static_cast<int>((zoom_x * tx + 0x800) >> 12);
                const int dx1 = ox + static_cast<int>((zoom_x * (tx + 1) + 0x800) >> 12);
                const int dest_w = std::max(0, dx1 - dx0);
                unsigned tile_code = code;
                bool tile_flip_x = flip_x;
                bool tile_flip_y = flip_y;
                if (mirror_x) {
                    if ((!flip_x) ^ (2 * tx < width_tiles)) {
                        tile_code += width_tiles - tx - 1;
                        tile_flip_x = true;
                    } else {
                        tile_code += tx;
                        tile_flip_x = false;
                    }
                } else tile_code += flip_x ? width_tiles - tx - 1 : tx;
                if (mirror_y) {
                    if ((!flip_y) ^ (2 * ty >= height_tiles)) {
                        tile_code += 8 * (height_tiles - ty - 1);
                        tile_flip_y = true;
                    } else {
                        tile_code += 8 * ty;
                        tile_flip_y = false;
                    }
                } else tile_code += 8 * (flip_y ? height_tiles - ty - 1 : ty);
                tile_code = (tile_code & 0x3f) | (code & ~0x3fu);
                for (int py = 0; py < dest_h; ++py) {
                    const int sy = dy0 + py;
                    if (sy < 16 || sy >= 240) continue;
                    unsigned src_y = static_cast<unsigned>((uint64_t(py) * 16) / std::max(1, dest_h));
                    if (tile_flip_y) src_y = 15 - src_y;
                    for (int px = 0; px < dest_w; ++px) {
                        const int sx = dx0 + px;
                        if (sx < int(visible_x_) || sx >= int(visible_x_ + frame.width)) continue;
                        unsigned src_x = static_cast<unsigned>((uint64_t(px) * 16) / std::max(1, dest_w));
                        if (tile_flip_x) src_x = 15 - src_x;
                        tile_code %= static_cast<unsigned>(gfx.size() / 128);
                        const uint8_t pen = pixel(tile_code, src_x, src_y);
                        if (!pen) continue;
                        const size_t out = size_t(sy - 16) * frame.width + (sx - visible_x_);
                        if (((1u << frame.priorities[out]) & priority_mask) != 0) {
                            continue;
                        }
                        if (shadow && pen == 0x0f) frame.shadow_pixels[out] = 1;
                        else frame.palette_indices[out] = static_cast<uint16_t>(color * 16 + pen);
                    }
                }
            }
        }
    }
}

Tmnt2Frame Tmnt2Board::render_frame() const
{
    Tmnt2Frame frame = render_tile_layers();
    draw_sprites(frame);
    return frame;
}

std::vector<uint8_t> Tmnt2Board::frame_rgb(const Tmnt2Frame& frame) const
{
    const size_t pixels = size_t(frame.width) * frame.height;
    if (frame.palette_indices.size() != pixels)
        throw std::invalid_argument("Konami frame has an invalid pixel count");
    if (frame.shadow_pixels.size() != pixels)
        throw std::invalid_argument("Konami frame has an invalid shadow plane");
    uint8_t dim_palette_mask = 0;
    const uint8_t dim_enable = priority(5);
    if (dim_enable != 0x3e) {
        dim_palette_mask |= 1u << ((palette_base(4) >> 4) & 7);
        if (!(dim_enable & 0x10)) {
            const unsigned sprite_group = palette_base(1) >> 4;
            dim_palette_mask |= 1u << (sprite_group & 7);
            dim_palette_mask |= 1u << ((sprite_group + 1) & 7);
            dim_palette_mask |= 1u << ((palette_base(3) >> 4) & 7);
        }
    }
    const unsigned dim_value = (graphics_control_ & 0x70) >> 4;
    const unsigned dim_factor = dim_value | ((~dim_control_ & 0x10) >> 1);
    const double dim_brightness = 1.0 - (1.0 - 0.6) * dim_factor / 8.0;
    std::vector<uint8_t> rgb(pixels * 3);
    for (size_t i = 0; i < pixels; ++i) {
        const uint16_t index = frame.palette_indices[i] & 0x07ff;
        const uint16_t value = read_word(palette_ram_, size_t(index) * 2);
        const uint8_t r5 = value & 0x1f;
        const uint8_t g5 = (value >> 5) & 0x1f;
        const uint8_t b5 = (value >> 10) & 0x1f;
        double brightness = (dim_palette_mask & (1u << (index >> 8)))
            ? dim_brightness : 1.0;
        if (frame.shadow_pixels[i]) brightness *= 0.6;
        rgb[i * 3] = static_cast<uint8_t>(((r5 << 3) | (r5 >> 2)) * brightness);
        rgb[i * 3 + 1] = static_cast<uint8_t>(((g5 << 3) | (g5 >> 2)) * brightness);
        rgb[i * 3 + 2] = static_cast<uint8_t>(((b5 << 3) | (b5 >> 2)) * brightness);
    }
    return rgb;
}

} // namespace multiarcaderecomp::konami
