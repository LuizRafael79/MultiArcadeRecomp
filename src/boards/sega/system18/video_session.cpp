#include "boards/sega/system18/video_session.hpp"
#include "boards/sega/system18/system18_bus.hpp"
#include "sega_315_5196.hpp"
#include "chips/sega_315_5197/sega_315_5197.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace multiarcaderecomp {
namespace {
class System18VideoRenderer final : public SegaBoardVideoRenderer {
public:
    System18VideoRenderer(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> sprite_rom, SegaAddressSpace& bus,
        SegaVideoSignals& signals)
        : m_decoded_tiles(decoded_tiles), m_sprite_rom(sprite_rom), m_bus(bus),
          m_signals(signals), m_board(dynamic_cast<System18AddressSpace*>(&bus)) {
        if (!m_board) throw std::invalid_argument("System 18 renderer requires System18AddressSpace");
    }

    SegaVideoFrame render(std::vector<SegaVideoFrame>*, SegaVideoProfile*) override {
        m_state.display_enable = m_board->display_enabled();
        m_state.flip = m_signals.flip;
        m_state.banks = m_board->tile_banks();
        m_state.bank_size = 0x400;
        m_state = chips::sega3155197::latch_frame_registers(m_bus.memory("textram"), m_state);
        const auto sprite_ram = m_bus.memory("sprites");
        m_reference_sprite_ram.assign(sprite_ram.begin(), sprite_ram.end());
        return render_frame(m_bus.mutable_memory("sprites"));
    }

    SegaVideoFrame render_cpu_reference() override {
        auto sprite_ram = m_reference_sprite_ram;
        return render_frame(sprite_ram);
    }

    std::string_view comparison_label() const override { return "System 18 OpenGL/CPU framebuffer comparison"; }

private:
    SegaVideoFrame render_frame(std::span<std::uint8_t> sprite_ram) {
        auto output = m_tilemap_renderer.render(m_decoded_tiles,
            m_bus.memory("tileram"), m_bus.memory("textram"), m_state,
            [&](chips::sega3155197::TilemapOutput& stage_output, unsigned stage) {
                mix_vdp_stage(stage_output, stage);
            });
        auto frame = frame_from_output(std::move(output));
        frame.vdp_palette_rgb = m_vdp_palette_rgb;
        const auto sprite_pixels = chips::sega3155196::rasterize_sprites(m_sprite_rom,
            sprite_ram, {}, m_state.flip);
        mix_sprites(frame, sprite_pixels);
        return frame;
    }
    static SegaVideoFrame frame_from_output(chips::sega3155197::TilemapOutput&& output) {
        SegaVideoFrame frame;
        frame.palette_indices = std::move(output.palette_indices);
        frame.tile_priorities = std::move(output.priorities);
        frame.shadow_pixels = std::move(output.shadow_pixels);
        return frame;
    }

    void mix_vdp_stage(chips::sega3155197::TilemapOutput& output, unsigned stage) {
        if (!m_board->vdp_enabled() || ((m_board->vdp_mixing() >> 1) & 3U) != stage) return;
        static constexpr std::array<std::uint8_t, 15> sega_level{
            0, 29, 52, 70, 87, 101, 116, 130, 144, 158, 172, 187, 206, 228, 255};
        const auto& vdp = m_board->vdp();
        for (unsigned index = 0; index < 64; ++index) {
            const unsigned raw = vdp.cram_word(index);
            const unsigned converted = ((raw & 0x00eU) >> 1)
                | ((raw & 0x0e0U) >> 2) | ((raw & 0xe00U) >> 3);
            const unsigned r = converted & 7U;
            const unsigned g = (converted >> 3) & 7U;
            const unsigned b = (converted >> 6) & 7U;
            m_vdp_palette_rgb[index] = (static_cast<std::uint32_t>(sega_level[r << 1]) << 16)
                | (static_cast<std::uint32_t>(sega_level[g << 1]) << 8)
                | sega_level[b << 1];
        }
        const unsigned priority_mark = (m_board->vdp_mixing() & 1U) ? (1U << stage) : 0U;
        for (unsigned y = 0; y < SegaVideoFrame::height; ++y) {
            const auto line = vdp.render_scanline(y);
            for (unsigned x = 0; x < SegaVideoFrame::width; ++x) {
                const auto& source = line[x];
                if (!source.opaque) continue;
                const auto pixel = static_cast<std::size_t>(y) * SegaVideoFrame::width + x;
                output.palette_indices[pixel] = static_cast<std::uint16_t>(0x1000U + source.color);
                output.priorities[pixel] |= static_cast<std::uint8_t>(priority_mark);
            }
        }
    }

    static void mix_sprites(SegaVideoFrame& frame, std::span<const std::uint16_t> sprite_pixels) {
        for (std::size_t pixel = 0; pixel < sprite_pixels.size() && pixel < frame.palette_indices.size(); ++pixel) {
            const std::uint16_t sprite = sprite_pixels[pixel];
            if (sprite == 0xffffU) continue;
            const unsigned priority = (sprite >> 10) & 3U;
            if ((1U << priority) <= frame.tile_priorities[pixel]) continue;
            if ((sprite & 0x03f0U) == 0x03f0U) {
                frame.palette_indices[pixel] = static_cast<std::uint16_t>(frame.palette_indices[pixel] + 0x800U);
                frame.shadow_pixels[pixel] = 1;
            } else {
                frame.palette_indices[pixel] = static_cast<std::uint16_t>(0x400U | (sprite & 0x03ffU));
            }
        }
    }

    std::span<const std::uint8_t> m_decoded_tiles;
    std::span<const std::uint8_t> m_sprite_rom;
    SegaAddressSpace& m_bus;
    SegaVideoSignals& m_signals;
    System18AddressSpace* m_board;
    chips::sega3155197::TilemapState m_state;
    chips::sega3155197::TilemapRenderer m_tilemap_renderer;
    std::vector<std::uint8_t> m_reference_sprite_ram;
    std::array<std::uint32_t, 64> m_vdp_palette_rgb{};
};
}

std::unique_ptr<SegaBoardVideoRenderer> make_system18_video_renderer(
    std::span<const std::uint8_t> decoded_tiles, std::span<const std::uint8_t> sprite_rom,
    SegaAddressSpace& bus, SegaVideoSignals& signals) {
    return std::make_unique<System18VideoRenderer>(decoded_tiles, sprite_rom, bus, signals);
}

} // namespace multiarcaderecomp
