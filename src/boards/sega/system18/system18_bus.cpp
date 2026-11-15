#include "system18_bus.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace multiarcaderecomp {

std::array<chips::Mapper3155195::Wiring, 8> System18AddressSpace::mapper_wiring(System18RomBoard board) {
    using Mapper = chips::Mapper3155195;
    using Target = Mapper::Target;
    // These are the mapper regions; the precise per-region ROM board windows
    // (including the split region 5) are supplied by system18_board.cpp.
    return {{
        {Target::Rom, 0x80000, 0xf80000, 0},
        {Target::ExternalDevice, 0x00010, 0xfffff0, 0},
        {Target::ExternalDevice, board == System18RomBoard::Sega5874 ? 0x00010U : 0U, 0xfffff0U, 0},
        {Target::WorkRam, 0x04000, 0xffc000, 0},
        {Target::SpriteRam, 0x00800, 0xfff800, 0},
        {Target::TileRamAndTextRam, 0x11000, 0xfe0000, 0},
        {Target::PaletteRam, 0x01000, 0xfff000, 0},
        {Target::Io, 0x04000, 0xffc000, 0},
    }};
}

System18AddressSpace::System18AddressSpace(std::span<const std::uint8_t> maincpu_rom,
    SegaInputState* inputs, SegaVideoSignals* video_signals, Devices devices, System18RomBoard board)
    : m_board(board), m_maincpu(maincpu_rom.begin(), maincpu_rom.end()), m_inputs(inputs), m_video_signals(video_signals),
      m_devices(std::move(devices)),
      m_mapper(mapper_wiring(board), {
          [this](std::uint32_t address) { const auto result = read16(address); return result ? result.value : 0xffff; },
          [this](std::uint32_t address, std::uint16_t data) { (void)write16(address, data); },
          {}, {}, {}}),
      m_vdp({[this](std::uint32_t address) {
          const auto result = read16(address);
          return result ? result.value : std::uint16_t{0xffff};
      }, {}, {}}),
      m_io({[this](unsigned port) { return read_io_port(port); },
          [this](unsigned port, std::uint8_t data) { write_io_port(port, data); },
          [this](unsigned pin, bool state) {
              if (pin == 1U) {
                  m_display_enabled = state;
                  if (m_video_signals) m_video_signals->display_enable = state;
              } else if (pin == 2U) {
                  if (m_vdp_enabled != state) {
                      m_vdp_enabled = state;
                      record_vdp_signal(VdpSignalEvent::Signal::Enable, static_cast<std::uint8_t>(state));
                  }
              }
          }}) {
    if (m_maincpu.empty()) throw std::invalid_argument("System 18 main CPU ROM is empty");
    // 171-SHADOW (Shadow Dancer) puts the VDP on region 1; 171-5874 (mwalkd)
    // puts it on region 2. Both board layouts mirror its 16-byte register
    // window across C00000-DFFFFF. Region 3 mirrors work RAM into the stack.
    // Region 0 needs 512 KiB; region 5 needs 128 KiB so tile/text do not alias.
    std::array<std::uint8_t, 16> mapping{};
    mapping[0] = 0x02; mapping[1] = 0x00; // ROM at 0x000000.
    if (m_board == System18RomBoard::ShadowDancer171) {
        mapping[2] = 0x03; mapping[3] = 0xc0; // VDP region 1 at 0xc00000-0xdfffff.
    } else {
        mapping[4] = 0x03; mapping[5] = 0xc0; // VDP region 2 at 0xc00000-0xdfffff.
    }
    mapping[6] = 0x00; mapping[7] = 0xff; // Work RAM at top-of-bus mirror.
    mapping[8] = 0x00; mapping[9] = 0x10; // Sprite RAM at 0x100000.
    mapping[10] = 0x01; mapping[11] = 0x40; // Tile/text RAM at 0x400000.
    mapping[12] = 0x00; mapping[13] = 0x20; // Palette RAM at 0x200000.
    mapping[14] = 0x00; mapping[15] = 0xe4; // 315-5296 I/O at 0xe40000.
    m_mapper.configure_explicit(mapping);
}

