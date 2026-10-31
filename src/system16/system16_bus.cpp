#include "system16_bus.hpp"
#include "system16a_sound.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace mamerecomp {
namespace {

constexpr std::uint32_t kAddressMask = 0x00ffffff;

void validate_region(std::uint32_t start, std::uint32_t size) {
    if (size == 0 || start > kAddressMask || static_cast<std::uint64_t>(start) + size > kAddressMask + 1ULL) {
        throw std::invalid_argument("invalid System 16 address region");
    }
}

} // namespace

void System16AddressSpace::add_region(Region region) {
    const std::uint64_t new_end = static_cast<std::uint64_t>(region.info.start) + region.info.size;
    if (std::any_of(m_regions.begin(), m_regions.end(), [&](const Region& current) {
            const std::uint64_t current_end = static_cast<std::uint64_t>(current.info.start) + current.info.size;
            return region.info.name == current.info.name
                || (region.info.start < current_end && current.info.start < new_end);
        })) {
        throw std::invalid_argument("duplicate or overlapping System 16 base region: " + region.info.name);
    }
    m_region_info.push_back(region.info);
    m_regions.push_back(std::move(region));
}

void System16AddressSpace::map_rom(std::string name, std::uint32_t start, std::uint32_t mirror_mask,
    std::span<const std::uint8_t> bytes) {
    validate_region(start, static_cast<std::uint32_t>(bytes.size()));
    auto memory = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    MemoryRegionInfo info{name, start, static_cast<std::uint32_t>(bytes.size()), mirror_mask & kAddressMask, false, false};
    add_region(Region{std::move(info), std::move(memory), {}, {}});
}

void System16AddressSpace::map_ram(std::string name, std::uint32_t start, std::uint32_t size,
    std::uint32_t mirror_mask, std::uint8_t fill) {
    validate_region(start, size);
    auto memory = std::make_shared<std::vector<std::uint8_t>>(size, fill);
    MemoryRegionInfo info{name, start, size, mirror_mask & kAddressMask, true, false};
    add_region(Region{std::move(info), std::move(memory), {}, {}});
}

void System16AddressSpace::map_device(std::string name, std::uint32_t start, std::uint32_t size,
    std::uint32_t mirror_mask, DeviceRead16 read, DeviceWrite16 write) {
    validate_region(start, size);
    MemoryRegionInfo info{name, start, size, mirror_mask & kAddressMask, static_cast<bool>(write), true};
    add_region(Region{std::move(info), {}, std::move(read), std::move(write)});
}

std::span<const std::uint8_t> System16AddressSpace::memory(std::string_view name) const {
    for (const auto& region : m_regions) {
        if (region.info.name == name && region.memory) return *region.memory;
    }
    return {};
}

std::span<std::uint8_t> System16AddressSpace::mutable_memory(std::string_view name) {
    for (auto& region : m_regions) {
        if (region.info.name == name && region.memory && region.info.writable) return *region.memory;
    }
    return {};
}

void System16AddressSpace::load_ram(std::string_view name, std::span<const std::uint8_t> bytes) {
    for (auto& region : m_regions) {
        if (region.info.name != name) continue;
        if (!region.memory || !region.info.writable) {
            throw std::invalid_argument("memory region is not writable RAM: " + std::string(name));
        }
        if (bytes.size() != region.memory->size()) {
            throw std::invalid_argument("RAM image size does not match region " + std::string(name));
        }
        std::copy(bytes.begin(), bytes.end(), region.memory->begin());
        return;
    }
    throw std::invalid_argument("unknown RAM region: " + std::string(name));
}

const System16AddressSpace::Region* System16AddressSpace::find_region(
    std::uint32_t address, std::uint32_t& canonical_address) const {
    address &= kAddressMask;
    for (const auto& region : m_regions) {
        const std::uint32_t canonical = address & ~region.info.mirror_mask & kAddressMask;
        if (canonical >= region.info.start && canonical - region.info.start < region.info.size) {
            canonical_address = canonical;
            return &region;
        }
    }
    return nullptr;
}

System16AddressSpace::Region* System16AddressSpace::find_region(std::uint32_t address, std::uint32_t& canonical_address) {
    return const_cast<Region*>(std::as_const(*this).find_region(address, canonical_address));
}

std::optional<std::uint8_t> System16AddressSpace::peek8(std::uint32_t address) const {
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region || !region->memory) return std::nullopt;
    const std::uint32_t offset = canonical - region->info.start;
    if (offset >= region->memory->size()) return std::nullopt;
    return (*region->memory)[offset];
}

ByteRead System16AddressSpace::read8(std::uint32_t address) const {
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region) {
        return {BusFault::Unmapped, 0xff};
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        return {BusFault::None, (*region->memory)[offset]};
    }
    if (!region->read) {
        return {BusFault::DeviceUnhandled, 0xff};
    }
    const auto word = region->read(offset & ~1U);
    if (!word) {
        return {BusFault::DeviceUnhandled, 0xff};
    }
    return {BusFault::None, static_cast<std::uint8_t>((offset & 1U) ? *word : (*word >> 8))};
}

