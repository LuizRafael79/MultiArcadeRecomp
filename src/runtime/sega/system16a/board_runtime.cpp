#include "runtime/sega/board_runtime.hpp"

#include "boards/sega/system16a/address_space.hpp"
#include "boards/sega/system16a/sound_board.hpp"
#include "boards/sega/system16a/video_session.hpp"
#include "boards/sega/common/graphics_decode.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace multiarcaderecomp::sega {
namespace {

struct System16ASoundRoms {
    std::vector<std::uint8_t> soundcpu;
    std::vector<std::uint8_t> upd7751;
    std::vector<std::uint8_t> upd7751_data;
};

System16ASoundRoms load_system16a_sound_roms(const GameProfile& profile,
    const std::filesystem::path& zip_path) {
    if (profile.upd7751_roms.empty() || profile.upd7751_data_roms.empty())
        throw std::runtime_error("game profile has no System 16A sound ROM layout");
    return {
        load_soundcpu_roms(profile, zip_path),
        load_profile_rom_region(profile, zip_path, profile.upd7751_region_size, profile.upd7751_roms),
        load_profile_rom_region(profile, zip_path, profile.upd7751_data_region_size, profile.upd7751_data_roms),
    };
}

} // namespace

BoardRuntimeHardware make_system16a_runtime(const BoardRuntimeContext& context) {
    BoardRuntimeHardware runtime;
    if (context.profile.default_dsw1) context.input_state.dsw1 = *context.profile.default_dsw1;
    if (context.profile.default_dsw2) context.input_state.dsw2 = *context.profile.default_dsw2;
    if (context.dsw1_override) context.input_state.dsw1 = *context.dsw1_override;
    if (context.dsw2_override) context.input_state.dsw2 = *context.dsw2_override;
    std::shared_ptr<System16ASound> sound;
    if (context.audio_enabled) {
        const auto roms = load_system16a_sound_roms(context.profile, context.rom_path);
        std::cout << "Validated sound ROMs: Z80=0x" << std::hex << roms.soundcpu.size()
                  << " uPD7751=0x" << roms.upd7751.size()
                  << " sample-data=0x" << roms.upd7751_data.size() << std::dec << '\n';
        sound = std::make_shared<System16ASound>(roms.soundcpu, roms.upd7751,
            roms.upd7751_data, context.audio_sink, context.audio_stereo_spread, context.audio_volume);
        std::cout << "Audio emulation: Z80 AOT + interpreter fallback + YM2151/YMFM + uPD7751/MCS-48/DAC enabled"
                  << (context.window_enabled ? " with stereo playback" : " without host playback")
                  << (context.audio_stereo_spread ? " (alternating-channel stereo spread)" : " (game-programmed pan)")
                  << "; sample output is driven by the interpreted MCU firmware\n";
        runtime.sound_owner = sound;
        runtime.sound_active = [sound] { return static_cast<bool>(sound); };
        runtime.advance_sound = [sound](std::uint64_t cycles) { sound->advance_to_main_cycles(cycles); };
        runtime.report_sound = [sound] {
            std::cout << "Sound CPU: Z80 cycles=" << std::dec << sound->z80_cycles()
                      << " instructions=" << sound->z80_instructions() << " pc=0x" << std::hex << sound->z80_pc()
                      << std::dec << "\nZ80 execution coverage: AOT instructions=" << sound->z80_aot_instructions()
                      << " fallback instructions=" << sound->z80_fallback_instructions()
                      << "; YM2151 writes=" << sound->ym2151_writes()
                      << " key-ons=" << sound->ym2151_key_ons() << " key-ons/ch=";
            for (std::size_t i = 0; i < sound->ym2151_key_ons_by_channel().size(); ++i) {
                if (i) std::cout << ',';
                std::cout << sound->ym2151_key_ons_by_channel()[i];
            }
            std::cout << " pan=";
            for (std::size_t i = 0; i < sound->ym2151_pan_by_channel().size(); ++i) {
                if (i) std::cout << ',';
                std::cout << std::hex << static_cast<unsigned>(sound->ym2151_pan_by_channel()[i]);
            }
            std::cout << " last-reg=0x" << static_cast<unsigned>(sound->ym2151_last_register())
                      << " value=0x" << static_cast<unsigned>(sound->ym2151_last_value())
                      << " upd7751 commands=" << sound->upd7751_commands()
                      << " last-command=0x" << static_cast<unsigned>(sound->upd7751_last_command())
                      << " MCU-insns=" << std::dec << sound->upd7751_instructions()
                      << " MCU-PC=0x" << std::hex << sound->upd7751_pc()
                      << " DAC-writes=" << std::dec << sound->upd7751_p1_writes()
                      << " NMI-edges=" << sound->nmi_edges() << " latch-reads=" << sound->sound_latch_reads()
                      << " latch=0x" << std::hex << static_cast<unsigned>(sound->sound_latch())
                      << " generated samples=" << sound->audio_samples() << " peak=" << sound->audio_peak()
                      << " LR-difference-peak=" << sound->stereo_difference_peak() << '\n';
        };
    }
    const auto start = std::chrono::steady_clock::now();
    runtime.decoded_tiles = decode_sega_tiles_3bpp(context.tile_rom);
    runtime.tile_decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    auto video_signals = std::make_shared<System16AVideoSignals>(context.video_signals);
    runtime.board_state_owner = video_signals;
    runtime.bus = std::make_unique<SegaAddressSpace>(make_system16a_address_space(
        context.maincpu_rom, video_signals.get(), &context.input_state, sound.get()));
    runtime.video = make_system16a_video_renderer(runtime.decoded_tiles, context.sprite_rom,
        *runtime.bus, *video_signals, context.video_backend);
    runtime.append_video_signal_state = [video_signals](std::ostream& output) {
        output << "row_scroll_enabled=" << video_signals->row_scroll_enabled << '\n'
               << "column_scroll_enabled=" << video_signals->column_scroll_enabled << '\n';
    };
    runtime.report_video_signals = [video_signals](std::ostream& output) {
        output << " row-scroll=" << video_signals->row_scroll_enabled
               << " column-scroll=" << video_signals->column_scroll_enabled;
    };
    runtime.report_board_video_state = [bus = runtime.bus.get()](std::ostream& output) {
        const auto textram = bus->memory("textram");
        const auto word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        output << "Tilemap regs: page-bg=0x" << std::hex << word(0xe9c)
               << " page-fg=0x" << word(0xe9e)
               << " x-fg/bg=0x" << word(0xff8) << "/0x" << word(0xffa)
               << " y-fg/bg=0x" << word(0xf24) << "/0x" << word(0xf26)
               << std::dec << '\n';
    };
    runtime.report_debug_snapshot = [bus = runtime.bus.get()](std::ostream& output) {
        for (std::uint32_t address = 0x440010; address < 0x440050; address += 2) {
            const auto high = bus->peek8(address);
            const auto low = bus->peek8(address + 1);
            output << "  [0x" << std::hex << std::setfill('0') << std::setw(6) << address << "]=0x";
            if (high && low) output << std::setw(4) << ((static_cast<unsigned>(*high) << 8) | *low);
            else output << "????";
            output << '\n';
        }
        output << "  sprite-source RAM (0xfff800..0xfff87f):\n";
        for (std::uint32_t base = 0x00fff800; base < 0x00fff880; base += 16) {
            output << "  [0x" << std::hex << std::setfill('0') << std::setw(6) << base << "]";
            for (std::uint32_t offset = 0; offset < 16; ++offset) {
                const auto byte = bus->peek8(base + offset);
                if (byte) output << ' ' << std::setw(2) << static_cast<unsigned>(*byte);
                else output << " ??";
            }
            output << '\n';
        }
        output << std::dec;
    };
    runtime.write_board_video_state = [bus = runtime.bus.get()](
        const std::filesystem::path&, std::ostream& info) {
        const auto textram = bus->memory("textram");
        const auto word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        info << "page_bg=0x" << std::hex << word(0xe9c) << '\n'
             << "page_fg=0x" << word(0xe9e) << '\n'
             << "x_fg=0x" << word(0xff8) << '\n'
             << "x_bg=0x" << word(0xffa) << '\n'
             << "y_fg=0x" << word(0xf24) << '\n'
             << "y_bg=0x" << word(0xf26) << '\n';
    };
    return runtime;
}

} // namespace multiarcaderecomp::sega

