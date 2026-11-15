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
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#include <SDL3/SDL.h>
#endif
#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
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

namespace mamerecomp::app::machine_runners {
using mamerecomp::app::read_binary_file;
using mamerecomp::app::run_child_process;
using InputEvent = mamerecomp::sega::InputEvent;

int run_konami_tmnt_machine(const std::filesystem::path& source_root,
                            const std::optional<std::filesystem::path>& explicit_zip,
                            bool build_mode, bool window,
                            std::uint64_t frames,
                            const std::optional<std::uint64_t>& cycles,
                            const std::optional<std::filesystem::path>& frame_out,
                            bool audio_enabled, const std::string& jobs, bool force_rebuild) {
    if (build_mode && (frames || cycles || frame_out))
        throw std::runtime_error("--frames, --cycles, and --frame-out are run-only options");
    if (!build_mode && !window && !frames && !cycles)
        throw std::runtime_error("TMNT headless run requires --frames or --cycles");

    const auto zip = explicit_zip ? std::filesystem::absolute(*explicit_zip)
        : source_root.parent_path() / "mame/roms/tmnt.zip";
    auto roms = mamerecomp::konami::load_tmnt_romset(zip);
    std::cout << "Konami TMNT: validated MAME ROMs from " << zip << '\n';
    const auto build_root = source_root / "build";
    const auto generated_root = build_root / "generated/konami/tmnt";
    const auto m68k_output = generated_root / "m68k";
    const auto z80_output = generated_root / "z80";
    const auto module_path = build_root / "mamerecomp_konami_tmnt.so";
    if (build_mode) {
        const auto m68k_image = generated_root / "program.bin";
        const auto z80_image = generated_root / "audio.bin";
        const auto entry_file = generated_root / "m68k_entries.txt";
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
        write_image(z80_image, audio, 0x8000);
        {
            std::ofstream entries(entry_file);
            if (!entries) throw std::runtime_error("could not create 68000 AOT entry list");
            for (uint32_t vector = 0; vector < 64; ++vector) {
                const uint32_t offset = vector * 4;
                const uint32_t address = (uint32_t(program[offset]) << 24)
                    | (uint32_t(program[offset + 1]) << 16)
                    | (uint32_t(program[offset + 2]) << 8) | program[offset + 3];
                if (address < program.size() && !(address & 1))
                    entries << std::hex << address << '\n';
            }
        }
        std::filesystem::create_directories(m68k_output);
        std::filesystem::create_directories(z80_output);
        if (force_rebuild) {
            std::filesystem::remove_all(m68k_output);
            std::filesystem::remove_all(z80_output);
            std::filesystem::create_directories(m68k_output);
            std::filesystem::create_directories(z80_output);
        }
        const auto m68krecomp = build_root / "m68krecomp";
        if (!std::filesystem::is_regular_file(m68krecomp))
            throw std::runtime_error("m68krecomp tool not found; build the mamerecomp and m68krecomp targets first");
        const uint32_t reset_pc = (uint32_t(program[4]) << 24) | (uint32_t(program[5]) << 16)
            | (uint32_t(program[6]) << 8) | program[7];
        std::cout << "Generating TMNT 68000 AOT from the main program ROMs.\n" << std::flush;
        int status = run_child_process({m68krecomp.string(), "--raw", m68k_image.string(), "--base", "0",
            "--entry", std::to_string(reset_pc), "--entries", entry_file.string(),
            "--board", "generic", "--prefix", "tmnt_",
            "--out", m68k_output.string(), "--files", "16"});
        if (status != 0) return status;
        std::cout << "Generating TMNT Z80 AOT from the audio program ROM.\n" << std::flush;
        status = run_child_process({MAMERECOMP_PYTHON_EXECUTABLE, (source_root / "tools/z80/recomp.py").string(),
            "--program", z80_image.string(), "--out", z80_output.string(), "--precise-cycle-yield"});
        if (status != 0) return status;
        status = run_child_process({"cmake", "-S", source_root.string(), "-B", build_root.string(),
            "-DMAMERECOMP_KONAMI_TMNT_AOT_DIR=" + generated_root.string()});
        if (status != 0) return status;
        std::cout << "Building the TMNT 68000/Z80 AOT module.\n" << std::flush;
        return run_child_process({"cmake", "--build", build_root.string(), "--target",
            "mamerecomp_konami_tmnt_aot", "-j", jobs});
    }

    if (!std::filesystem::is_regular_file(module_path))
        throw std::runtime_error("TMNT AOT module is missing; run `mamerecomp build --machine=konami --board=tmnt --game=tmnt` first");
    void* module = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) throw std::runtime_error("could not load TMNT AOT module: " + std::string(dlerror()));
    struct ModuleGuard { void* handle; ~ModuleGuard() { if (handle) dlclose(handle); } } module_guard{module};
    using M68kTableFn = const ng_func_entry_t* (*)(size_t*);
    using Z80CodeFn = const mamerecomp::z80::Z80Code* (*)();
    auto get_m68k_table = reinterpret_cast<M68kTableFn>(dlsym(module, "mamerecomp_konami_tmnt_m68k_table"));
    auto get_z80_code = reinterpret_cast<Z80CodeFn>(dlsym(module, "mamerecomp_konami_tmnt_z80_code"));
    if (!get_m68k_table || !get_z80_code) throw std::runtime_error("TMNT AOT module is missing CPU tables");
    size_t m68k_entry_count = 0;
    const auto* m68k_entries = get_m68k_table(&m68k_entry_count);
    if (!m68k_entries || !m68k_entry_count) throw std::runtime_error("TMNT 68000 AOT table is empty");
    mamerecomp::z80::SetZ80Code(get_z80_code());

