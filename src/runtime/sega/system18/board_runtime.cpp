#include "runtime/sega/board_runtime.hpp"

#include "boards/sega/system18/system18_bus.hpp"
#include "boards/sega/system18/system18_sound.hpp"
#include "boards/sega/system18/video_session.hpp"
#include "boards/sega/common/graphics_decode.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "chips/sega_i8751/sega_i8751.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <fstream>
#include <stdexcept>

namespace multiarcaderecomp::sega {

BoardRuntimeHardware make_system18_runtime(const BoardRuntimeContext& context) {
    const auto& config = std::get<System18Configuration>(context.profile.board_configuration);
    BoardRuntimeHardware runtime;
    runtime.supports_video_profile = false;
    if (context.profile.default_dsw1) context.input_state.dsw1 = *context.profile.default_dsw1;
    if (context.profile.default_dsw2) context.input_state.dsw2 = *context.profile.default_dsw2;
    if (context.dsw1_override) context.input_state.dsw1 = *context.dsw1_override;
    if (context.dsw2_override) context.input_state.dsw2 = *context.dsw2_override;
    std::shared_ptr<System18Sound> sound;
    const auto sound_roms = load_soundcpu_roms(context.profile, context.rom_path);
    std::cout << "Validated System 18 sound ROM region: 0x" << std::hex << sound_roms.size() << std::dec << " bytes\n";
    sound = std::make_shared<System18Sound>(sound_roms,
        context.audio_volume, context.audio_stereo_spread, context.audio_sink);
    std::cout << "System 18 sound devices are emulated (Z80, dual YM3438, VDP PSG, RF5C68); playback "
              << (context.audio_sink ? "enabled" : "muted")
              << (context.audio_stereo_spread ? " with alternating YM stereo spread\n" : " with game-programmed YM pan\n");
    runtime.sound_owner = sound;
    runtime.sound_active = [] { return true; };
    runtime.advance_sound = [sound](std::uint64_t cycles) { sound->advance_to_main_cycles(cycles); };
    runtime.report_sound = [sound] {
        std::cout << "System 18 sound: Z80 cycles=" << sound->z80_cycles()
                  << "\nZ80 execution coverage: AOT instructions=" << sound->z80_aot_instructions()
                  << " fallback instructions=" << sound->z80_fallback_instructions()
                  << "; YM3438 writes=" << sound->ym_writes(0) << '/' << sound->ym_writes(1)
                  << " FM peaks=" << sound->ym_peak(0) << '/' << sound->ym_peak(1)
                  << "; VDP PSG writes=" << sound->psg_writes()
                  << "; RF5C68 RAM writes=" << sound->pcm_memory_writes()
                  << " enabled channels=0x" << std::hex << static_cast<unsigned>(sound->pcm_enabled_channels())
                  << std::dec << " PCM peak=" << sound->pcm_peak()
                  << "; generated samples=" << sound->audio_samples() << " peak=" << sound->audio_peak() << '\n';
    };
    const auto start = std::chrono::steady_clock::now();
    runtime.decoded_tiles = decode_sega_tiles_3bpp(context.tile_rom);
    runtime.tile_decode_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    runtime.bus = std::make_unique<System18AddressSpace>(context.maincpu_rom,
        &context.input_state, &context.video_signals, System18AddressSpace::Devices{}, config.rom_board);
    auto& bus = static_cast<System18AddressSpace&>(*runtime.bus);
    bus.vdp().set_psg_write_callback([sound](std::uint8_t value) { sound->write_vdp_psg(value); });
    std::shared_ptr<chips::SegaI8751> mcu;
    if (context.profile.mcu_region_size != 0) {
        // The board routes vblank to I8751 INT0. The MCU firmware should
        // request the main CPU IRQ through mapper register 4, but until that
        // path is confirmed, keep the 68000 vblank IRQ enabled as a fallback
        // so games do not remain in their IRQ wait loop without video.
        runtime.maincpu_vblank_irq_level = 4;
        auto maincpu_reset_asserted = std::make_shared<bool>(false);
        auto pending_mapper_irq = std::make_shared<unsigned>(0);
        auto mapper_irq_requests = std::make_shared<std::uint64_t>(0);
        bus.set_mapper_irq_callback([pending_mapper_irq, mapper_irq_requests](unsigned level) {
            *pending_mapper_irq = level;
            if (level != 0) ++*mapper_irq_requests;
        });
        runtime.pending_maincpu_irq_level = [pending_mapper_irq] { return *pending_mapper_irq; };
        runtime.acknowledge_maincpu_irq = [pending_mapper_irq](unsigned level) {
            if (*pending_mapper_irq == level) *pending_mapper_irq = 0;
        };
        bus.set_mapper_reset_callback([maincpu_reset_asserted](bool asserted) {
            *maincpu_reset_asserted = asserted;
        });
        runtime.maincpu_reset_asserted = [maincpu_reset_asserted] { return *maincpu_reset_asserted; };
        runtime.maincpu_stall_until_cycles = [maincpu_reset_asserted] {
            return *maincpu_reset_asserted ? std::numeric_limits<std::uint64_t>::max() : 0;
        };
        const auto firmware = load_mcu_roms(context.profile, context.rom_path);
        chips::SegaI8751::Bus mcu_bus;
        mcu_bus.read_external = [&bus](std::uint16_t address) {
            return bus.mapper().read(static_cast<std::uint8_t>(address));
        };
        mcu_bus.write_external = [&bus](std::uint16_t address, std::uint8_t value) {
            bus.mapper().write(static_cast<std::uint8_t>(address), value);
        };
        mcu = std::make_shared<chips::SegaI8751>(firmware, std::move(mcu_bus));
        bus.set_mapper_mcu_irq_callback([mcu](bool asserted) { mcu->set_irq1(asserted); });
        runtime.signal_vblank = [mcu] { mcu->set_vblank(true); };
        runtime.report_devices = [mcu, &bus, pending_mapper_irq, mapper_irq_requests,
            maincpu_reset_asserted] {
            std::cout << "I8751: PC=0x" << std::hex << mcu->pc() << " IE=0x"
                      << static_cast<unsigned>(mcu->interrupt_enable()) << " TCON=0x"
                      << static_cast<unsigned>(mcu->timer_control()) << std::dec
                      << " machine-cycles=" << mcu->executed_machine_cycles()
                      << " instructions=" << mcu->instruction_count() << '\n';
            const auto mapper = bus.mapper().registers();
            std::cout << "315-5195 control=0x" << std::hex
                      << static_cast<unsigned>(mapper[0x02])
                      << " sound-reply=0x" << static_cast<unsigned>(mapper[0x03])
                      << " irq-control=0x" << static_cast<unsigned>(mapper[0x04])
                      << " irq-requests=" << std::dec << *mapper_irq_requests
                      << " irq-pending=" << *pending_mapper_irq
                      << " reset=" << (*maincpu_reset_asserted ? "asserted" : "released")
                      << std::dec << '\n';
        };
        std::cout << "System 18 I8751 firmware active: " << firmware.size() << " bytes\n";
    }
    bus.set_vdp_cycle_provider(context.cycle_provider, context.registration.maincpu_clock_hz,
        context.registration.refresh_rate_num, context.registration.refresh_rate_den);
    struct VdpScanlineClock {
        std::uint64_t next = 0;
        std::uint64_t remainder = 0;
        std::uint32_t line = 0;
    };
    auto scanline_clock = std::make_shared<VdpScanlineClock>();
    bus.vdp().set_scanline(0);
    const std::uint64_t scanline_denominator = context.registration.refresh_rate_num * 262U;
    const std::uint64_t scanline_numerator_base = context.registration.maincpu_clock_hz
        * context.registration.refresh_rate_den;
    const auto schedule_scanline = [scanline_clock, scanline_numerator_base, scanline_denominator] {
        const std::uint64_t numerator = scanline_numerator_base + scanline_clock->remainder;
        scanline_clock->next += numerator / scanline_denominator;
        scanline_clock->remainder = numerator % scanline_denominator;
    };
    schedule_scanline();
    runtime.next_device_event = [scanline_clock] { return scanline_clock->next; };
    runtime.advance_devices_to_cycles = [&bus, scanline_clock, schedule_scanline, mcu,
        maincpu_clock = context.registration.maincpu_clock_hz](std::uint64_t cycles) {
        while (cycles >= scanline_clock->next) {
            scanline_clock->line = (scanline_clock->line + 1U) % 262U;
            bus.vdp().set_scanline(scanline_clock->line);
            schedule_scanline();
        }
        if (mcu) mcu->advance_to_host_cycle(cycles, maincpu_clock);
    };
    runtime.write_board_video_state = [&bus](const std::filesystem::path& directory, std::ostream& info) {
        const auto write_bytes = [](const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error("could not create System 18 video-state dump: " + path.string());
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("failed writing System 18 video-state dump: " + path.string());
        };
        const auto& vdp = bus.vdp();
        write_bytes(directory / "vdp_vram_be.bin", std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(vdp.vram().data()), vdp.vram().size() * sizeof(std::uint16_t)));
        write_bytes(directory / "vdp_cram_be.bin", std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(vdp.cram().data()), vdp.cram().size() * sizeof(std::uint16_t)));
        write_bytes(directory / "vdp_vsram_be.bin", std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(vdp.vsram().data()), vdp.vsram().size() * sizeof(std::uint16_t)));
        std::ofstream regs(directory / "vdp_regs.bin", std::ios::binary);
        regs.write(reinterpret_cast<const char*>(vdp.registers().data()), vdp.registers().size());
        info << "vdp_enabled=" << bus.vdp_enabled() << '\n'
             << "vdp_mixing=0x" << std::hex << static_cast<unsigned>(bus.vdp_mixing()) << '\n'
             << "vdp_scanline=" << std::dec << vdp.scanline() << '\n'
             << "vdp_hpos=" << vdp.horizontal_position() << '\n';
        const auto& events = bus.vdp_signal_events();
        info << "vdp_signal_event_count=" << events.size() << '\n';
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            info << "vdp_signal_event_" << i << "="
                 << (event.signal == System18AddressSpace::VdpSignalEvent::Signal::Enable ? "enable" : "mixing")
                 << ":0x" << std::hex << static_cast<unsigned>(event.value)
                 << ",cycles:" << std::dec << event.cycles << '\n';
        }
        for (unsigned i = 0; i < vdp.registers().size(); ++i)
            info << "vdp_reg_" << std::dec << i << "=0x" << std::hex << static_cast<unsigned>(vdp.reg(i)) << '\n';
        for (const auto& mapping : bus.mapper().mappings())
            info << "map_region_" << std::dec << mapping.region << "=target:" << static_cast<unsigned>(mapping.target)
                 << ",start:0x" << std::hex << mapping.start << ",end:0x" << mapping.end
                 << ",mirror:0x" << mapping.mirror << ",mask:0x" << mapping.size_mask << '\n';
    };
    if (sound) {
        bus.mapper().set_sound_callback([sound](std::uint8_t value) {
            sound->write_sound_latch(value);
        });
        sound->set_sound_reply_callback([&bus](std::uint8_t value) {
            bus.set_sound_reply(value);
        });
    }
    runtime.video = make_system18_video_renderer(runtime.decoded_tiles, context.sprite_rom,
        *runtime.bus, context.video_signals);
    return runtime;
}

} // namespace multiarcaderecomp::sega

