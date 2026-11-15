#include "rom_mapper.hpp"

#include <stdexcept>
#include <utility>

namespace mamerecomp {

std::array<chips::Mapper3155195::Wiring, 8> system16b_5797_mapper_wiring(std::uint32_t workram_size) {
    using Target = chips::Mapper3155195::Target;
    return {{
        {Target::Rom, 0x80000, 0xf80000, 0},
        {Target::ExternalDevice, 0x04000, 0xffc000, 0},
        {Target::ExternalDevice, 0x10000, 0xff0000, 0},
        {Target::WorkRam, workram_size, ~(workram_size - 1U), 0},
        {Target::SpriteRam, 0x00800, 0xfff800, 0},
        {Target::TileRamAndTextRam, 0x11000, 0xfe0000, 0},
        {Target::PaletteRam, 0x01000, 0xfff000, 0},
        {Target::Io, 0x04000, 0xffc000, 0},
    }};
}

std::array<chips::Mapper3155195::Wiring, 8> system16b_5358_mapper_wiring(std::uint32_t workram_size) {
    using Target = chips::Mapper3155195::Target;
    return {{
        {Target::Rom, 0x20000, 0xfe0000, 0x00000},
        {Target::Rom, 0x20000, 0xfe0000, 0x20000},
        {Target::Rom, 0x20000, 0xfe0000, 0x40000},
        {Target::WorkRam, workram_size, ~(workram_size - 1U), 0},
        {Target::SpriteRam, 0x00800, 0xfff800, 0},
        {Target::TileRamAndTextRam, 0x11000, 0xfe0000, 0},
        {Target::PaletteRam, 0x01000, 0xfff000, 0},
        {Target::Io, 0x04000, 0xffc000, 0},
    }};
}

std::array<chips::Mapper3155195::Wiring, 8> system16b_5521_mapper_wiring(std::uint32_t workram_size) {
    using Target = chips::Mapper3155195::Target;
    // MAME's generic 5704 mapping used by goldnaxejd: two fixed 256 KiB ROM
    // windows, a 64 KiB board handler for region 2, then the shared RAM/I/O.
    return {{
        {Target::Rom, 0x40000, 0xfc0000, 0x00000},
        {Target::Rom, 0x40000, 0xfc0000, 0x40000},
        {Target::ExternalDevice, 0x10000, 0xff0000, 0},
        {Target::WorkRam, workram_size, ~(workram_size - 1U), 0},
        {Target::SpriteRam, 0x00800, 0xfff800, 0},
        {Target::TileRamAndTextRam, 0x11000, 0xfe0000, 0},
        {Target::PaletteRam, 0x01000, 0xfff000, 0},
        {Target::Io, 0x04000, 0xffc000, 0},
    }};
}

System16BAddressSpace::System16BAddressSpace(std::span<const std::uint8_t> maincpu_rom,
    SegaVideoSignals* video_signals, SegaInputState* inputs, System16BBoard board)
    : m_maincpu(maincpu_rom.begin(), maincpu_rom.end()), m_video_signals(video_signals), m_inputs(inputs),
      m_board(board),
      m_mapper(board == System16BBoard::Sega5358 ? system16b_5358_mapper_wiring()
          : board == System16BBoard::Sega5521 ? system16b_5521_mapper_wiring()
          : system16b_5797_mapper_wiring(), {
          [this](std::uint32_t address) { const auto result = read16(address); return result ? result.value : 0xffff; },
          [this](std::uint32_t address, std::uint16_t data) { (void)write16(address, data); },
          {}, {}, {}}) {
    if (board == System16BBoard::Sega5358) {
        // MAME's init_generic path uses the wired 5358 bank list; absent
        // physical banks must be skipped by the 315-5196 sprite decoder.
        m_sprite_banks = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 3,
            0xff, 0xff, 0xff, 2, 0xff, 1, 0, 0xff};
    } else {
        for (std::size_t i = 0; i < m_sprite_banks.size(); ++i)
            m_sprite_banks[i] = static_cast<std::uint8_t>(i);
    }
}

