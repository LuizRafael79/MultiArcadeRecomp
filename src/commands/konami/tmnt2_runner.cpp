#include "commands/machine_runner_support.hpp"
#include "commands/machine_runners.hpp"
#include "konami/tmnt_machine.hpp"
#include "konami/tmnt_presenter.hpp"
#include "video/sdl_video_presenter.hpp"
#include "konami/tmnt2_machine.hpp"
#include "seibu/toki_machine.hpp"
#include "seibu/toki_presenter.hpp"
#include "zn_runtime.hpp"
#include "native_z80.h"
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#include <SDL3/SDL.h>
#endif
#ifdef MULTIARCADERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <dlfcn.h>

namespace multiarcaderecomp::app::machine_runners {
using multiarcaderecomp::app::read_binary_file;
using multiarcaderecomp::app::run_child_process;
using InputEvent = multiarcaderecomp::sega::InputEvent;

int run_konami_tmnt2_machine(const std::filesystem::path& source_root,
                             const std::string& game,
                             const std::optional<std::filesystem::path>& explicit_zip,
                             bool build_mode, bool window,
                             std::uint64_t frames,
                             const std::optional<std::uint64_t>& cycles,
                             const std::optional<std::filesystem::path>& frame_out,
                             const std::optional<std::filesystem::path>& frame_sequence_dir,
                             std::uint64_t frame_sequence_step,
                             bool audio_enabled, const std::vector<InputEvent>& input_events,
                             const std::string& jobs, bool force_rebuild) {
    if (game != "tmnt2" && game != "ssriders")
        throw std::runtime_error("Konami TMNT2 board supports --game=tmnt2 or --game=ssriders");
    if (build_mode && (frames || cycles || frame_out || frame_sequence_dir))
        throw std::runtime_error("frame and cycle capture options are run-only options");
    if (frame_sequence_step == 0) throw std::runtime_error("--frame-sequence-step must be positive");
    if (!build_mode && !window && !frames && !cycles)
        throw std::runtime_error("Konami headless run requires --frames or --cycles");

    const auto zip = explicit_zip ? std::filesystem::absolute(*explicit_zip)
        : source_root.parent_path() / "mame/roms" / (game + ".zip");
    auto roms = multiarcaderecomp::konami::load_tmnt2_romset(game, zip);
    std::cout << "Konami " << game << ": validated MAME ROMs from " << zip << '\n';
    const auto build_root = source_root / "build";
    const auto generated_root = build_root / "generated/konami/tmnt2" / game;
    const auto m68k_output = generated_root / "m68k";
    const auto z80_output = generated_root / "z80";
    const auto module_path = build_root / ("multiarcaderecomp_konami_" + game + ".so");
    if (build_mode) {
        if (audio_enabled || window) throw std::runtime_error("TMNT2-family build does not accept run-only audio/video options");
        const auto m68k_image = generated_root / "program.bin";
        const auto z80_image = generated_root / "audio.bin";
        const auto m68k_entries = generated_root / "m68k_entries.txt";
        const auto z80_entries = generated_root / "z80_entries.txt";
        std::filesystem::create_directories(generated_root);
        const auto write_image = [](const std::filesystem::path& path,
                                    const std::vector<uint8_t>& bytes, size_t length) {
            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error("could not create AOT input " + path.string());
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(length));
            if (!output) throw std::runtime_error("failed writing AOT input " + path.string());
        };
        const auto& program = roms.at("maincpu");
        const auto& audio = roms.at("audiocpu");
        write_image(m68k_image, program, program.size());
        write_image(z80_image, audio, 0xf000);
        {
            std::ofstream entries(m68k_entries);
            if (!entries) throw std::runtime_error("could not create TMNT2-family 68000 entry list");
            for (uint32_t vector = 0; vector < 64; ++vector) {
                const uint32_t offset = vector * 4;
                const uint32_t address = (uint32_t(program[offset]) << 24)
                    | (uint32_t(program[offset + 1]) << 16)
                    | (uint32_t(program[offset + 2]) << 8) | program[offset + 3];
                if (address < program.size() && !(address & 1))
                    entries << std::hex << address << '\n';
            }
        }
        { std::ofstream entries(z80_entries); entries << "0066\n"; }
        if (force_rebuild) {
            std::filesystem::remove_all(m68k_output);
            std::filesystem::remove_all(z80_output);
        }
        std::filesystem::create_directories(m68k_output);
        std::filesystem::create_directories(z80_output);
        const auto m68krecomp = build_root / "m68krecomp";
        if (!std::filesystem::is_regular_file(m68krecomp))
            throw std::runtime_error("m68krecomp tool not found; build the multiarcaderecomp and m68krecomp targets first");
        const uint32_t reset_pc = (uint32_t(program[4]) << 24) | (uint32_t(program[5]) << 16)
            | (uint32_t(program[6]) << 8) | program[7];
        std::cout << "Generating " << game << " 68000 AOT from its program ROMs.\n" << std::flush;
        int status = run_child_process({m68krecomp.string(), "--raw", m68k_image.string(), "--base", "0",
            "--entry", std::to_string(reset_pc), "--entries", m68k_entries.string(), "--board", "generic",
            "--prefix", game + "_", "--out", m68k_output.string(), "--files", "16"});
        if (status != 0) return status;
        std::cout << "Generating " << game << " Z80 AOT from its audio program ROM.\n" << std::flush;
        status = run_child_process({MULTIARCADERECOMP_PYTHON_EXECUTABLE, (source_root / "tools/z80/recomp.py").string(),
            "--program", z80_image.string(), "--entries", z80_entries.string(), "--out", z80_output.string(),
            "--symbol-prefix", game + "_", "--precise-cycle-yield"});
        if (status != 0) return status;
        status = run_child_process({"cmake", "-S", source_root.string(), "-B", build_root.string(),
            "-DMULTIARCADERECOMP_KONAMI_TMNT2_AOT_DIR=" + generated_root.string(),
            "-DMULTIARCADERECOMP_KONAMI_TMNT2_GAME_ID=" + game});
        if (status != 0) return status;
        std::cout << "Building the " << game << " 68000/Z80 AOT module.\n" << std::flush;
        return run_child_process({"cmake", "--build", build_root.string(), "--target",
            "multiarcaderecomp_konami_tmnt2_aot", "-j", jobs});
    }

