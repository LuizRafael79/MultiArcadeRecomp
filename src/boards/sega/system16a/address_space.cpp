#include "boards/sega/system16a/address_space.hpp"
#include "boards/sega/system16a/sound_board.hpp"
#include "chips/i8255/i8255.hpp"

#include <memory>

namespace mamerecomp {
SegaAddressSpace make_system16a_address_space(std::span<const std::uint8_t> maincpu_rom,
    System16AVideoSignals* video_signals, SegaInputState* input_state, System16ASound* sound) {
    SegaAddressSpace bus;
    auto ppi = std::make_shared<chips::I8255>();
    if (sound) {
        sound->set_latch_read_callback([ppi, sound]() {
            ppi->acknowledge_port_a();
            if (ppi->port_a_mode2()) sound->set_nmi_line(false);
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
    // The board exposes the standard input and DIP register layout.
    bus.map_device("misc_io", 0xc40000, 0x004000, 0x39c000,
        [ppi, input_state](std::uint32_t offset) -> std::optional<std::uint16_t> {
            if (offset < 0x1000) {
                return ppi->read((offset >> 1) & 3U);
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
            const auto effects = ppi->write((offset >> 1) & 3U, value);
            switch (effects.register_index) {
            case 0:
                if (sound) {
                    sound->write_sound_latch(value);
                    if (effects.mode2) sound->set_nmi_line(true);
                }
                break;
            case 1:
                if (video_signals) {
                    video_signals->common.flip = (value & 0x80U) != 0;
                    video_signals->common.display_enable = (value & 0x10U) != 0;
                }
                break;
            case 2:
                if (effects.port_c_write && video_signals) {
                    video_signals->column_scroll_enabled = (effects.value & 0x04U) == 0;
                    video_signals->row_scroll_enabled = (effects.value & 0x02U) == 0;
                }
                if (effects.port_c_write && sound && !effects.mode2)
                    sound->set_nmi_line((effects.value & 0x80U) == 0);
                break;
            default:
                if (effects.control_write && effects.mode2 && sound)
                    sound->set_nmi_line(false);
                if (effects.port_c_changed) {
                    if (video_signals) {
                        video_signals->column_scroll_enabled = (effects.value & 0x04U) == 0;
                        video_signals->row_scroll_enabled = (effects.value & 0x02U) == 0;
                    }
                    if (sound && !effects.mode2)
                        sound->set_nmi_line((effects.value & 0x80U) == 0);
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


} // namespace mamerecomp
