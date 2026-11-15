#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace multiarcaderecomp::chips {

// Behavioral model of Sega's 315-5195 mapper ASIC. Board packages provide
// the eight region wirings and callbacks for the device's external bus pins.
class Mapper3155195 {
public:
    enum class Target : std::uint8_t { Rom, Io, PaletteRam, TileRamAndTextRam, SpriteRam, WorkRam, ExternalDevice };
    struct Wiring { Target target; std::uint32_t length; std::uint32_t mirror; std::uint32_t target_offset = 0; };
    struct Mapping { unsigned region; Target target; std::uint32_t start; std::uint32_t end; std::uint32_t mirror; std::uint32_t target_offset; std::uint32_t size_mask; };
    struct Callbacks {
        std::function<std::uint16_t(std::uint32_t)> read16;
        std::function<void(std::uint32_t, std::uint16_t)> write16;
        std::function<void(std::uint8_t)> to_sound;
        std::function<void(unsigned)> irq;
        std::function<void(bool)> maincpu_reset;
        std::function<void(bool)> mcu_irq;
    };

    explicit Mapper3155195(std::array<Wiring, 8> wiring, Callbacks callbacks = {});
    std::uint8_t read(std::uint8_t offset);
    void write(std::uint8_t offset, std::uint8_t data);
    void configure_explicit(std::span<const std::uint8_t, 16> config);
    std::span<const std::uint8_t, 32> registers() const { return m_regs; }
    std::span<const Mapping> mappings() const { return m_mappings; }
    void set_sound_reply(std::uint8_t data);
    void set_sound_callback(std::function<void(std::uint8_t)> callback) { m_callbacks.to_sound = std::move(callback); }
    void set_irq_callback(std::function<void(unsigned)> callback) { m_callbacks.irq = std::move(callback); }
    void set_maincpu_reset_callback(std::function<void(bool)> callback) { m_callbacks.maincpu_reset = std::move(callback); }
    void set_mcu_irq_callback(std::function<void(bool)> callback) { m_callbacks.mcu_irq = std::move(callback); }
    std::uint8_t sound_latch_read();

private:
    void rebuild_mappings();
    std::uint16_t read_mapped(std::uint32_t address);
    void write_mapped(std::uint32_t address, std::uint16_t data);

    std::array<Wiring, 8> m_wiring;
    Callbacks m_callbacks;
    std::array<std::uint8_t, 32> m_regs{};
    std::vector<Mapping> m_mappings;
    std::uint8_t m_sound_reply = 0;
};

} // namespace multiarcaderecomp::chips
