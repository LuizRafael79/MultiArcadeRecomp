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

int run_seibu_toki_machine(const std::filesystem::path& source_root,
                           const std::string& game,
                           const std::optional<std::filesystem::path>& explicit_zip,
                           bool build_mode, bool window, std::uint64_t frames,
                           const std::optional<std::uint64_t>& cycles,
                           const std::optional<std::filesystem::path>& frame_out,
                           bool audio_enabled, const std::vector<InputEvent>& input_events,
                           const std::string& jobs, bool force_rebuild) {
    if (game != "toki") throw std::runtime_error("Seibu Toki adapter supports --game=toki");
    if (build_mode && (frames || cycles || frame_out))
        throw std::runtime_error("--frames, --cycles, and --frame-out are run-only options");
    if (!build_mode && !window && !frames && !cycles)
        throw std::runtime_error("Toki headless run requires --frames or --cycles");

    const auto zip = explicit_zip ? std::filesystem::absolute(*explicit_zip)
        : source_root.parent_path() / "mame/roms/toki.zip";
    auto roms = multiarcaderecomp::seibu::load_toki_romset(zip);
    std::cout << "Toki: validated MAME ROMs from " << zip << '\n';
    const auto build_root = source_root / "build";
    const auto generated_root = build_root / "generated/seibu/toki/toki/m68k";
    const auto z80_generated_root = build_root / "generated/seibu/toki/toki/z80";
    const auto module_path = build_root / "multiarcaderecomp_seibu_toki.so";
    if (build_mode) {
        std::filesystem::create_directories(generated_root);
        std::filesystem::create_directories(z80_generated_root);
        const auto image = generated_root / "program.bin";
        const auto entry_file = generated_root / "entries.txt";
        const auto& program = roms.at("maincpu");
        {
            std::ofstream output(image, std::ios::binary);
            if (!output) throw std::runtime_error("could not create Toki 68000 AOT image");
            output.write(reinterpret_cast<const char*>(program.data()), static_cast<std::streamsize>(program.size()));
            if (!output) throw std::runtime_error("failed writing Toki 68000 AOT image");
        }
        {
            std::ofstream entries(entry_file);
            if (!entries) throw std::runtime_error("could not create Toki 68000 entry list");
            for (uint32_t vector = 0; vector < 64; ++vector) {
                const auto offset = size_t(vector) * 4;
                const uint32_t address = (uint32_t(program[offset]) << 24)
                    | (uint32_t(program[offset + 1]) << 16)
                    | (uint32_t(program[offset + 2]) << 8) | program[offset + 3];
                if (address < program.size() && !(address & 1)) entries << std::hex << address << '\n';
            }
        }
        const auto m68krecomp = build_root / "m68krecomp";
        if (!std::filesystem::is_regular_file(m68krecomp))
            throw std::runtime_error("m68krecomp not found; build the multiarcaderecomp and m68krecomp targets first");
        const uint32_t reset_pc = (uint32_t(program[4]) << 24) | (uint32_t(program[5]) << 16)
            | (uint32_t(program[6]) << 8) | program[7];
        if (force_rebuild) std::filesystem::remove_all(generated_root);
        std::filesystem::create_directories(generated_root);
        std::cout << "Generating Toki 68000 AOT from its program ROMs.\n" << std::flush;
        int status = run_child_process({m68krecomp.string(), "--raw", image.string(), "--base", "0",
            "--entry", std::to_string(reset_pc), "--entries", entry_file.string(), "--board", "generic",
            "--prefix", "toki_", "--out", generated_root.string(), "--files", "8"});
        if (status != 0) return status;
        const auto& audio = roms.at("audiocpu");
        std::vector<uint8_t> z80_program(0x8000, 0xff);
        std::vector<uint8_t> z80_opcodes(0x8000, 0xff);
        std::vector<uint8_t> z80_data(0x8000, 0xff);
        for (uint16_t address = 0; address < 0x2000; ++address) {
            z80_program[address] = audio[address];
            z80_opcodes[address] = multiarcaderecomp::seibu::toki_sei80bu_opcode(address, audio[address]);
            z80_data[address] = multiarcaderecomp::seibu::toki_sei80bu_data(address, audio[address]);
        }
        const auto write_image = [](const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error("could not create Toki Z80 AOT image " + path.string());
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw std::runtime_error("failed writing Toki Z80 AOT image " + path.string());
        };
        const auto z80_program_path = z80_generated_root / "program.bin";
        const auto z80_opcode_path = z80_generated_root / "opcodes.bin";
        const auto z80_data_path = z80_generated_root / "data.bin";
        const auto z80_entries_path = z80_generated_root / "entries.txt";
        write_image(z80_program_path, z80_program);
        write_image(z80_opcode_path, z80_opcodes);
        write_image(z80_data_path, z80_data);
        {
            std::ofstream entries(z80_entries_path);
            if (!entries) throw std::runtime_error("could not create Toki Z80 entry list");
            entries << "0010\n0018\n0038\n";
        }
        const auto z80_recomp = source_root / "tools/z80/recomp.py";
        std::cout << "Generating Toki encrypted Z80 AOT from SEI80BU opcode/data views.\n" << std::flush;
        status = run_child_process({MULTIARCADERECOMP_PYTHON_EXECUTABLE, z80_recomp.string(),
            "--program", z80_program_path.string(), "--opcodes", z80_opcode_path.string(),
            "--data", z80_data_path.string(), "--entries", z80_entries_path.string(),
            "--out", z80_generated_root.string(), "--symbol-prefix", "toki_",
            "--precise-cycle-yield", "--list", (z80_generated_root / "instructions.txt").string()});
        if (status != 0) return status;
        status = run_child_process({"cmake", "-S", source_root.string(), "-B", build_root.string(),
            "-DMULTIARCADERECOMP_SEIBU_TOKI_AOT_DIR=" + generated_root.string(),
            "-DMULTIARCADERECOMP_SEIBU_TOKI_Z80_AOT_DIR=" + z80_generated_root.string()});
        if (status != 0) return status;
        std::cout << "Building Toki 68000 AOT module.\n" << std::flush;
        return run_child_process({"cmake", "--build", build_root.string(), "--target",
            "multiarcaderecomp_seibu_toki_aot", "-j", jobs});
    }

    if (!std::filesystem::is_regular_file(module_path))
        throw std::runtime_error("Toki AOT module is missing; run `multiarcaderecomp build --machine=seibu --board=toki --game=toki` first");
    void* module = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module) throw std::runtime_error("could not load Toki AOT module: " + std::string(dlerror()));
    struct ModuleGuard { void* handle; ~ModuleGuard() { if (handle) dlclose(handle); } } guard{module};
    using TableFn = const ng_func_entry_t* (*)(size_t*);
    using Z80CodeFn = const multiarcaderecomp::z80::Z80Code* (*)();
    auto get_table = reinterpret_cast<TableFn>(dlsym(module, "multiarcaderecomp_seibu_toki_m68k_table"));
    if (!get_table) throw std::runtime_error("Toki AOT module is missing its 68000 table");
    auto get_z80_code = reinterpret_cast<Z80CodeFn>(dlsym(module, "multiarcaderecomp_seibu_toki_z80_code"));
    if (!get_z80_code) throw std::runtime_error("Toki AOT module is missing its Z80 table");
    multiarcaderecomp::z80::SetZ80Code(get_z80_code());
    size_t count = 0;
    const auto* entries = get_table(&count);
    multiarcaderecomp::seibu::TokiBoard board(std::move(roms));
    multiarcaderecomp::seibu::TokiMainCpu cpu(board, entries, count);
    multiarcaderecomp::seibu::TokiSoundCpu sound_cpu(board);
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    std::unique_ptr<multiarcaderecomp::video::SdlVideoPresenter> video_presenter;
    if (window) video_presenter = std::make_unique<multiarcaderecomp::video::SdlVideoPresenter>(
        "Toki - MultiArcadeRecomp", 256, 224);
    if (video_presenter)
        std::cout << "Controls: coin 5, start 1, P1 arrows + keypad 1/2, P2 WASD + J/K, F2 service.\n";
    std::unique_ptr<multiarcaderecomp::seibu::TokiAudioPresenter> audio_presenter;
    if (audio_enabled) audio_presenter = std::make_unique<multiarcaderecomp::seibu::TokiAudioPresenter>(board.audio_sample_rate());
