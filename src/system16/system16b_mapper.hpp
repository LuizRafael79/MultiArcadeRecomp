#pragma once

#include "chips/315_5195/mapper.hpp"
#include "system16_bus.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mamerecomp {

// Compatibility name while board code migrates to the chip-oriented API.
using System16BMapper3155195 = chips::Mapper3155195;

// Standard 171-5797 region wiring used by Golden Axe (including goldnaxeud).
std::array<System16BMapper3155195::Wiring, 8> system16b_5797_mapper_wiring(std::uint32_t workram_size = 0x4000);
std::array<System16BMapper3155195::Wiring, 8> system16b_5358_mapper_wiring(std::uint32_t workram_size = 0x4000);

class System16BAddressSpace final : public System16AddressSpace {
public:
    System16BAddressSpace(std::span<const std::uint8_t> maincpu_rom,
        System16AVideoSignals* video_signals = nullptr, System16AInputState* inputs = nullptr,
        System16BBoard board = System16BBoard::Sega5797);
    ByteRead read8(std::uint32_t address) const override;
    std::optional<std::uint8_t> peek8(std::uint32_t address) const override;
    WordRead read16(std::uint32_t address) const override;
    BusFault write8(std::uint32_t address, std::uint8_t value) override;
    BusFault write16(std::uint32_t address, std::uint16_t value) override;
    std::span<const MemoryRegionInfo> regions() const override;
    std::span<const std::uint8_t> memory(std::string_view name) const override;
    std::span<std::uint8_t> mutable_memory(std::string_view name) override;
    void load_ram(std::string_view name, std::span<const std::uint8_t> bytes) override;

    System16BMapper3155195& mapper() { return m_mapper; }
    const System16BMapper3155195& mapper() const { return m_mapper; }
    std::array<std::uint8_t, 8>& tile_banks() { return m_tile_banks; }
    std::array<std::uint8_t, 16>& sprite_banks() { return m_sprite_banks; }

private:
    struct Resolved { const System16BMapper3155195::Mapping* mapping; std::uint32_t offset; };
    Resolved resolve(std::uint32_t address) const;
    std::uint16_t read_io(std::uint32_t offset) const;
    void write_io(std::uint32_t offset, std::uint16_t data, std::uint8_t lanes);
    std::uint16_t read_romboard(std::uint32_t offset, unsigned region) const;
    void write_romboard(std::uint32_t offset, unsigned region, std::uint16_t data, std::uint8_t lanes);
    static std::uint16_t read_compare(const std::array<std::uint16_t, 16>& regs, std::uint32_t offset);
    static void write_compare(std::array<std::uint16_t, 16>& regs, std::uint8_t& history_bit,
        std::uint32_t offset, std::uint16_t data, std::uint8_t lanes);
    static void execute_compare(std::array<std::uint16_t, 16>& regs, std::uint8_t& history_bit,
        bool update_history);
    std::uint16_t read_target(const System16BMapper3155195::Mapping& mapping, std::uint32_t offset) const;
    void write_target(const System16BMapper3155195::Mapping& mapping, std::uint32_t offset,
        std::uint16_t data, std::uint8_t lanes);

    std::vector<std::uint8_t> m_maincpu;
    std::vector<std::uint8_t> m_palette = std::vector<std::uint8_t>(0x1000, 0xff);
    std::vector<std::uint8_t> m_tileram = std::vector<std::uint8_t>(0x10000, 0);
    std::vector<std::uint8_t> m_textram = std::vector<std::uint8_t>(0x1000, 0);
    std::vector<std::uint8_t> m_sprites = std::vector<std::uint8_t>(0x0800, 0);
    std::vector<std::uint8_t> m_workram = std::vector<std::uint8_t>(0x4000, 0);
    System16AVideoSignals* m_video_signals;
    System16AInputState* m_inputs;
    System16BMapper3155195 m_mapper;
    std::array<std::uint8_t, 8> m_tile_banks{0, 1, 2, 3, 4, 5, 6, 7};
    std::array<std::uint8_t, 16> m_sprite_banks{};
    mutable std::vector<MemoryRegionInfo> m_region_info;
    std::array<std::uint16_t, 2> m_multiplier_regs{};
    std::array<std::uint16_t, 16> m_compare1_regs{};
    std::array<std::uint16_t, 16> m_compare2_regs{};
    std::uint8_t m_compare1_history_bit = 0;
    std::uint8_t m_compare2_history_bit = 0;
};

} // namespace mamerecomp