extern "C" unsigned multiarcaderecomp_runtime_abi() { return 1; }
extern "C" unsigned multiarcaderecomp_runtime_revision() { return 16; }
extern "C" multiarcaderecomp::sega::BoardRuntimeHardware* multiarcaderecomp_create_runtime(
    const multiarcaderecomp::sega::BoardRuntimeContext* context) {
    return context ? new multiarcaderecomp::sega::BoardRuntimeHardware(
        multiarcaderecomp::sega::make_system16a_runtime(*context)) : nullptr;
}
extern "C" void multiarcaderecomp_destroy_runtime(multiarcaderecomp::sega::BoardRuntimeHardware* runtime) {
    delete runtime;
}
extern "C" bool multiarcaderecomp_runtime_memory_map(const multiarcaderecomp::GameProfile* profile,
    const std::uint8_t* rom, std::size_t size,
    void (*visit)(void*, const char*, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool), void* user) {
    if (!profile || (!rom && size) || !visit) return false;
    const auto bus = multiarcaderecomp::make_system16a_address_space(std::span<const std::uint8_t>(rom, size));
    for (const auto& region : bus.regions())
        visit(user, region.name.c_str(), region.start, region.size, region.mirror_mask, region.writable, region.device);
    return true;
}

extern "C" std::vector<std::uint8_t> multiarcaderecomp_runtime_decode_tiles(
    const std::uint8_t* data, std::size_t size) {
    if (!data && size) throw std::invalid_argument("null tile ROM data");
    return multiarcaderecomp::decode_sega_tiles_3bpp(std::span<const std::uint8_t>(data, size));
}