#else
    if (window) throw std::runtime_error("Toki live video requires an SDL3-enabled build");
    if (audio_enabled) throw std::runtime_error("Toki --audio requires an SDL3-enabled build");
#endif
    constexpr uint64_t frame_cycles = 167'700;
    constexpr uint64_t vblank_cycles = 156'000;
    const uint64_t budget = frames ? frames * frame_cycles
        : cycles ? *cycles : std::numeric_limits<uint64_t>::max();
    uint64_t next_vblank = vblank_cycles;
    uint64_t scheduled_cycles = 0;
    bool window_open = true;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    multiarcaderecomp::video::SdlArcadeInput live_keys{};
    uint64_t live_keys_frame = std::numeric_limits<uint64_t>::max();
#endif
    while (scheduled_cycles < budget && window_open) {
        const uint64_t target = std::min({next_vblank, scheduled_cycles + 650, budget});
        const uint64_t frame_index = scheduled_cycles / frame_cycles;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (video_presenter && live_keys_frame != frame_index) {
            if (!video_presenter->pump_events()) {
                window_open = false;
                break;
            }
            live_keys = video_presenter->input_state();
            live_keys_frame = frame_index;
        }
#endif
        uint8_t system = 0x1f;
        uint8_t coins = 0;
        uint8_t p1 = 0xff;
        uint8_t p2 = 0xff;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
        if (video_presenter) {
            if (live_keys.service_pressed) system &= static_cast<uint8_t>(~(1U << 2));
            if (live_keys.players_pressed[0] & multiarcaderecomp::video::SdlArcadeInput::start)
                system &= static_cast<uint8_t>(~(1U << 3));
            if (live_keys.players_pressed[1] & multiarcaderecomp::video::SdlArcadeInput::start)
                system &= static_cast<uint8_t>(~(1U << 4));
            coins |= live_keys.coins_pressed & 0x03;
            const auto to_toki_controls = [](uint8_t pressed) {
                uint8_t active_low = 0xff;
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::up) active_low &= ~(1U << 0);
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::down) active_low &= ~(1U << 1);
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::left) active_low &= ~(1U << 2);
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::right) active_low &= ~(1U << 3);
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::button1) active_low &= ~(1U << 4);
                if (pressed & multiarcaderecomp::video::SdlArcadeInput::button2) active_low &= ~(1U << 5);
                return active_low;
            };
            p1 = to_toki_controls(live_keys.players_pressed[0]);
            p2 = to_toki_controls(live_keys.players_pressed[1]);
        }