    if (!std::filesystem::is_regular_file(module_path))
        throw std::runtime_error(game + " AOT module is missing; run `multiarcaderecomp build --machine=konami --board=tmnt2 --game=" + game + "` first");
    void* module = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) throw std::runtime_error("could not load " + game + " AOT module: " + std::string(dlerror()));
    struct ModuleGuard { void* handle; ~ModuleGuard() { if (handle) dlclose(handle); } } module_guard{module};
    using M68kTableFn = const ng_func_entry_t* (*)(size_t*);
    using Z80CodeFn = const multiarcaderecomp::z80::Z80Code* (*)();
    auto get_m68k_table = reinterpret_cast<M68kTableFn>(dlsym(module, "multiarcaderecomp_konami_tmnt2_m68k_table"));
    auto get_z80_code = reinterpret_cast<Z80CodeFn>(dlsym(module, "multiarcaderecomp_konami_tmnt2_z80_code"));
    if (!get_m68k_table || !get_z80_code) throw std::runtime_error(game + " AOT module is missing CPU tables");
    size_t m68k_entry_count = 0;
    const auto* m68k_entries = get_m68k_table(&m68k_entry_count);
    if (!m68k_entries || !m68k_entry_count) throw std::runtime_error(game + " 68000 AOT table is empty");
    multiarcaderecomp::z80::SetZ80Code(get_z80_code());

    multiarcaderecomp::konami::Tmnt2Board board(game, std::move(roms));
    multiarcaderecomp::konami::Tmnt2MainCpu main_cpu(board, m68k_entries, m68k_entry_count);
    multiarcaderecomp::konami::Tmnt2SoundCpu sound_cpu(board);
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    std::unique_ptr<multiarcaderecomp::video::SdlVideoPresenter> video_presenter;
    if (window) video_presenter = std::make_unique<multiarcaderecomp::video::SdlVideoPresenter>(
        "Konami " + game + " - MultiArcadeRecomp", game == "tmnt2" ? 304u : 288u, 224u);
    if (video_presenter)
        std::cout << "Controls: P1 arrows + keypad 1/2/3, start 1; P2 WASD + J/K/L, start 2; coins 5-8; F2 service.\n";
    std::unique_ptr<multiarcaderecomp::konami::TmntPresenter> audio_output;
    if (audio_enabled)
        audio_output = std::make_unique<multiarcaderecomp::konami::TmntPresenter>(
            "Konami TMNT2 audio", false, true, board.audio_sample_rate());
