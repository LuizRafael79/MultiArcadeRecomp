#pragma once

#include "boards/sega/common/board_family.hpp"
#include "chips/315_5195/mapper.hpp"
#include "chips/sega_315_5248/sega_315_5248.hpp"
#include "chips/sega_315_5250/sega_315_5250.hpp"
#include "boards/sega/common/sega_address_space.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mamerecomp {

// Standard 171-5797 region wiring used by Golden Axe (including goldnaxeud).
std::array<chips::Mapper3155195::Wiring, 8> system16b_5797_mapper_wiring(std::uint32_t workram_size = 0x4000);
std::array<chips::Mapper3155195::Wiring, 8> system16b_5358_mapper_wiring(std::uint32_t workram_size = 0x4000);
std::array<chips::Mapper3155195::Wiring, 8> system16b_5521_mapper_wiring(std::uint32_t workram_size = 0x4000);

class System16BAddressSpace final : public SegaAddressSpace {
public:
    struct VideoWriteStats {
        std::uint64_t palette = 0;
        std::uint64_t tile = 0;
        std::uint64_t text = 0;
        std::uint64_t sprite = 0;
        std::uint64_t tile_bank = 0;
    };
    System16BAddressSpace(std::span<const std::uint8_t> maincpu_rom,
        SegaVideoSignals* video_signals = nullptr, SegaInputState* inputs = nullptr,
        System16BBoard board = System16BBoard::Sega5797);
    SegaByteRead read8(std::uint32_t address) const override;
    std::optional<std::uint8_t> peek8(std::uint32_t address) const override;
    SegaWordRead read16(std::uint32_t address) const override;
    SegaBusFault write8(std::uint32_t address, std::uint8_t value) override;
    SegaBusFault write16(std::uint32_t address, std::uint16_t value) override;
    std::span<const SegaMemoryRegionInfo> regions() const override;
    std::span<const std::uint8_t> memory(std::string_view name) const override;
    std::span<std::uint8_t> mutable_memory(std::string_view name) override;
    void load_ram(std::string_view name, std::span<const std::uint8_t> bytes) override;

    chips::Mapper3155195& mapper() { return m_mapper; }
    const chips::Mapper3155195& mapper() const { return m_mapper; }
    void set_mapper_access_callback(std::function<void()> callback) { m_mapper_access_callback = std::move(callback); }
    void set_open_bus_pc_provider(std::function<std::uint32_t()> provider) { m_open_bus_pc_provider = std::move(provider); }
    void set_mapper_irq_callback(std::function<void(unsigned)> callback) { m_mapper.set_irq_callback(std::move(callback)); }
    void set_mapper_reset_callback(std::function<void(bool)> callback) { m_mapper.set_maincpu_reset_callback(std::move(callback)); }
    void set_mapper_mcu_irq_callback(std::function<void(bool)> callback) { m_mapper.set_mcu_irq_callback(std::move(callback)); }
    std::array<std::uint8_t, 8>& tile_banks() { return m_tile_banks; }
    const VideoWriteStats& video_write_stats() const { return m_video_write_stats; }
    std::array<std::uint8_t, 16>& sprite_banks() { return m_sprite_banks; }

private:
    struct Resolved { const chips::Mapper3155195::Mapping* mapping; std::uint32_t offset; };
    Resolved resolve(std::uint32_t address) const;
    std::uint16_t read_open_bus() const;
    std::uint16_t read_io(std::uint32_t offset) const;
    void write_io(std::uint32_t offset, std::uint16_t data, std::uint8_t lanes);
    std::uint16_t read_romboard(std::uint32_t offset, unsigned region) const;
    void write_romboard(std::uint32_t offset, unsigned region, std::uint16_t data, std::uint8_t lanes);
    std::uint16_t read_target(const chips::Mapper3155195::Mapping& mapping, std::uint32_t offset) const;
    void write_target(const chips::Mapper3155195::Mapping& mapping, std::uint32_t offset,
        std::uint16_t data, std::uint8_t lanes);

    std::vector<std::uint8_t> m_maincpu;
    std::vector<std::uint8_t> m_palette = std::vector<std::uint8_t>(0x1000, 0xff);
    std::vector<std::uint8_t> m_tileram = std::vector<std::uint8_t>(0x10000, 0);
    std::vector<std::uint8_t> m_textram = std::vector<std::uint8_t>(0x1000, 0);
    std::vector<std::uint8_t> m_sprites = std::vector<std::uint8_t>(0x0800, 0);
    std::vector<std::uint8_t> m_workram = std::vector<std::uint8_t>(0x4000, 0);
    SegaVideoSignals* m_video_signals;
    SegaInputState* m_inputs;
    System16BBoard m_board;
    chips::Mapper3155195 m_mapper;
    std::function<void()> m_mapper_access_callback;
    std::function<std::uint32_t()> m_open_bus_pc_provider;
    std::array<std::uint8_t, 8> m_tile_banks{0, 1, 2, 3, 4, 5, 6, 7};
    VideoWriteStats m_video_write_stats;
    std::array<std::uint8_t, 16> m_sprite_banks{};
    mutable std::vector<SegaMemoryRegionInfo> m_region_info;
    chips::Sega3155248Multiplier m_multiplier;
    chips::Sega3155250CompareTimer m_compare_timer_1;
    chips::Sega3155250CompareTimer m_compare_timer_2;
};

} // namespace mamerecomp