System16BAddressSpace::Resolved System16BAddressSpace::resolve(std::uint32_t address) const {
    address &= 0x00ffffffU;
    const auto mappings = m_mapper.mappings();
    // MAME installs mapper regions from 7 down to 0; on overlapping windows,
    // the later lower-numbered mapping takes precedence.
    for (auto it = mappings.rbegin(); it != mappings.rend(); ++it) {
        const std::uint32_t canonical = address & ~it->mirror & 0x00ffffffU;
        if (canonical >= it->start && canonical <= it->end)
            return {&*it, it->target_offset + canonical - it->start};
    }
    return {nullptr, address};
}

std::uint16_t System16BAddressSpace::read_open_bus() const {
    const std::uint32_t pc = m_open_bus_pc_provider ? m_open_bus_pc_provider() : 0xffffffffU;
    if (pc >= m_maincpu.size() || static_cast<std::uint64_t>(pc) + 1U >= m_maincpu.size()) return 0xffffU;
    return static_cast<std::uint16_t>((m_maincpu[pc] << 8) | m_maincpu[pc + 1U]);
}

std::uint16_t System16BAddressSpace::read_io(std::uint32_t offset) const {
    const unsigned word_offset = (offset >> 1) & 0x1fffU;
    switch (word_offset & 0x1800U) {
    case 0x0800U:
        if (!m_inputs) return 0x00ff;
        switch (word_offset & 3U) {
        case 0:
            ++m_inputs->service_reads;
            if (m_inputs->service != 0xff) ++m_inputs->service_pressed_reads;
            return m_inputs->service;
        case 1:
            ++m_inputs->p1_reads;
            if (m_inputs->p1 != 0xff) ++m_inputs->p1_pressed_reads;
            return m_inputs->p1;
        case 2: return 0x00ff;
        default:
            ++m_inputs->p2_reads;
            if (m_inputs->p2 != 0xff) ++m_inputs->p2_pressed_reads;
            return m_inputs->p2;
        }
    case 0x1000U:
        if (!m_inputs) return 0x00ff;
        if (word_offset & 1U) {
            ++m_inputs->dsw1_reads;
            return m_inputs->dsw1;
        }
        ++m_inputs->dsw2_reads;
        return m_inputs->dsw2;
    default:
        return 0xffff;
    }
}

void System16BAddressSpace::write_io(std::uint32_t offset, std::uint16_t data, std::uint8_t lanes) {
    const unsigned word_offset = (offset >> 1) & 0x1fffU;
    if ((word_offset & 0x1800U) != 0 || !m_video_signals || !(lanes & 1U)) return;
    m_video_signals->flip = (data & 0x40U) != 0;
    m_video_signals->display_enable = (data & 0x20U) != 0;
}

std::uint16_t System16BAddressSpace::read_romboard(std::uint32_t offset, unsigned region) const {
    const unsigned word_offset = (offset >> 1) & 0x1fffU;
    if (m_board == System16BBoard::Sega5521 && region == 2) return read_open_bus();
    if (region == 1) {
        switch (word_offset & 0x1800U) {
        case 0x0000U: return m_multiplier.read(word_offset);
        case 0x0800U: return m_compare_timer_1.read(word_offset);
        default: return 0xffff;
        }
    }
    return m_compare_timer_2.read(word_offset);
}

void System16BAddressSpace::write_romboard(std::uint32_t offset, unsigned region,
    std::uint16_t data, std::uint8_t lanes) {
    const unsigned word_offset = (offset >> 1) & 0x1fffU;
    if (m_board == System16BBoard::Sega5521 && region == 2) {
        if (lanes & 1U) {
            m_tile_banks[word_offset & 1U] = static_cast<std::uint8_t>(data & 7U);
            ++m_video_write_stats.tile_bank;
        }
        return;
    }
    if (region == 1) {
        switch (word_offset & 0x1800U) {
        case 0x0000U: m_multiplier.write(word_offset, data, lanes); break;
        case 0x0800U: m_compare_timer_1.write(word_offset, data, lanes); break;
        case 0x1000U:
            if (lanes & 1U) {
                m_tile_banks[word_offset & 1U] = static_cast<std::uint8_t>(data & 7U);
                ++m_video_write_stats.tile_bank;
            }
            break;
        }
    } else {
        m_compare_timer_2.write(word_offset, data, lanes);
    }
}

