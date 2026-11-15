#include "boards/sega/system16a/video_session.hpp"

namespace multiarcaderecomp {
namespace {
class System16AVideoRenderer final : public SegaBoardVideoRenderer {
public:
    System16AVideoRenderer(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> sprite_rom, SegaAddressSpace& bus,
        System16AVideoSignals& signals, SegaVideoBackend backend)
        : m_decoded_tiles(decoded_tiles), m_sprite_rom(sprite_rom), m_bus(bus),
          m_signals(signals), m_backend(backend) {}

    SegaVideoFrame render(std::vector<SegaVideoFrame>* stages,
        SegaVideoProfile* profile) override {
        return render_system16a_frame(m_decoded_tiles, m_sprite_rom,
            m_bus.memory("tileram"), m_bus.memory("textram"), m_bus.memory("sprites"),
            m_bus.memory("paletteram"), {m_signals.common.display_enable, m_signals.common.flip,
                m_signals.row_scroll_enabled, m_signals.column_scroll_enabled},
            stages, profile, m_backend);
    }

    SegaVideoFrame render_cpu_reference() override {
        return render_system16a_frame(m_decoded_tiles, m_sprite_rom,
            m_bus.memory("tileram"), m_bus.memory("textram"), m_bus.memory("sprites"),
            m_bus.memory("paletteram"), {m_signals.common.display_enable, m_signals.common.flip,
                m_signals.row_scroll_enabled, m_signals.column_scroll_enabled});
    }

    std::string_view comparison_label() const override { return "OpenGL vs CPU framebuffer comparison"; }

private:
    std::span<const std::uint8_t> m_decoded_tiles;
    std::span<const std::uint8_t> m_sprite_rom;
    SegaAddressSpace& m_bus;
    System16AVideoSignals& m_signals;
    SegaVideoBackend m_backend;
};
}

std::unique_ptr<SegaBoardVideoRenderer> make_system16a_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, System16AVideoSignals& signals, SegaVideoBackend backend) {
    return std::make_unique<System16AVideoRenderer>(decoded_tiles, sprite_rom, bus, signals, backend);
}

} // namespace multiarcaderecomp