#endif
        for (const auto& event : input_events) {
            if (frame_index < event.first_frame || frame_index - event.first_frame >= event.duration_frames) continue;
            switch (event.port) {
            case InputEvent::Port::Service: system &= event.active_low_mask; break;
            case InputEvent::Port::P1: p1 &= event.active_low_mask; break;
            case InputEvent::Port::P2: p2 &= event.active_low_mask; break;
            case InputEvent::Port::Coin:
                coins |= event.active_low_mask;
                system &= static_cast<uint8_t>(~event.active_low_mask);
                break;
            case InputEvent::Port::System:
            case InputEvent::Port::P1Kick:
            case InputEvent::Port::P2Kick:
                break;
            }
        }
        board.set_inputs(static_cast<uint16_t>(p1 | (uint16_t(p2) << 8)), system, coins);
        cpu.execute_until(target);
        sound_cpu.run_until(target * 3'579'545ull / 10'000'000ull);
        scheduled_cycles = target;
        if (target == next_vblank) {
            board.video_vblank();
            next_vblank += frame_cycles;
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
            if (video_presenter) {
                const auto live_frame = board.render_frame();
                video_presenter->present(live_frame.rgb);
                const auto live_audio = board.take_audio_samples();
                if (audio_presenter) audio_presenter->queue(live_audio);
                window_open = video_presenter->pump_events();
            }
#endif
        }
    }
    const auto frame = board.render_frame();
    const auto audio_samples = board.take_audio_samples();
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
    if (audio_presenter) audio_presenter->queue(audio_samples);