std::uint16_t System16BAddressSpace::read_target(const chips::Mapper3155195::Mapping& mapping,
    std::uint32_t offset) const {
    using Target = chips::Mapper3155195::Target;
    const auto read_mem = [&](std::span<const std::uint8_t> bytes, std::uint32_t at) {
        if (at + 1 >= bytes.size()) return std::uint16_t{0xffff};
        return static_cast<std::uint16_t>((bytes[at] << 8) | bytes[at + 1]);
    };
    switch (mapping.target) {
    case Target::Rom: return read_mem(m_maincpu, offset);
    case Target::PaletteRam: return read_mem(m_palette, offset);
    case Target::TileRamAndTextRam:
        return offset < 0x10000U ? read_mem(m_tileram, offset) : read_mem(m_textram, offset - 0x10000U);
    case Target::SpriteRam: return read_mem(m_sprites, offset);
    case Target::WorkRam: return read_mem(m_workram, offset);
    case Target::Io: return read_io(offset);
    case Target::ExternalDevice: return read_romboard(offset, mapping.region);
    }
    return 0xffff;
}

void System16BAddressSpace::write_target(const chips::Mapper3155195::Mapping& mapping,
    std::uint32_t offset, std::uint16_t data, std::uint8_t lanes) {
    using Target = chips::Mapper3155195::Target;
    auto write_mem = [&](std::span<std::uint8_t> bytes, std::uint32_t at) {
        if ((lanes & 2U) && at < bytes.size()) bytes[at] = static_cast<std::uint8_t>(data >> 8);
        if ((lanes & 1U) && at + 1 < bytes.size()) bytes[at + 1] = static_cast<std::uint8_t>(data);
    };
    switch (mapping.target) {
    case Target::Rom: break; // The mapper unmaps ROM writes.
    case Target::PaletteRam:
        write_mem(m_palette, offset);
        ++m_video_write_stats.palette;
        break;
    case Target::TileRamAndTextRam:
        if (offset < 0x10000U) {
            write_mem(m_tileram, offset);
            ++m_video_write_stats.tile;
        } else {
            write_mem(m_textram, offset - 0x10000U);
            ++m_video_write_stats.text;
        }
        break;
    case Target::SpriteRam:
        write_mem(m_sprites, offset);
        ++m_video_write_stats.sprite;
        break;
    case Target::WorkRam: write_mem(m_workram, offset); break;
    case Target::Io: write_io(offset, data, lanes); break;
    case Target::ExternalDevice: write_romboard(offset, mapping.region, data, lanes); break;
    }
}

SegaByteRead System16BAddressSpace::read8(std::uint32_t address) const {
    const auto resolved = resolve(address);
    if (resolved.mapping) {
        const auto word = read_target(*resolved.mapping, resolved.offset & ~1U);
        return {SegaBusFault::None, static_cast<std::uint8_t>((resolved.offset & 1U) ? word : word >> 8)};
    }
    if (m_mapper_access_callback) m_mapper_access_callback();
    const auto offset = static_cast<std::uint8_t>((address >> 1) & 0x1fU);
    if (address & 1U) {
        const auto value = offset <= 3U
            ? const_cast<chips::Mapper3155195&>(m_mapper).read(offset)
            : static_cast<std::uint8_t>(read_open_bus());
        return {SegaBusFault::None, value};
    }
    return {SegaBusFault::None, static_cast<std::uint8_t>(read_open_bus() >> 8)};
}