void System18AddressSpace::set_vdp_cycle_provider(std::function<std::uint64_t()> cycle_provider,
    std::uint64_t cpu_clock_hz, std::uint32_t refresh_num, std::uint32_t refresh_den) {
    if (!cycle_provider || cpu_clock_hz == 0 || refresh_num == 0 || refresh_den == 0)
        throw std::invalid_argument("System 18 VDP timing requires a cycle source and nonzero clock/refresh ratio");
    m_vdp_cycle_provider = std::move(cycle_provider);
    m_vdp_cpu_clock_hz = cpu_clock_hz;
    m_vdp_refresh_num = refresh_num;
    m_vdp_refresh_den = refresh_den;
}

void System18AddressSpace::update_vdp_beam() const {
    if (!m_vdp_cycle_provider || m_vdp_cpu_clock_hz == 0 || m_vdp_refresh_num == 0 || m_vdp_refresh_den == 0)
        return;
    constexpr std::uint64_t total_lines = 262;
    constexpr std::uint64_t hcounter_max = 480;
    const std::uint64_t denominator = m_vdp_cpu_clock_hz * m_vdp_refresh_den;
    const std::uint64_t cycles = m_vdp_cycle_provider();
    const std::uint64_t line_phase = cycles * m_vdp_refresh_num * total_lines;
    const std::uint64_t line_index = line_phase / denominator;
    const std::uint64_t within_line = line_phase % denominator;
    const auto hpos = static_cast<unsigned>((within_line * hcounter_max) / denominator);
    m_vdp.set_beam_position(static_cast<unsigned>(line_index % total_lines), hpos);
}

System18AddressSpace::Resolved System18AddressSpace::resolve(std::uint32_t address) const {
    address &= 0x00ffffffU;
    const auto configs = m_mapper.mappings();
    const auto windows = system18_mapper_windows(m_board);
    // Resolve the selected ROM-board wiring through the mapper masks.
    for (auto config = configs.rbegin(); config != configs.rend(); ++config) {
        for (const auto& window : windows) {
            if (window.mapper_region != config->region) continue;
            const std::uint32_t start = config->start + (window.offset & config->size_mask);
            const std::uint32_t end = start + std::min(window.length - 1U, config->size_mask);
            const std::uint32_t canonical = address & ~config->mirror & 0x00ffffffU;
            if (canonical >= start && canonical <= end)
                return {&window, canonical - start};
        }
    }
    return {};
}

std::uint16_t System18AddressSpace::read_window(const System18MapperWindow& window, std::uint32_t offset) const {
    const auto read_ram = [offset](const std::vector<std::uint8_t>& ram) {
        if (offset + 1 >= ram.size()) return std::uint16_t{0xffff};
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(ram[offset]) << 8) | ram[offset + 1]);
    };
    switch (window.target) {
    case System18Target::MainRom:
        if (offset + 1 >= m_maincpu.size()) return 0xffff;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(m_maincpu[offset]) << 8) | m_maincpu[offset + 1]);
    case System18Target::Vdp:
        update_vdp_beam();
        if (m_devices.vdp_read) {
            const auto value = m_devices.vdp_read(offset & ~1U);
            if (value) return *value;
        }
        return m_vdp.read(offset);
    case System18Target::WorkRam: return read_ram(m_workram);
    case System18Target::SpriteRam: return read_ram(m_sprites);
    case System18Target::TileRam: return read_ram(m_tileram);
    case System18Target::TextRam: return read_ram(m_textram);
    case System18Target::PaletteRam: return read_ram(m_palette);
    case System18Target::Io:
        if (m_devices.io_read) {
            const auto value = m_devices.io_read(offset & ~1U);
            if (value) return *value;
        }
        return read_io(offset & ~1U);
    }
    return 0xffff;
}

void System18AddressSpace::write_window(const System18MapperWindow& window, std::uint32_t offset,
    std::uint16_t data, std::uint8_t lanes) {
    const auto write_ram = [offset, data, lanes](std::vector<std::uint8_t>& ram) {
        if (offset + 1 >= ram.size()) return;
        if (lanes & 2U) ram[offset] = static_cast<std::uint8_t>(data >> 8);
        if (lanes & 1U) ram[offset + 1] = static_cast<std::uint8_t>(data);
    };
    switch (window.target) {
    case System18Target::MainRom: break;
    case System18Target::Vdp:
        update_vdp_beam();
        if (m_devices.vdp_write) (void)m_devices.vdp_write(offset & ~1U, data, lanes);
        else m_vdp.write(offset, data, lanes);
        break;
    case System18Target::WorkRam: write_ram(m_workram); break;
    case System18Target::SpriteRam: write_ram(m_sprites); break;
    case System18Target::TileRam: write_ram(m_tileram); break;
    case System18Target::TextRam: write_ram(m_textram); break;
    case System18Target::PaletteRam: write_ram(m_palette); break;
    case System18Target::Io:
        if (m_devices.io_write) (void)m_devices.io_write(offset & ~1U, data, lanes);
        else write_io(offset & ~1U, data, lanes);
        break;
    }
}