#else
    if (window) throw std::runtime_error("Konami live video requires an SDL3-enabled build");
    if (audio_enabled)
        throw std::runtime_error("Konami audio output requires an SDL3-enabled build");
#endif
    constexpr std::uint64_t main_cycles_per_frame = 270'336;
    constexpr auto frame_period = std::chrono::duration<double>(
        static_cast<double>(main_cycles_per_frame) / 16'000'000.0);
    std::uint64_t scheduled_main_cycles = 0;
    std::uint64_t frame_index = 0;
    bool window_open = true;
    if (frame_sequence_dir) std::filesystem::create_directories(*frame_sequence_dir);
    auto next_audio_deadline = std::chrono::steady_clock::now();
    auto run_frame = [&] {
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (video_presenter && !video_presenter->pump_events()) {
            window_open = false;
            return;
        }
#endif
        std::array<std::uint8_t, 5> input_masks{0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (video_presenter) {
            const auto keys = video_presenter->input_state();
            input_masks[0] &= static_cast<std::uint8_t>(~keys.coins_pressed);
            for (size_t player = 0; player < keys.players_pressed.size(); ++player)
                input_masks[player + 1] &= static_cast<std::uint8_t>(~keys.players_pressed[player]);
        }
#endif
        for (const auto& event : input_events) {
            if (frame_index >= event.first_frame &&
                frame_index - event.first_frame < event.duration_frames) {
                const auto port = static_cast<size_t>(event.port);
                input_masks[port] &= event.active_low_mask;
            }
        }
        board.set_input(0, static_cast<std::uint16_t>(0xff00 | input_masks[0]));
        for (unsigned player = 0; player < 4; ++player)
            board.set_input(player + 1, static_cast<std::uint16_t>(0xff00 | input_masks[player + 1]));
        std::vector<std::int16_t> frame_audio;
        // The 16 MHz 68000 and 8 MHz Z80 share absolute time deadlines; the
        // latter is scheduled to half the main CPU clock as in MAME's config.
        constexpr std::uint64_t main_quantum = 8'192;
        for (unsigned i = 0; i < 30; ++i) {
            scheduled_main_cycles += main_quantum;
            main_cpu.execute_until(scheduled_main_cycles);
            sound_cpu.run_until(scheduled_main_cycles / 2);
            auto samples = board.take_audio_samples();
            frame_audio.insert(frame_audio.end(), samples.begin(), samples.end());
        }
        board.video_vblank();
        for (unsigned i = 0; i < 3; ++i) {
            scheduled_main_cycles += main_quantum;
            main_cpu.execute_until(scheduled_main_cycles);
            sound_cpu.run_until(scheduled_main_cycles / 2);
            auto samples = board.take_audio_samples();
            frame_audio.insert(frame_audio.end(), samples.begin(), samples.end());
        }
        ++frame_index;
        if (frame_sequence_dir
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            || video_presenter
#endif
        ) {
            const auto live_frame = board.render_frame();
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            if (video_presenter) {
                video_presenter->present(board.frame_rgb(live_frame));
                window_open = video_presenter->pump_events();
            }
#endif
            if (frame_sequence_dir && ((frame_index - 1) % frame_sequence_step) == 0) {
                auto path = *frame_sequence_dir / ("frame_" + std::to_string(frame_index - 1) + ".ppm");
                std::ofstream output(path, std::ios::binary);
                if (!output) throw std::runtime_error("could not create frame sequence image " + path.string());
                output << "P6\n" << live_frame.width << ' ' << live_frame.height << "\n255\n";
                const auto rgb = board.frame_rgb(live_frame);
                output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
                if (!output) throw std::runtime_error("failed writing frame sequence image " + path.string());
            }
        }
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (audio_output) {
            audio_output->queue_audio(frame_audio);
            next_audio_deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_period);
            std::this_thread::sleep_until(next_audio_deadline);
            const auto now = std::chrono::steady_clock::now();
            if (now - next_audio_deadline > std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_period))
                next_audio_deadline = now;
        }