std::optional<std::uint8_t> System16BAddressSpace::peek8(std::uint32_t address) const {
    const auto resolved = resolve(address);
    if (!resolved.mapping) return std::nullopt;
    const auto word = read_target(*resolved.mapping, resolved.offset & ~1U);
    return static_cast<std::uint8_t>((resolved.offset & 1U) ? word : word >> 8);
}

SegaWordRead System16BAddressSpace::read16(std::uint32_t address) const {
    if (address & 1U) return {SegaBusFault::AddressError, 0xffff};
    const auto resolved = resolve(address);
    if (resolved.mapping) return {SegaBusFault::None, read_target(*resolved.mapping, resolved.offset & ~1U)};
    if (m_mapper_access_callback) m_mapper_access_callback();
    const auto offset = static_cast<std::uint8_t>((address >> 1) & 0x1fU);
    if (offset <= 3U) {
        const auto low = const_cast<chips::Mapper3155195&>(m_mapper).read(offset);
        return {SegaBusFault::None, static_cast<std::uint16_t>((read_open_bus() & 0xff00U) | low)};
    }
    return {SegaBusFault::None, read_open_bus()};
}

SegaBusFault System16BAddressSpace::write8(std::uint32_t address, std::uint8_t value) {
    const auto resolved = resolve(address);
    if (resolved.mapping) {
        const bool high = (resolved.offset & 1U) == 0;
        const auto data = high ? static_cast<std::uint16_t>(value << 8) : value;
        write_target(*resolved.mapping, resolved.offset & ~1U, data, high ? 2U : 1U);
    } else if (address & 1U) {
        if (m_mapper_access_callback) m_mapper_access_callback();
        m_mapper.write((address >> 1) & 0x1fU, value);
    }
    return SegaBusFault::None;
}

SegaBusFault System16BAddressSpace::write16(std::uint32_t address, std::uint16_t value) {
    if (address & 1U) return SegaBusFault::AddressError;
    const auto resolved = resolve(address);
    if (resolved.mapping) write_target(*resolved.mapping, resolved.offset & ~1U, value, 3U);
    else {
        if (m_mapper_access_callback) m_mapper_access_callback();
        m_mapper.write((address >> 1) & 0x1fU, static_cast<std::uint8_t>(value));
    }
    return SegaBusFault::None;
}

std::span<const SegaMemoryRegionInfo> System16BAddressSpace::regions() const {
    m_region_info.clear();
    const auto mappings = m_mapper.mappings();
    for (const auto& map : mappings) {
        m_region_info.push_back({"3155195-region-" + std::to_string(map.region), map.start,
            map.end - map.start + 1U, map.mirror, map.target != chips::Mapper3155195::Target::Rom,
            map.target == chips::Mapper3155195::Target::Io || map.target == chips::Mapper3155195::Target::ExternalDevice});
    }
    return m_region_info;
}

std::span<const std::uint8_t> System16BAddressSpace::memory(std::string_view name) const {
    if (name == "maincpu") return m_maincpu;
    if (name == "paletteram") return m_palette;
    if (name == "tileram") return m_tileram;
    if (name == "textram") return m_textram;
    if (name == "sprites") return m_sprites;
    if (name == "workram") return m_workram;
    return {};
}

std::span<std::uint8_t> System16BAddressSpace::mutable_memory(std::string_view name) {
    if (name == "paletteram") return m_palette;
    if (name == "tileram") return m_tileram;
    if (name == "textram") return m_textram;
    if (name == "sprites") return m_sprites;
    if (name == "workram") return m_workram;
    return {};
}

void System16BAddressSpace::load_ram(std::string_view name, std::span<const std::uint8_t> bytes) {
    auto target = mutable_memory(name);
    if (target.empty() || target.size() != bytes.size())
        throw std::invalid_argument("unknown or mismatched System 16B RAM region: " + std::string(name));
    std::copy(bytes.begin(), bytes.end(), target.begin());
}

} // namespace mamerecomp