    mamerecomp::konami::TmntBoard board(std::move(roms));
    mamerecomp::konami::TmntMainCpu main_cpu(board, m68k_entries, m68k_entry_count);
    mamerecomp::konami::TmntSoundCpu sound_cpu(board);
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
    std::unique_ptr<mamerecomp::konami::TmntPresenter> presenter;
    if (window || audio_enabled)
        presenter = std::make_unique<mamerecomp::konami::TmntPresenter>(
            "Teenage Mutant Ninja Turtles - MameRecomp", window, audio_enabled,
            board.audio_sample_rate());
    if (window)
        std::cout << "Controls: P1 arrows + keypad 1/2/3, start 1; P2 WASD + J/K/L, start 2; coins 5-8.\n";
#else
    if (window || audio_enabled)
        throw std::runtime_error("TMNT live video/audio requires an SDL3-enabled build");
#endif
    // Use the clocks and raster in MAME's tmnt machine configuration.
    constexpr std::uint64_t pixel_clock_hz = 24'000'000 / 4;
    constexpr std::uint64_t main_clock_hz = 24'000'000 / 3;
    constexpr std::uint64_t pixels_per_line = 384;
    constexpr std::uint64_t lines_per_frame = 264;
    constexpr std::uint64_t cycles_per_line = main_clock_hz * pixels_per_line / pixel_clock_hz;
    constexpr std::uint64_t cycles_per_frame = cycles_per_line * lines_per_frame;
    constexpr auto frame_period = std::chrono::duration<double>(
        static_cast<double>(cycles_per_frame) / static_cast<double>(main_clock_hz));
    const std::uint64_t frame_count = frames ? frames : (cycles ? (*cycles + cycles_per_frame - 1) / cycles_per_frame : 0);
    const bool run_until_window_close = window && !frames && !cycles;
    std::uint64_t executed_frames = 0;
    std::uint64_t scheduled_main_cycles = 0;
    std::optional<mamerecomp::konami::TmntFrame> final_frame;
    auto next_frame_deadline = std::chrono::steady_clock::now();
    bool window_open = true;
    while (window_open && (run_until_window_close || executed_frames < frame_count)) {
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
        if (presenter && window && !presenter->pump_events(board)) break;
#endif
        std::vector<std::int16_t> frame_audio;
        for (unsigned line = 0; line < lines_per_frame; ++line) {
            if (line == 240) board.video_vblank();
            scheduled_main_cycles += cycles_per_line;
            main_cpu.execute_until(scheduled_main_cycles);
            const uint64_t sound_timestamp = scheduled_main_cycles * 3'579'545ull / main_clock_hz;
            sound_cpu.run_until(sound_timestamp);
            auto samples = board.take_audio_samples();
            frame_audio.insert(frame_audio.end(), samples.begin(), samples.end());
        }
        ++executed_frames;
        final_frame = board.render_frame();
#ifdef MAMERECOMP_HAS_SDL3_PRESENTER
        if (presenter) {
            if (audio_enabled) presenter->queue_audio(frame_audio);
            if (window) presenter->present(board.frame_rgb(*final_frame));
            next_frame_deadline += std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_period);
            std::this_thread::sleep_until(next_frame_deadline);
            const auto now = std::chrono::steady_clock::now();
            if (now - next_frame_deadline > std::chrono::duration_cast<std::chrono::steady_clock::duration>(frame_period))
                next_frame_deadline = now;
        }
#endif
    }

    const auto frame = final_frame.value_or(board.render_frame());
    if (frame_out) {
        if (!frame_out->parent_path().empty()) std::filesystem::create_directories(frame_out->parent_path());
        const auto rgb = board.frame_rgb(frame);
        std::ofstream output(*frame_out, std::ios::binary);
        if (!output) throw std::runtime_error("could not create frame output " + frame_out->string());
        output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!output) throw std::runtime_error("failed writing frame output " + frame_out->string());
        std::cout << "Wrote " << frame.width << 'x' << frame.height << " framebuffer to " << *frame_out << '\n';
    }
    std::cout << "Executed " << main_cpu.cycles() << " 68000 cycles (raster budget="
              << executed_frames * cycles_per_frame << "); PC=0x"
              << std::hex << main_cpu.pc() << ", Z80 PC=0x" << sound_cpu.pc()
              << std::dec << ", Z80 cycles=" << sound_cpu.cycles() << ".\n";
    std::cout << "68000 execution coverage: AOT instructions=" << main_cpu.aot_instructions()
              << " Musashi fallback instructions=" << main_cpu.fallback_instructions() << '\n'
              << "Z80 execution coverage: AOT instructions=" << sound_cpu.aot_instructions()
              << " fallback instructions=" << sound_cpu.fallback_instructions() << '\n';
    return 0;
}

} // namespace mamerecomp::app::machine_runners