SegaByteRead System18AddressSpace::read8(std::uint32_t address) const {
    const auto resolved = resolve(address);
    if (!resolved.window) {
        if (m_mapper_access_callback) m_mapper_access_callback();
        const auto word = static_cast<std::uint16_t>(0xff00U | const_cast<chips::Mapper3155195&>(m_mapper).read((address >> 1) & 0x1fU));
        return {SegaBusFault::None, static_cast<std::uint8_t>((address & 1U) ? word : word >> 8)};
    }
    const auto word = read_window(*resolved.window, resolved.offset & ~1U);
    return {SegaBusFault::None, static_cast<std::uint8_t>((resolved.offset & 1U) ? word : word >> 8)};
}

std::optional<std::uint8_t> System18AddressSpace::peek8(std::uint32_t address) const {
    const auto resolved = resolve(address);
    if (!resolved.window) return std::nullopt;
    switch (resolved.window->target) {
    case System18Target::MainRom:
        if (resolved.offset >= m_maincpu.size()) return std::nullopt;
        return m_maincpu[resolved.offset];
    case System18Target::WorkRam: return m_workram[resolved.offset];
    case System18Target::SpriteRam: return m_sprites[resolved.offset];
    case System18Target::TileRam: return m_tileram[resolved.offset];
    case System18Target::TextRam: return m_textram[resolved.offset];
    case System18Target::PaletteRam: return m_palette[resolved.offset];
    case System18Target::Vdp:
    case System18Target::Io: return std::nullopt;
    }
    return std::nullopt;
}

SegaWordRead System18AddressSpace::read16(std::uint32_t address) const {
    if (address & 1U) return {SegaBusFault::AddressError, 0xffff};
    const auto resolved = resolve(address);
    if (!resolved.window) {
        if (m_mapper_access_callback) m_mapper_access_callback();
        const auto low = const_cast<chips::Mapper3155195&>(m_mapper).read((address >> 1) & 0x1fU);
        return {SegaBusFault::None, static_cast<std::uint16_t>(0xff00U | low)};
    }
    return {SegaBusFault::None, read_window(*resolved.window, resolved.offset & ~1U)};
}

SegaBusFault System18AddressSpace::write8(std::uint32_t address, std::uint8_t value) {
    const auto resolved = resolve(address);
    if (!resolved.window) {
        if (address & 1U) {
            if (m_mapper_access_callback) m_mapper_access_callback();
            m_mapper.write((address >> 1) & 0x1fU, value);
        }
        return SegaBusFault::None;
    }
    const bool high_lane = (resolved.offset & 1U) == 0;
    const auto data = high_lane ? static_cast<std::uint16_t>(value << 8) : value;
    write_window(*resolved.window, resolved.offset & ~1U, data, high_lane ? 2U : 1U);
    return SegaBusFault::None;
}

SegaBusFault System18AddressSpace::write16(std::uint32_t address, std::uint16_t value) {
    if (address & 1U) return SegaBusFault::AddressError;
    const auto resolved = resolve(address);
    if (!resolved.window) {
        if (m_mapper_access_callback) m_mapper_access_callback();
        m_mapper.write((address >> 1) & 0x1fU, static_cast<std::uint8_t>(value));
    } else write_window(*resolved.window, resolved.offset & ~1U, value, 3U);
    return SegaBusFault::None;
}

std::span<const SegaMemoryRegionInfo> System18AddressSpace::regions() const {
    m_region_info.clear();
    for (const auto& map : m_mapper.mappings()) {
        for (const auto& window : system18_mapper_windows(m_board)) {
            if (window.mapper_region != map.region) continue;
            const auto start = map.start + (window.offset & map.size_mask);
            m_region_info.push_back({"system18-region-" + std::to_string(map.region) + "-" + std::to_string(static_cast<unsigned>(window.target)),
                start, std::min(window.length, map.size_mask + 1U), window.mirror & map.size_mask,
                window.target != System18Target::MainRom,
                window.target == System18Target::Vdp || window.target == System18Target::Io});
        }
    }
    return m_region_info;
}

