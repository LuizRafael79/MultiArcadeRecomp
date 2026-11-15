#pragma once

#include "chips/315_5195/mapper.hpp"
#include "chips/315_5296/io.hpp"
#include "chips/315_5313/vdp.hpp"
#include "boards/sega/common/sega_address_space.hpp"
#include "system18_board.hpp"

#include <array>
#include <functional>
#include <span>
#include <vector>

namespace mamerecomp {

// System 18 bus for supported 171-SHADOW and 171-5874 ROM boards. The VDP has
// a built-in behavioral model; callbacks can override the VDP or I/O endpoints.
class System18AddressSpace final : public SegaAddressSpace {
public:
    struct VdpSignalEvent {
        enum class Signal : std::uint8_t { Enable, Mixing };
        Signal signal = Signal::Enable;
        std::uint8_t value = 0;
        std::uint64_t cycles = 0;
    };
    struct Devices {
        DeviceRead16 vdp_read;
        DeviceWrite16 vdp_write;
        DeviceRead16 io_read;
        DeviceWrite16 io_write;
    };

    explicit System18AddressSpace(std::span<const std::uint8_t> maincpu_rom,
        SegaInputState* inputs = nullptr, SegaVideoSignals* video_signals = nullptr,
        Devices devices = {}, System18RomBoard board = System18RomBoard::ShadowDancer171);

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
    void set_mapper_irq_callback(std::function<void(unsigned)> callback) { m_mapper.set_irq_callback(std::move(callback)); }
    void set_mapper_reset_callback(std::function<void(bool)> callback) { m_mapper.set_maincpu_reset_callback(std::move(callback)); }
    void set_mapper_mcu_irq_callback(std::function<void(bool)> callback) { m_mapper.set_mcu_irq_callback(std::move(callback)); }
    void set_sound_reply(std::uint8_t value) { m_mapper.set_sound_reply(value); }
    void set_vdp_cycle_provider(std::function<std::uint64_t()> cycle_provider,
        std::uint64_t cpu_clock_hz, std::uint32_t refresh_num, std::uint32_t refresh_den);
    chips::Sega3155313& vdp() { return m_vdp; }
    const chips::Sega3155313& vdp() const { return m_vdp; }
    const std::array<std::uint8_t, 8>& tile_banks() const { return m_tile_banks; }
    bool display_enabled() const { return m_video_signals ? m_video_signals->display_enable : m_display_enabled; }
    bool vdp_enabled() const { return m_vdp_enabled; }
    std::uint8_t vdp_mixing() const { return m_vdp_mixing; }
    const std::vector<VdpSignalEvent>& vdp_signal_events() const { return m_vdp_signal_events; }

private:
    struct Resolved {
        const System18MapperWindow* window = nullptr;
        std::uint32_t offset = 0;
    };
    Resolved resolve(std::uint32_t address) const;
    std::uint16_t read_window(const System18MapperWindow& window, std::uint32_t offset) const;
    void write_window(const System18MapperWindow& window, std::uint32_t offset,
        std::uint16_t data, std::uint8_t lanes);
    void update_vdp_beam() const;
    void record_vdp_signal(VdpSignalEvent::Signal signal, std::uint8_t value);
    std::uint16_t read_io(std::uint32_t byte_offset) const;
    void write_io(std::uint32_t byte_offset, std::uint16_t data, std::uint8_t lanes);
    std::uint8_t read_io_port(unsigned port) const;
    void write_io_port(unsigned port, std::uint8_t data);
    static std::array<chips::Mapper3155195::Wiring, 8> mapper_wiring(System18RomBoard board);

    System18RomBoard m_board;
    std::vector<std::uint8_t> m_maincpu;
    std::vector<std::uint8_t> m_workram = std::vector<std::uint8_t>(0x4000, 0);
    std::vector<std::uint8_t> m_sprites = std::vector<std::uint8_t>(0x0800, 0);
    std::vector<std::uint8_t> m_tileram = std::vector<std::uint8_t>(0x10000, 0);
    std::vector<std::uint8_t> m_textram = std::vector<std::uint8_t>(0x1000, 0);
    std::vector<std::uint8_t> m_palette = std::vector<std::uint8_t>(0x1000, 0);
    SegaInputState* m_inputs = nullptr;
    SegaVideoSignals* m_video_signals = nullptr;
    Devices m_devices;
    chips::Mapper3155195 m_mapper;
    std::function<void()> m_mapper_access_callback;
    mutable chips::Sega3155313 m_vdp;
    std::function<std::uint64_t()> m_vdp_cycle_provider;
    std::uint64_t m_vdp_cpu_clock_hz = 0;
    std::uint32_t m_vdp_refresh_num = 0;
    std::uint32_t m_vdp_refresh_den = 0;
    std::array<std::uint8_t, 8> m_tile_banks{0, 1, 2, 3, 4, 5, 6, 7};
    bool m_display_enabled = true;
    bool m_vdp_enabled = false;
    bool m_grayscale_enabled = false;
    std::uint8_t m_vdp_mixing = 0;
    std::vector<VdpSignalEvent> m_vdp_signal_events;
    mutable chips::Sega3155296 m_io;
    mutable std::vector<SegaMemoryRegionInfo> m_region_info;
};

} // namespace mamerecomp