#endif
    if (frame_out) {
        if (!frame_out->parent_path().empty()) std::filesystem::create_directories(frame_out->parent_path());
        std::ofstream output(*frame_out, std::ios::binary);
        if (!output) throw std::runtime_error("could not create Toki frame output " + frame_out->string());
        output << "P6\n256 224\n255\n";
        output.write(reinterpret_cast<const char*>(frame.rgb.data()), static_cast<std::streamsize>(frame.rgb.size()));
        if (!output) throw std::runtime_error("failed writing Toki frame output");
        std::cout << "Wrote 256x224 Toki framebuffer to " << *frame_out << '\n';
    }
    std::cout << "Toki 68000 cycles=" << cpu.cycles() << " PC=0x" << std::hex << cpu.pc()
              << "; Z80 PC=0x" << sound_cpu.pc() << std::dec << " Z80 cycles=" << sound_cpu.cycles()
              << "; vblank IRQs=" << board.main_irq_count()
              << "\n68000 execution coverage: AOT instructions=" << cpu.aot_instructions()
              << " Musashi fallback instructions=" << cpu.fallback_instructions()
              << "\nZ80 execution coverage: AOT instructions=" << sound_cpu.aot_instructions()
              << " fallback instructions=" << sound_cpu.fallback_instructions()
              << "\nToki YM3812: samples=" << board.ym3812_samples()
              << " rate=" << board.audio_sample_rate() << "Hz peak=" << board.ym3812_peak()
              << " register writes=" << board.ym3812_writes()
              << " sound command writes=" << board.sound_command_writes()
              << " RST18 assertions/acks=" << board.sound_irq_assertions() << '/' << board.sound_irq18_acks()
              << " RST10 acks=" << board.sound_irq10_acks()
              << " sound-latch reads=" << board.sound_latch_reads()
              << " last-latch=0x" << std::hex << static_cast<unsigned>(board.last_sound_latch_value())
              << " last-bus-word=0x" << board.last_sound_bus_word()
              << " low/high-nonzero=" << std::dec << board.sound_low_lane_nonzero()
              << '/' << board.sound_high_lane_nonzero()
              << " pending-acks=" << std::dec << board.sound_pending_acks()
              << " key-ons=" << board.ym3812_key_ons() << " last-reg=0x"
              << std::hex << static_cast<unsigned>(board.ym3812_last_register())
              << " last-value=0x" << static_cast<unsigned>(board.ym3812_last_value()) << std::dec
              << " OKIM6295 writes=" << board.oki_writes()
              << " active-voices=" << board.oki_active_voices()
              << (audio_enabled ? " queued to SDL playback" : " generated without host playback") << '\n';
    const auto video_bytes = board.nonzero_video_bytes();
    const auto tile_coverage = board.visible_tile_pixels();
    std::cout << "Toki nonzero video RAM bytes: palette=" << video_bytes[0]
              << " buffered-sprites=" << video_bytes[1] << " background=" << video_bytes[2]
              << " foreground=" << video_bytes[3] << " text=" << video_bytes[4]
              << " scroll-priority=0x" << std::hex << board.scroll_control() << std::dec
              << "\nToki nontransparent tile pixels: bg=" << tile_coverage[0]
              << " fg=" << tile_coverage[1] << "\nToki video sample: bg[0..3]=0x"
              << std::hex << board.tile_word(0, 0) << ",0x" << board.tile_word(0, 1)
              << ",0x" << board.tile_word(0, 2) << ",0x" << board.tile_word(0, 3)
              << " palette[544..559]=";
    for (unsigned i = 544; i < 560; ++i) std::cout << (i == 544 ? "0x" : ",0x") << board.palette_word(i);
    std::cout << std::dec << '\n';
    const auto text_rows = board.text_tiles_per_row();
    std::cout << "Toki text tiles/row:";
    for (unsigned row = 0; row < text_rows.size(); ++row)
        if (text_rows[row]) std::cout << ' ' << row << ':' << text_rows[row];
    std::cout << "; active sprite entries=" << board.active_sprite_entries() << '\n';
    return 0;
}

} // namespace multiarcaderecomp::app::machine_runners