WordRead System16AddressSpace::read16(std::uint32_t address) const {
    if (address & 1U) {
        return {BusFault::AddressError, 0xffff};
    }
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region) {
        return {BusFault::Unmapped, 0xffff};
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (offset + 1 >= region->memory->size()) {
            return {BusFault::Unmapped, 0xffff};
        }
        return {BusFault::None, static_cast<std::uint16_t>(((*region->memory)[offset] << 8) | (*region->memory)[offset + 1])};
    }
    if (!region->read) {
        return {BusFault::DeviceUnhandled, 0xffff};
    }
    const auto word = region->read(offset);
    return word ? WordRead{BusFault::None, *word} : WordRead{BusFault::DeviceUnhandled, 0xffff};
}

BusFault System16AddressSpace::write8(std::uint32_t address, std::uint8_t value) {
    std::uint32_t canonical = 0;
    Region* region = find_region(address, canonical);
    if (!region) {
        return BusFault::Unmapped;
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (!region->info.writable) {
            return BusFault::None; // Writes to ROM are ignored by the board.
        }
        (*region->memory)[offset] = value;
        return BusFault::None;
    }
    if (!region->write) {
        return BusFault::DeviceUnhandled;
    }
    const bool high_lane = (offset & 1U) == 0;
    const std::uint16_t data = high_lane ? static_cast<std::uint16_t>(value << 8) : value;
    const std::uint8_t lane_mask = high_lane ? 0x02 : 0x01;
    return region->write(offset & ~1U, data, lane_mask) ? BusFault::None : BusFault::DeviceUnhandled;
}

BusFault System16AddressSpace::write16(std::uint32_t address, std::uint16_t value) {
    if (address & 1U) {
        return BusFault::AddressError;
    }
    std::uint32_t canonical = 0;
    Region* region = find_region(address, canonical);
    if (!region) {
        return BusFault::Unmapped;
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (!region->info.writable) {
            return BusFault::ReadOnly;
        }
        if (offset + 1 >= region->memory->size()) {
            return BusFault::Unmapped;
        }
        (*region->memory)[offset] = static_cast<std::uint8_t>(value >> 8);
        (*region->memory)[offset + 1] = static_cast<std::uint8_t>(value);
        return BusFault::None;
    }
    if (!region->write) {
        return BusFault::DeviceUnhandled;
    }
    return region->write(offset, value, 0x03) ? BusFault::None : BusFault::DeviceUnhandled;
}