#endif
    };
    if (window && !frames && !cycles) {
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        while (window_open) run_frame();
#endif
    } else if (frames) {
        for (std::uint64_t frame = 0; frame < frames && window_open; ++frame) run_frame();
    } else {
        const std::uint64_t count = *cycles / main_cycles_per_frame;
        for (std::uint64_t frame = 0; frame < count && window_open; ++frame) run_frame();
        const std::uint64_t remainder = *cycles % main_cycles_per_frame;
        if (remainder) {
            std::uint64_t left = remainder;
            while (left) {
                const auto quantum = std::min<std::uint64_t>(left, 8'192);
                scheduled_main_cycles += quantum;
                main_cpu.execute_until(scheduled_main_cycles);
                sound_cpu.run_until(scheduled_main_cycles / 2);
                (void)board.take_audio_samples();
                left -= quantum;
            }
        }
    }

    const auto frame = board.render_frame();
    const auto min_index = *std::min_element(frame.palette_indices.begin(), frame.palette_indices.end());
    const auto max_index = *std::max_element(frame.palette_indices.begin(), frame.palette_indices.end());
    if (frame_out) {
        if (!frame_out->parent_path().empty())
            std::filesystem::create_directories(frame_out->parent_path());
        const auto rgb = board.frame_rgb(frame);
        std::ofstream output(*frame_out, std::ios::binary);
        if (!output) throw std::runtime_error("could not create frame output " + frame_out->string());
        output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!output) throw std::runtime_error("failed writing frame output " + frame_out->string());
        std::cout << "Wrote " << frame.width << 'x' << frame.height << " framebuffer to " << *frame_out << '\n';
    }
    std::cout << "Executed " << main_cpu.cycles() << " 68000 cycles (raster budget="
              << (frames ? frames * main_cycles_per_frame : *cycles) << "); 68000 PC=0x" << std::hex << main_cpu.pc()
              << " D1=0x" << main_cpu.data_register(1)
              << " D3=0x" << main_cpu.data_register(3)
              << " D4=0x" << main_cpu.data_register(4)
              << " D6=0x" << main_cpu.data_register(6)
              << " D7=0x" << main_cpu.data_register(7)
              << " A4=0x" << main_cpu.address_register(4)
              << " A5=0x" << main_cpu.address_register(5)
              << " instruction=\"" << main_cpu.current_instruction() << "\", Z80 PC=0x"
              << sound_cpu.pc() << std::dec
              << ", SP=0x" << std::hex << sound_cpu.sp() << std::dec
              << ", Z80 cycles=" << sound_cpu.cycles() << ", NMI accepts=" << sound_cpu.nmi_count()
              << ", K053260 reply=0x" << std::hex << (board.main_read(0x5c0600, 0x00ff) & 0xff)
              << std::dec << "; indexed frame range=" << min_index << ".." << max_index << ".\n";
    std::cout << "68000 execution coverage: AOT instructions=" << main_cpu.aot_instructions()
              << " Musashi fallback instructions=" << main_cpu.fallback_instructions() << '\n'
              << "Z80 execution coverage: AOT instructions=" << sound_cpu.aot_instructions()
              << " fallback instructions=" << sound_cpu.fallback_instructions() << '\n';
    if (frames >= 60) std::cout << "Recent 68000 path:\n" << main_cpu.disassembly_before_pc();
    return 0;
}

} // namespace multiarcaderecomp::app::machine_runners