extern "C" unsigned multiarcaderecomp_runtime_abi() { return 1; }
extern "C" unsigned multiarcaderecomp_runtime_revision() { return 18; }
extern "C" multiarcaderecomp::sega::BoardRuntimeHardware* multiarcaderecomp_create_runtime(
    const multiarcaderecomp::sega::BoardRuntimeContext* context) {
    return context ? new multiarcaderecomp::sega::BoardRuntimeHardware(
        multiarcaderecomp::sega::make_system18_runtime(*context)) : nullptr;
}
extern "C" void multiarcaderecomp_destroy_runtime(multiarcaderecomp::sega::BoardRuntimeHardware* runtime) {
    delete runtime;
}
extern "C" bool multiarcaderecomp_runtime_memory_map(const multiarcaderecomp::GameProfile* profile,
    const std::uint8_t* rom, std::size_t size,
    void (*visit)(void*, const char*, std::uint32_t, std::uint32_t, std::uint32_t, bool, bool), void* user) {
    if (!profile || (!rom && size) || !visit) return false;
    const auto& config = std::get<multiarcaderecomp::System18Configuration>(profile->board_configuration);
    multiarcaderecomp::System18AddressSpace bus(std::span<const std::uint8_t>(rom, size), nullptr, nullptr,
        multiarcaderecomp::System18AddressSpace::Devices{}, config.rom_board);
    for (const auto& region : bus.regions())
        visit(user, region.name.c_str(), region.start, region.size, region.mirror_mask, region.writable, region.device);
    return true;
}

extern "C" std::vector<std::uint8_t> multiarcaderecomp_runtime_decode_tiles(
    const std::uint8_t* data, std::size_t size) {
    if (!data && size) throw std::invalid_argument("null tile ROM data");
    return multiarcaderecomp::decode_sega_tiles_3bpp(std::span<const std::uint8_t>(data, size));
}
