#include "boards/sega/system16b/video_session.hpp"
#include "boards/sega/system16b/rom_mapper.hpp"

#include <stdexcept>

namespace multiarcaderecomp {
namespace {
class System16BVideoRenderer final : public SegaBoardVideoRenderer {
public:
    System16BVideoRenderer(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> sprite_rom, SegaAddressSpace& bus,
        SegaVideoSignals& signals, SegaVideoBackend backend, bool capture_reference)
        : m_decoded_tiles(decoded_tiles), m_sprite_rom(sprite_rom), m_bus(bus),
          m_signals(signals), m_backend(backend), m_capture_reference(capture_reference),
          m_board(dynamic_cast<System16BAddressSpace*>(&bus)) {
        if (!m_board) throw std::invalid_argument("System 16B renderer requires System16BAddressSpace");
        m_state.bank_size = 0x1000; // MAME's TILEMAP_16B uses numbanks=2.
    }

    SegaVideoFrame render(std::vector<SegaVideoFrame>*, SegaVideoProfile*) override {
        m_state.display_enable = m_signals.display_enable;
        m_state.flip = m_signals.flip;
        m_state.banks = m_board->tile_banks();
        m_state = chips::sega3155197::latch_frame_registers(m_bus.memory("textram"), m_state);
        if (m_capture_reference) {
            m_reference_state = m_state;
            const auto sprite_ram = m_bus.memory("sprites");
            m_reference_sprite_ram.assign(sprite_ram.begin(), sprite_ram.end());
        }
        return render_system16b_frame(m_decoded_tiles, m_sprite_rom,
            m_bus.memory("tileram"), m_bus.memory("textram"), m_bus.mutable_memory("sprites"),
            m_state, m_board->sprite_banks(), m_backend == SegaVideoBackend::OpenGL, &m_tilemap_renderer);
    }

    SegaVideoFrame render_cpu_reference() override {
        auto sprite_ram = m_reference_sprite_ram;
        return render_system16b_frame_cpu(m_decoded_tiles, m_sprite_rom,
            m_bus.memory("tileram"), m_bus.memory("textram"), sprite_ram,
            m_reference_state, m_board->sprite_banks(), {}, &m_tilemap_renderer);
    }

    std::string_view comparison_label() const override { return "System 16B OpenGL vs CPU framebuffer comparison"; }

private:
    std::span<const std::uint8_t> m_decoded_tiles;
    std::span<const std::uint8_t> m_sprite_rom;
    SegaAddressSpace& m_bus;
    SegaVideoSignals& m_signals;
    SegaVideoBackend m_backend;
    bool m_capture_reference;
    System16BAddressSpace* m_board;
    chips::sega3155197::TilemapState m_state;
    chips::sega3155197::TilemapState m_reference_state;
    chips::sega3155197::TilemapRenderer m_tilemap_renderer;
    std::vector<std::uint8_t> m_reference_sprite_ram;
};
}

std::unique_ptr<SegaBoardVideoRenderer> make_system16b_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, SegaVideoSignals& signals, SegaVideoBackend backend,
    bool capture_reference) {
    return std::make_unique<System16BVideoRenderer>(decoded_tiles, sprite_rom, bus, signals, backend, capture_reference);
}

} // namespace multiarcaderecomp
