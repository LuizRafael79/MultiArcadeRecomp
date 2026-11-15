#include "boards/sega/common/sega_address_space.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace multiarcaderecomp {

namespace {

constexpr std::uint32_t kAddressMask = 0x00ffffff;

void validate_region(std::uint32_t start, std::uint32_t size) {
    if (size == 0 || start > kAddressMask || static_cast<std::uint64_t>(start) + size > kAddressMask + 1ULL) {
        throw std::invalid_argument("invalid Sega address region");
    }
}

} // namespace

void SegaAddressSpace::add_region(Region region) {
    const std::uint64_t new_end = static_cast<std::uint64_t>(region.info.start) + region.info.size;
    if (std::any_of(m_regions.begin(), m_regions.end(), [&](const Region& current) {
            const std::uint64_t current_end = static_cast<std::uint64_t>(current.info.start) + current.info.size;
            return region.info.name == current.info.name
                || (region.info.start < current_end && current.info.start < new_end);
        })) {
        throw std::invalid_argument("duplicate or overlapping Sega base region: " + region.info.name);
    }
    m_region_info.push_back(region.info);
    m_regions.push_back(std::move(region));
}

void SegaAddressSpace::map_rom(std::string name, std::uint32_t start, std::uint32_t mirror_mask,
    std::span<const std::uint8_t> bytes) {
    validate_region(start, static_cast<std::uint32_t>(bytes.size()));
    auto memory = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
    SegaMemoryRegionInfo info{name, start, static_cast<std::uint32_t>(bytes.size()), mirror_mask & kAddressMask, false, false};
    add_region(Region{std::move(info), std::move(memory), {}, {}});
}

void SegaAddressSpace::map_ram(std::string name, std::uint32_t start, std::uint32_t size,
    std::uint32_t mirror_mask, std::uint8_t fill) {
    validate_region(start, size);
    auto memory = std::make_shared<std::vector<std::uint8_t>>(size, fill);
    SegaMemoryRegionInfo info{name, start, size, mirror_mask & kAddressMask, true, false};
    add_region(Region{std::move(info), std::move(memory), {}, {}});
}

void SegaAddressSpace::map_device(std::string name, std::uint32_t start, std::uint32_t size,
    std::uint32_t mirror_mask, DeviceRead16 read, DeviceWrite16 write) {
    validate_region(start, size);
    SegaMemoryRegionInfo info{name, start, size, mirror_mask & kAddressMask, static_cast<bool>(write), true};
    add_region(Region{std::move(info), {}, std::move(read), std::move(write)});
}

std::span<const std::uint8_t> SegaAddressSpace::memory(std::string_view name) const {
    for (const auto& region : m_regions) {
        if (region.info.name == name && region.memory) return *region.memory;
    }
    return {};
}

std::span<std::uint8_t> SegaAddressSpace::mutable_memory(std::string_view name) {
    for (auto& region : m_regions) {
        if (region.info.name == name && region.memory && region.info.writable) return *region.memory;
    }
    return {};
}

void SegaAddressSpace::load_ram(std::string_view name, std::span<const std::uint8_t> bytes) {
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

const SegaAddressSpace::Region* SegaAddressSpace::find_region(
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

SegaAddressSpace::Region* SegaAddressSpace::find_region(std::uint32_t address, std::uint32_t& canonical_address) {
    return const_cast<Region*>(std::as_const(*this).find_region(address, canonical_address));
}

std::optional<std::uint8_t> SegaAddressSpace::peek8(std::uint32_t address) const {
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region || !region->memory) return std::nullopt;
    const std::uint32_t offset = canonical - region->info.start;
    if (offset >= region->memory->size()) return std::nullopt;
    return (*region->memory)[offset];
}

SegaByteRead SegaAddressSpace::read8(std::uint32_t address) const {
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region) {
        return {SegaBusFault::Unmapped, 0xff};
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        return {SegaBusFault::None, (*region->memory)[offset]};
    }
    if (!region->read) {
        return {SegaBusFault::DeviceUnhandled, 0xff};
    }
    const auto word = region->read(offset & ~1U);
    if (!word) {
        return {SegaBusFault::DeviceUnhandled, 0xff};
    }
    return {SegaBusFault::None, static_cast<std::uint8_t>((offset & 1U) ? *word : (*word >> 8))};
}

