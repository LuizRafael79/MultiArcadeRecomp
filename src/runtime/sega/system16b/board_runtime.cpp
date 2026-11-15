#include "runtime/sega/board_runtime.hpp"

#include "boards/sega/system16b/rom_mapper.hpp"
#include "boards/sega/system16b/sound_board.hpp"
#include "boards/sega/system16b/video_session.hpp"
#include "boards/sega/common/graphics_decode.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "chips/sega_i8751/sega_i8751.hpp"
#include "runtime/sega/system16b/fd1094_musashi_binding.hpp"
#include "cpus/m68000/fd1094/fd1094.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace multiarcaderecomp::sega {
namespace {
struct Fd1094RuntimeState {
    std::shared_ptr<multiarcaderecomp::Fd1094> cipher;
    std::unique_ptr<Fd1094MusashiBinding> musashi_binding;
};
}

BoardRuntimeHardware make_system16b_runtime(const BoardRuntimeContext& context) {
    const auto& config = std::get<System16BConfiguration>(context.profile.board_configuration);
    BoardRuntimeHardware runtime;
    runtime.supports_video_profile = false;
    runtime.cpu_sprite_gpu_mixer = context.video_backend == SegaVideoBackend::OpenGL;
    runtime.video_backend_description = context.video_backend == SegaVideoBackend::OpenGL
        ? "OpenGL 16B tilemaps + CPU sprite rasterizer/OpenGL mixer" : "software CPU";
    if (context.profile.default_dsw1) context.input_state.dsw1 = *context.profile.default_dsw1;
    if (context.profile.default_dsw2) context.input_state.dsw2 = *context.profile.default_dsw2;
    if (context.dsw1_override) context.input_state.dsw1 = *context.dsw1_override;
    if (context.dsw2_override) context.input_state.dsw2 = *context.dsw2_override;
    std::shared_ptr<System16BSound> sound;
    if (context.audio_enabled) {
        const auto roms = load_soundcpu_roms(context.profile, context.rom_path);
        std::cout << "Validated System 16B sound ROM region: 0x" << std::hex << roms.size() << std::dec << " bytes\n";
        sound = std::make_shared<System16BSound>(roms, context.audio_sink, context.audio_volume,
            config.rom_board, context.audio_stereo_spread, context.ym2151_muted_channels,
            context.ym2151_suppress_slot0_channel2, context.ym2151_suppress_slot3_channel5,
            context.ym2151_suppress_slot4_channel6, context.ym2151_suppress_slot5_channel7,
            context.sound_command_observer);
        if (context.ym2151_muted_channels != 0) {
            std::cout << "YM2151 muted channels:";
            for (unsigned channel = 0; channel < 8; ++channel)
                if ((context.ym2151_muted_channels & (1U << channel)) != 0)
                    std::cout << ' ' << (channel + 1);
            std::cout << '\n';
        }
        if (context.ym2151_suppress_slot0_channel2)
            std::cout << "YM2151: suppressing voice slot 0 writes routed to channel 2\n";
        if (context.ym2151_suppress_slot3_channel5)
            std::cout << "YM2151: suppressing voice slot 3 writes routed to channel 5\n";
        if (context.ym2151_suppress_slot4_channel6)
            std::cout << "YM2151: suppressing voice slot 4 writes routed to channel 6\n";
        if (context.ym2151_suppress_slot5_channel7)
            std::cout << "YM2151: suppressing voice slot 5 writes routed to channel 7\n";
        std::cout << "Audio path: System 16B Z80 AOT + interpreter fallback + YM2151/YMFM + uPD7759 slave ADPCM/NMI path enabled"
                  << (context.audio_stereo_spread ? " (alternating-channel stereo spread)\n" : " (game-programmed pan)\n");
        runtime.sound_owner = sound;
        runtime.sound_active = [sound] { return static_cast<bool>(sound); };
        runtime.advance_sound = [sound](std::uint64_t cycles) { sound->advance_to_main_cycles(cycles); };
        runtime.report_sound = [sound] {
            std::cout << "System 16B sound: Z80 cycles=" << sound->z80_cycles()
                      << " instructions=" << sound->z80_instructions() << " pc=0x" << std::hex << sound->z80_pc()
                      << std::dec << "\nZ80 execution coverage: AOT instructions=" << sound->z80_aot_instructions()
                      << " fallback instructions=" << sound->z80_fallback_instructions()
                      << "; YM2151 writes=" << sound->ym2151_writes() << " key-ons=" << sound->ym2151_key_ons()
                      << " suppressed slot0/channel2 writes=" << sound->ym2151_suppressed_voice_writes()
                      << " pan-overrides=" << sound->ym2151_pan_overrides() << " pan=";
            for (std::size_t i = 0; i < sound->ym2151_pan_by_channel().size(); ++i) {
                if (i) std::cout << ',';
                std::cout << std::hex << static_cast<unsigned>(sound->ym2151_pan_by_channel()[i]);
            }
            std::cout << std::dec
                      << "; uPD7759 FIFO writes=" << sound->upd7759_writes()
                      << " last-byte=0x" << std::hex << static_cast<unsigned>(sound->upd7759_last_byte())
                      << " NMI-edges=" << std::dec << sound->upd7759_nmi_edges()
                      << " serviced=" << sound->z80_nmi_serviced()
                      << "; samples=" << sound->audio_samples() << " peak=" << sound->audio_peak()
                      << " LR-difference-peak=" << sound->stereo_difference_peak() << '\n';
        };
    }
    const auto start = std::chrono::steady_clock::now();
    runtime.decoded_tiles = decode_sega_tiles_3bpp(context.tile_rom);
    runtime.tile_decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    runtime.bus = std::make_unique<System16BAddressSpace>(context.maincpu_rom,
        &context.video_signals, &context.input_state, config.rom_board);
    auto& bus = static_cast<System16BAddressSpace&>(*runtime.bus);
    if (context.profile.fd1094_key_region_size != 0) {
        const auto key = load_fd1094_key_roms(context.profile, context.rom_path);
        auto cpu_state = std::make_shared<Fd1094RuntimeState>();
        cpu_state->cipher = std::make_shared<multiarcaderecomp::Fd1094>(key);
        cpu_state->musashi_binding = std::make_unique<Fd1094MusashiBinding>(
            *cpu_state->cipher, bus, context.maincpu_rom, context.invalidate_aot);
        runtime.cpu_variant_owner = cpu_state;
        runtime.stateful_opcode_transform = true;
        runtime.transform_maincpu_opcode = [cpu_state](std::uint32_t address,
            std::uint16_t encrypted, bool vector_fetch) {
            return cpu_state->cipher->decrypt_opcode(address, encrypted, vector_fetch);
        };
        runtime.on_maincpu_interrupt_accepted = [cpu_state, invalidate = context.invalidate_aot] {
            cpu_state->cipher->enter_irq();
            if (invalidate) invalidate();
        };
    }
    bus.set_open_bus_pc_provider(context.pc_provider);
    std::shared_ptr<chips::SegaI8751> mcu;
    auto maincpu_stall_until = std::make_shared<std::uint64_t>(0);
    if (context.profile.mcu_region_size != 0) {
        const auto firmware = load_mcu_roms(context.profile, context.rom_path);
        chips::SegaI8751::Bus mcu_bus;
        mcu_bus.read_external = [&bus](std::uint16_t address) {
            return bus.mapper().read(static_cast<std::uint8_t>(address));
        };
        mcu_bus.write_external = [&bus](std::uint16_t address, std::uint8_t value) {
            bus.mapper().write(static_cast<std::uint8_t>(address), value);
        };
        mcu_bus.read_port1 = [&context] { return context.input_state.service; };
        mcu_bus.write_port1 = [&context, maincpu_stall_until](std::uint8_t) {
            *maincpu_stall_until = std::max(*maincpu_stall_until,
                context.cycle_provider() + 20'000U);
        };
        mcu = std::make_shared<chips::SegaI8751>(firmware, std::move(mcu_bus));
        bus.set_mapper_mcu_irq_callback([mcu](bool asserted) { mcu->set_irq1(asserted); });
        runtime.signal_vblank = [mcu] { mcu->set_vblank(true); };
        runtime.advance_devices_to_cycles = [mcu, maincpu_clock = context.registration.maincpu_clock_hz](std::uint64_t cycles) {
            mcu->advance_to_host_cycle(cycles, maincpu_clock);
        };
        runtime.maincpu_stall_until_cycles = [maincpu_stall_until] { return *maincpu_stall_until; };
        std::cout << "System 16B I8751 firmware active: " << firmware.size() << " bytes\n";
    }
    if (sound) {
        const auto maincpu_cycle = context.cycle_provider;
        const auto maincpu_pc = context.pc_provider;
        bus.mapper().set_sound_callback([sound, maincpu_cycle, maincpu_pc](std::uint8_t value) {
            sound->write_sound_latch(value,
                maincpu_cycle ? maincpu_cycle() : 0,
                maincpu_pc ? maincpu_pc() : 0);
        });
        if (sound->sound_trace_enabled()) {
            runtime.trace_inputs = [sound](std::uint8_t service, std::uint8_t p1, std::uint8_t p2) {
                sound->trace_inputs(service, p1, p2);
            };
        }
    }
    runtime.report_devices = [mcu, &bus,
        inputs = &context.input_state, video_signals = &context.video_signals] {
        if (mcu) {
            std::cout << "I8751: PC=0x" << std::hex << mcu->pc() << " IE=0x"
                      << static_cast<unsigned>(mcu->interrupt_enable()) << " TCON=0x"
                      << static_cast<unsigned>(mcu->timer_control()) << std::dec
                      << " machine-cycles=" << mcu->executed_machine_cycles()
                      << " instructions=" << mcu->instruction_count() << '\n';
        }
        const auto registers = bus.mapper().registers();
        std::cout << "315-5195 mapper registers:";
        for (unsigned offset = 0x10; offset < 0x20; ++offset)
            std::cout << " " << std::hex << offset << "="
                      << static_cast<unsigned>(registers[offset]);
        std::cout << std::dec << '\n';
        for (const auto& mapping : bus.mapper().mappings())
            std::cout << "  region " << mapping.region << " -> 0x" << std::hex << mapping.start
                      << "-0x" << mapping.end << " mirror=0x" << mapping.mirror << std::dec << '\n';
        std::cout << "System 16B inputs: SERVICE=0x" << std::hex << static_cast<unsigned>(inputs->service)
                  << " P1=0x" << static_cast<unsigned>(inputs->p1)
                  << " P2=0x" << static_cast<unsigned>(inputs->p2)
                  << " DSW1=0x" << static_cast<unsigned>(inputs->dsw1)
                  << " DSW2=0x" << static_cast<unsigned>(inputs->dsw2) << std::dec
                  << " reads SERVICE/P1/P2=" << inputs->service_reads << '/' << inputs->p1_reads << '/' << inputs->p2_reads
                  << " pressed=" << inputs->service_pressed_reads << '/' << inputs->p1_pressed_reads << '/' << inputs->p2_pressed_reads
                  << '\n';
        const auto textram = bus.memory("textram");
        const auto text_word = [&](std::size_t offset) {
            if (offset + 1 >= textram.size()) return 0U;
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        const auto& banks = bus.tile_banks();
        std::cout << "System 16B video: display=" << video_signals->display_enable
                  << " flip=" << video_signals->flip
                  << " tile-banks=" << static_cast<unsigned>(banks[0]) << ',' << static_cast<unsigned>(banks[1])
                  << " pages bg/fg=0x" << std::hex << text_word(0xe82) << "/0x" << text_word(0xe80)
                  << " scroll fg/bg x=0x" << text_word(0xe98) << "/0x" << text_word(0xe9a)
                  << " y=0x" << text_word(0xe90) << "/0x" << text_word(0xe92) << std::dec << '\n';
        const auto& writes = bus.video_write_stats();
        std::cout << "System 16B video RAM writes: palette=" << writes.palette
                  << " tile=" << writes.tile << " text=" << writes.text
                  << " sprite=" << writes.sprite << " tile-bank=" << writes.tile_bank << '\n';
    };
    runtime.video = make_system16b_video_renderer(runtime.decoded_tiles, context.sprite_rom,
        *runtime.bus, context.video_signals, context.video_backend, context.compare_cpu_video);
    runtime.report_board_video_state = [bus = runtime.bus.get()](std::ostream& output) {
        const auto textram = bus->memory("textram");
        const auto word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        output << "Tilemap regs: page-bg=0x" << std::hex << word(0xe82)
               << " page-fg=0x" << word(0xe80)
               << " x-fg/bg=0x" << word(0xe98) << "/0x" << word(0xe9a)
               << " y-fg/bg=0x" << word(0xe90) << "/0x" << word(0xe92)
               << std::dec << '\n';
    };
    runtime.write_board_video_state = [bus = runtime.bus.get()](
        const std::filesystem::path&, std::ostream& info) {
        const auto textram = bus->memory("textram");
        const auto word = [&](std::size_t offset) {
            return static_cast<unsigned>((textram[offset] << 8) | textram[offset + 1]);
        };
        info << "page_bg=0x" << std::hex << word(0xe82) << '\n'
             << "page_fg=0x" << word(0xe80) << '\n'
             << "x_fg=0x" << word(0xe98) << '\n'
             << "x_bg=0x" << word(0xe9a) << '\n'
             << "y_fg=0x" << word(0xe90) << '\n'
             << "y_bg=0x" << word(0xe92) << '\n';
    };
    return runtime;
}

} // namespace multiarcaderecomp::sega

extern "C" unsigned multiarcaderecomp_runtime_abi() { return 1; }
extern "C" unsigned multiarcaderecomp_runtime_revision() { return 18; }
extern "C" multiarcaderecomp::sega::BoardRuntimeHardware* multiarcaderecomp_create_runtime(
    const multiarcaderecomp::sega::BoardRuntimeContext* context) {
    return context ? new multiarcaderecomp::sega::BoardRuntimeHardware(
        multiarcaderecomp::sega::make_system16b_runtime(*context)) : nullptr;
}
extern "C" void multiarcaderecomp_destroy_runtime(multiarcaderecomp::sega::BoardRuntimeHardware* runtime) {
    delete runtime;
}
extern "C" bool multiarcaderecomp_runtime_memory_map(const multiarcaderecomp::GameProfile* profile,
    const std::uint8_t* rom, std::size_t size,
    void (*visit)(void*, const char*, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool), void* user) {
    if (!profile || (!rom && size) || !visit) return false;
    const auto& config = std::get<multiarcaderecomp::System16BConfiguration>(profile->board_configuration);
    multiarcaderecomp::System16BAddressSpace bus(std::span<const std::uint8_t>(rom, size), nullptr, nullptr, config.rom_board);
    for (const auto& region : bus.regions())
        visit(user, region.name.c_str(), region.start, region.size, region.mirror_mask, region.writable, region.device);
    return true;
}

extern "C" std::vector<std::uint8_t> multiarcaderecomp_runtime_decode_tiles(
    const std::uint8_t* data, std::size_t size) {
    if (!data && size) throw std::invalid_argument("null tile ROM data");
    return multiarcaderecomp::decode_sega_tiles_3bpp(std::span<const std::uint8_t>(data, size));
}

extern "C" std::vector<std::uint8_t> multiarcaderecomp_runtime_decrypt_initial_program(
    const multiarcaderecomp::GameProfile* profile, const char* rom_path,
    const std::uint8_t* encrypted, std::size_t size) {
    if (!profile || !rom_path || (!encrypted && size))
        throw std::invalid_argument("invalid FD1094 initial-program request");
    const auto key = multiarcaderecomp::load_fd1094_key_roms(*profile, rom_path);
    multiarcaderecomp::Fd1094 cipher(key);
    std::vector<std::uint8_t> opcodes(size);
    cipher.decrypt_region(std::span<const std::uint8_t>(encrypted, size), opcodes, cipher.state());
    return opcodes;
}