std::span<const std::uint8_t> System18AddressSpace::memory(std::string_view name) const {
    if (name == "maincpu") return m_maincpu;
    if (name == "workram" || name == "nvram") return m_workram;
    if (name == "sprites") return m_sprites;
    if (name == "tileram") return m_tileram;
    if (name == "textram") return m_textram;
    if (name == "paletteram") return m_palette;
    return {};
}

std::span<std::uint8_t> System18AddressSpace::mutable_memory(std::string_view name) {
    if (name == "workram" || name == "nvram") return m_workram;
    if (name == "sprites") return m_sprites;
    if (name == "tileram") return m_tileram;
    if (name == "textram") return m_textram;
    if (name == "paletteram") return m_palette;
    return {};
}

std::uint16_t System18AddressSpace::read_io(std::uint32_t byte_offset) const {
    const unsigned word_offset = (byte_offset >> 1) & 0x1fffU;
    switch (word_offset & 0x1800U) {
    case 0x0000U:
    case 0x0800U:
        return static_cast<std::uint16_t>(0xff00U | m_io.read(word_offset));
    case 0x1000U:
        // The board's two special-port inputs are not connected on Shadow Dancer.
        return 0xffffU;
    default:
        return 0xffffU;
    }
}

void System18AddressSpace::write_io(std::uint32_t byte_offset, std::uint16_t data, std::uint8_t lanes) {
    const unsigned word_offset = (byte_offset >> 1) & 0x1fffU;
    switch (word_offset & 0x1800U) {
    case 0x0000U:
    case 0x0800U:
        if (lanes & 1U) m_io.write(word_offset, static_cast<std::uint8_t>(data));
        break;
    case 0x1000U:
        if (lanes & 1U) {
            const auto mixing = static_cast<std::uint8_t>(data);
            if (mixing != m_vdp_mixing) {
                m_vdp_mixing = mixing;
                record_vdp_signal(VdpSignalEvent::Signal::Mixing, mixing);
            }
        }
        break;
    default:
        break;
    }
}

void System18AddressSpace::record_vdp_signal(VdpSignalEvent::Signal signal, std::uint8_t value) {
    const std::uint64_t cycles = m_vdp_cycle_provider ? m_vdp_cycle_provider() : 0;
    m_vdp_signal_events.push_back({signal, value, cycles});
}

std::uint8_t System18AddressSpace::read_io_port(unsigned port) const {
    if (!m_inputs) return 0xffU;
    switch (port & 7U) {
    case 0:
        ++m_inputs->p1_reads;
        m_inputs->p1_pressed_reads += m_inputs->p1 != 0xff;
        return m_inputs->p1;
    case 1:
        ++m_inputs->p2_reads;
        m_inputs->p2_pressed_reads += m_inputs->p2 != 0xff;
        return m_inputs->p2;
    case 2: return 0xffU;
    case 4:
        ++m_inputs->service_reads;
        m_inputs->service_pressed_reads += m_inputs->service != 0xff;
        return m_inputs->service;
    case 5:
        ++m_inputs->dsw1_reads;
        return m_inputs->dsw1;
    case 6:
        ++m_inputs->dsw2_reads;
        return m_inputs->dsw2;
    default: return 0xffU;
    }
}

void System18AddressSpace::write_io_port(unsigned port, std::uint8_t data) {
    switch (port & 7U) {
    case 3:
        m_grayscale_enabled = (data & 0x40U) == 0;
        if (m_video_signals) m_video_signals->flip = (data & 0x20U) != 0;
        break;
    case 7:
        for (unsigned bank = 0; bank < 4; ++bank) {
            m_tile_banks[bank] = static_cast<std::uint8_t>((data & 0x0fU) * 4U + bank);
            m_tile_banks[4U + bank] = static_cast<std::uint8_t>(((data >> 4) & 0x0fU) * 4U + bank);
        }
        break;
    default:
        break;
    }
}

void System18AddressSpace::load_ram(std::string_view name, std::span<const std::uint8_t> bytes) {
    auto target = mutable_memory(name);
    if (target.empty() || target.size() != bytes.size())
        throw std::invalid_argument("unknown or mismatched System 18 RAM region: " + std::string(name));
    std::copy(bytes.begin(), bytes.end(), target.begin());
}

} // namespace multiarcaderecomp