System16AddressSpace make_system16a_address_space(std::span<const std::uint8_t> maincpu_rom,
    System16AVideoSignals* video_signals, System16AInputState* input_state, System16ASound* sound) {
    System16AddressSpace bus;
    struct Ppi8255 {
        std::uint8_t port_a = 0xff;
        std::uint8_t port_b = 0;
        // The sound interface uses the 8255's mode-2 port-A handshake:
        // PC7 is OBF (active low) and PC6 is ACK (active low).
        std::uint8_t port_c = 0xc0;
        std::uint8_t control = 0x9b;
        bool port_a_mode2 = false;
    };
    auto ppi = std::make_shared<Ppi8255>();
    if (sound) {
        sound->set_latch_read_callback([ppi, sound]() {
            ppi->port_c &= static_cast<std::uint8_t>(~0x40U); // ACK low
            if (ppi->port_a_mode2) {
                ppi->port_c |= 0x80U; // ACK releases OBF high
                sound->set_nmi_line(false);
            }
        });
    }
    bus.map_rom("maincpu", 0x000000, 0x380000, maincpu_rom);
    bus.map_ram("tileram", 0x400000, 0x008000, 0xb88000);
    bus.map_ram("textram", 0x410000, 0x001000, 0xb8f000);
    bus.map_ram("sprites", 0x440000, 0x000800, 0x3bf800);
    // MAME's shared palette RAM starts at 0xffff. Preserve that bus power-on
    // value so byte-lane palette writes leave the untouched byte unchanged.
    bus.map_ram("paletteram", 0x840000, 0x001000, 0x3bf000, 0xff);
    // System 16A places the 8255 on the low byte lane at offsets 0, 2, 4 and 6.
    // The initial Shinobi profile uses the standard input and DIP register layout.
    bus.map_device("misc_io", 0xc40000, 0x004000, 0x39c000,
        [ppi, input_state](std::uint32_t offset) -> std::optional<std::uint16_t> {
            if (offset < 0x1000) {
                switch ((offset >> 1) & 3U) {
                case 0: return ppi->port_a;
                case 1: return ppi->port_b;
                case 2: return ppi->port_c;
                default: return ppi->control;
                }
            }
            if (offset >= 0x1000 && offset < 0x2000) {
                // MAME standard_io_r selects SERVICE, P1, UNUSED, P2 on
                // successive word offsets. Inputs are active-low bytes.
                if (!input_state) return 0x00ff;
                switch ((offset >> 1) & 3U) {
                case 0:
                    ++input_state->service_reads;
                    input_state->service_pressed_reads += input_state->service != 0xff;
                    return input_state->service;
                case 1:
                    ++input_state->p1_reads;
                    input_state->p1_pressed_reads += input_state->p1 != 0xff;
                    return input_state->p1;
                case 2: return 0x00ff;
                default:
                    ++input_state->p2_reads;
                    input_state->p2_pressed_reads += input_state->p2 != 0xff;
                    return input_state->p2;
                }
            }
            // Shinobi DSW2 defaults to three lives, normal difficulty, slow
            // enemy bullets, English. DSW1 defaults to Sega coinage 1C/1C.
            if (offset >= 0x2000 && offset < 0x3000) {
                if (!input_state) return (offset & 2U) ? 0x007c : 0x00ff;
                if (offset & 2U) {
                    ++input_state->dsw2_reads;
                    return input_state->dsw2;
                }
                ++input_state->dsw1_reads;
                return input_state->dsw1;
            }
            return 0xffff;
        },
        [ppi, video_signals, sound](std::uint32_t offset, std::uint16_t data, std::uint8_t lanes) {
            if (offset >= 0x1000 || (lanes & 0x01U) == 0) return true;
            const std::uint8_t value = static_cast<std::uint8_t>(data);
            switch ((offset >> 1) & 3U) {
            case 0:
                ppi->port_a = value;
                if (sound) {
                    sound->write_sound_latch(value);
                    if (ppi->port_a_mode2) {
                        ppi->port_c &= static_cast<std::uint8_t>(~0x80U); // OBF low
                        sound->set_nmi_line(true);
                    }
                }
                break;
            case 1:
                ppi->port_b = value;
                if (video_signals) {
                    video_signals->flip = (value & 0x80U) != 0;
                    video_signals->display_enable = (value & 0x10U) != 0;
                }
                break;
            case 2:
                // In mode 2, PC6/PC7 are handshake pins, not ordinary
                // software-controlled port-C outputs.
                ppi->port_c = ppi->port_a_mode2
                    ? static_cast<std::uint8_t>((value & 0x3fU) | (ppi->port_c & 0xc0U))
                    : value;
                if (video_signals) {
                    video_signals->column_scroll_enabled = (value & 0x04U) == 0;
                    video_signals->row_scroll_enabled = (value & 0x02U) == 0;
                }
                if (sound && !ppi->port_a_mode2) sound->set_nmi_line((value & 0x80U) == 0);
                break;
            case 3:
                if (value & 0x80U) {
                    ppi->control = value;
                    ppi->port_a_mode2 = ((value >> 5) & 0x03U) == 0x02U;
                    if (ppi->port_a_mode2) {
                        ppi->port_c |= 0xc0U; // idle: ACK and OBF deasserted
                        if (sound) sound->set_nmi_line(false);
                    }
                } else {
                    const unsigned bit = (value >> 1) & 7U;
                    const auto mask = static_cast<std::uint8_t>(1U << bit);
                    if (ppi->port_a_mode2 && (bit == 6 || bit == 7)) break;
                    if (value & 1U) ppi->port_c |= mask;
                    else ppi->port_c &= static_cast<std::uint8_t>(~mask);
                    if (video_signals && bit == 2) video_signals->column_scroll_enabled = (ppi->port_c & 0x04U) == 0;
                    if (video_signals && bit == 1) video_signals->row_scroll_enabled = (ppi->port_c & 0x02U) == 0;
                    if (sound && !ppi->port_a_mode2 && bit == 7) sound->set_nmi_line((ppi->port_c & 0x80U) == 0);
                }
                break;
            }
            return true;
        });
    // Watchdog reset reads do not carry useful game data.
    bus.map_device("watchdog", 0xc60000, 0x010000, 0,
        [](std::uint32_t) -> std::optional<std::uint16_t> { return 0xffff; },
        [](std::uint32_t, std::uint16_t, std::uint8_t) { return true; });
    bus.map_ram("nvram", 0xc70000, 0x004000, 0x38c000);
    return bus;
}

bool system16_bus_write8_callback(void* context, std::uint32_t address, std::uint8_t value) {
    if (!context) {
        return false;
    }
    auto& bus = *static_cast<System16AddressSpace*>(context);
    return bus.write8(address, value) == BusFault::None;
}

bool system16_bus_write16_callback(void* context, std::uint32_t address, std::uint16_t value) {
    if (!context) return false;
    return static_cast<System16AddressSpace*>(context)->write16(address, value) == BusFault::None;
}

bool system16_bus_read16_callback(void* context, std::uint32_t address, std::uint16_t* value) {
    if (!context || !value) return false;
    const auto result = static_cast<System16AddressSpace*>(context)->read16(address);
    if (!result) return false;
    *value = result.value;
    return true;
}

bool system16_bus_read8_callback(void* context, std::uint32_t address, std::uint8_t* value) {
    if (!context || !value) return false;
    const auto result = static_cast<System16AddressSpace*>(context)->read8(address);
    if (!result) return false;
    *value = result.value;
    return true;
}

} // namespace mamerecomp
