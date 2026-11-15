#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace multiarcaderecomp {

enum class SegaBusFault {
    None,
    Unmapped,
    ReadOnly,
    DeviceUnhandled,
    AddressError,
};

struct SegaByteRead {
    SegaBusFault fault = SegaBusFault::None;
    std::uint8_t value = 0xff;
    explicit operator bool() const { return fault == SegaBusFault::None; }
};

struct SegaWordRead {
    SegaBusFault fault = SegaBusFault::None;
    std::uint16_t value = 0xffff;
    explicit operator bool() const { return fault == SegaBusFault::None; }
};

struct SegaMemoryRegionInfo {
    std::string name;
    std::uint32_t start;
    std::uint32_t size;
    std::uint32_t mirror_mask;
    bool writable;
    bool device;
};

struct SegaVideoSignals {
    bool display_enable = true;
    bool flip = false;
};

// Active-low controls and DIP byte values shared by the current Sega board
// adapters. Each board keeps its own address decoding and register mapping.
struct SegaInputState {
    std::uint8_t service = 0xff;
    std::uint8_t p1 = 0xff;
    std::uint8_t p2 = 0xff;
    std::uint8_t dsw1 = 0xff;
    std::uint8_t dsw2 = 0x7c;
    std::uint64_t service_reads = 0;
    std::uint64_t p1_reads = 0;
    std::uint64_t p2_reads = 0;
    std::uint64_t service_pressed_reads = 0;
    std::uint64_t p1_pressed_reads = 0;
    std::uint64_t p2_pressed_reads = 0;
    std::uint64_t dsw1_reads = 0;
    std::uint64_t dsw2_reads = 0;
};

class SegaAddressSpace {
public:
    virtual ~SegaAddressSpace() = default;
    using DeviceRead16 = std::function<std::optional<std::uint16_t>(std::uint32_t byte_offset)>;
    using DeviceWrite16 = std::function<bool(std::uint32_t byte_offset, std::uint16_t data, std::uint8_t lane_mask)>;

    void map_rom(std::string name, std::uint32_t start, std::uint32_t mirror_mask, std::span<const std::uint8_t> bytes);
    void map_ram(std::string name, std::uint32_t start, std::uint32_t size, std::uint32_t mirror_mask = 0, std::uint8_t fill = 0);
    void map_device(std::string name, std::uint32_t start, std::uint32_t size, std::uint32_t mirror_mask,
        DeviceRead16 read, DeviceWrite16 write);

    virtual SegaByteRead read8(std::uint32_t address) const;
    virtual std::optional<std::uint8_t> peek8(std::uint32_t address) const;
    virtual SegaWordRead read16(std::uint32_t address) const;
    virtual SegaBusFault write8(std::uint32_t address, std::uint8_t value);
    virtual SegaBusFault write16(std::uint32_t address, std::uint16_t value);

    virtual std::span<const SegaMemoryRegionInfo> regions() const { return m_region_info; }
    virtual std::span<const std::uint8_t> memory(std::string_view name) const;
    virtual std::span<std::uint8_t> mutable_memory(std::string_view name);
    virtual void load_ram(std::string_view name, std::span<const std::uint8_t> bytes);

private:
    struct Region {
        SegaMemoryRegionInfo info;
        std::shared_ptr<std::vector<std::uint8_t>> memory;
        DeviceRead16 read;
        DeviceWrite16 write;
    };

    const Region* find_region(std::uint32_t address, std::uint32_t& canonical_address) const;
    Region* find_region(std::uint32_t address, std::uint32_t& canonical_address);
    void add_region(Region region);

    std::vector<Region> m_regions;
    std::vector<SegaMemoryRegionInfo> m_region_info;
};

bool sega_bus_write8_callback(void* context, std::uint32_t address, std::uint8_t value);
bool sega_bus_write16_callback(void* context, std::uint32_t address, std::uint16_t value);
bool sega_bus_read8_callback(void* context, std::uint32_t address, std::uint8_t* value);
bool sega_bus_read16_callback(void* context, std::uint32_t address, std::uint16_t* value);

} // namespace multiarcaderecomp