SegaWordRead SegaAddressSpace::read16(std::uint32_t address) const {
    if (address & 1U) {
        return {SegaBusFault::AddressError, 0xffff};
    }
    std::uint32_t canonical = 0;
    const Region* region = find_region(address, canonical);
    if (!region) {
        return {SegaBusFault::Unmapped, 0xffff};
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (offset + 1 >= region->memory->size()) {
            return {SegaBusFault::Unmapped, 0xffff};
        }
        return {SegaBusFault::None, static_cast<std::uint16_t>(((*region->memory)[offset] << 8) | (*region->memory)[offset + 1])};
    }
    if (!region->read) {
        return {SegaBusFault::DeviceUnhandled, 0xffff};
    }
    const auto word = region->read(offset);
    return word ? SegaWordRead{SegaBusFault::None, *word} : SegaWordRead{SegaBusFault::DeviceUnhandled, 0xffff};
}

SegaBusFault SegaAddressSpace::write8(std::uint32_t address, std::uint8_t value) {
    std::uint32_t canonical = 0;
    Region* region = find_region(address, canonical);
    if (!region) {
        return SegaBusFault::Unmapped;
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (!region->info.writable) {
            return SegaBusFault::None; // Writes to ROM are ignored by the board.
        }
        (*region->memory)[offset] = value;
        return SegaBusFault::None;
    }
    if (!region->write) {
        return SegaBusFault::DeviceUnhandled;
    }
    const bool high_lane = (offset & 1U) == 0;
    const std::uint16_t data = high_lane ? static_cast<std::uint16_t>(value << 8) : value;
    const std::uint8_t lane_mask = high_lane ? 0x02 : 0x01;
    return region->write(offset & ~1U, data, lane_mask) ? SegaBusFault::None : SegaBusFault::DeviceUnhandled;
}

SegaBusFault SegaAddressSpace::write16(std::uint32_t address, std::uint16_t value) {
    if (address & 1U) {
        return SegaBusFault::AddressError;
    }
    std::uint32_t canonical = 0;
    Region* region = find_region(address, canonical);
    if (!region) {
        return SegaBusFault::Unmapped;
    }
    const std::uint32_t offset = canonical - region->info.start;
    if (region->memory) {
        if (!region->info.writable) {
            return SegaBusFault::ReadOnly;
        }
        if (offset + 1 >= region->memory->size()) {
            return SegaBusFault::Unmapped;
        }
        (*region->memory)[offset] = static_cast<std::uint8_t>(value >> 8);
        (*region->memory)[offset + 1] = static_cast<std::uint8_t>(value);
        return SegaBusFault::None;
    }
    if (!region->write) {
        return SegaBusFault::DeviceUnhandled;
    }
    return region->write(offset, value, 0x03) ? SegaBusFault::None : SegaBusFault::DeviceUnhandled;
}

bool sega_bus_write8_callback(void* context, std::uint32_t address, std::uint8_t value) {
    if (!context) {
        return false;
    }
    auto& bus = *static_cast<SegaAddressSpace*>(context);
    return bus.write8(address, value) == SegaBusFault::None;
}

bool sega_bus_write16_callback(void* context, std::uint32_t address, std::uint16_t value) {
    if (!context) return false;
    return static_cast<SegaAddressSpace*>(context)->write16(address, value) == SegaBusFault::None;
}

bool sega_bus_read16_callback(void* context, std::uint32_t address, std::uint16_t* value) {
    if (!context || !value) return false;
    const auto result = static_cast<SegaAddressSpace*>(context)->read16(address);
    if (!result) return false;
    *value = result.value;
    return true;
}

bool sega_bus_read8_callback(void* context, std::uint32_t address, std::uint8_t* value) {
    if (!context || !value) return false;
    const auto result = static_cast<SegaAddressSpace*>(context)->read8(address);
    if (!result) return false;
    *value = result.value;
    return true;
}


} // namespace multiarcaderecomp
