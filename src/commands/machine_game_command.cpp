#include "commands/machine_runner_support.hpp"
#include "commands/machine_game_command.hpp"
#include "commands/machine_runners.hpp"
#include "neogeo/neogeo_romset.hpp"
#include "konami/tmnt_machine.hpp"
#include "konami/tmnt_presenter.hpp"
#include "video/sdl_video_presenter.hpp"
#include "konami/tmnt2_machine.hpp"
#include "seibu/toki_machine.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "runtime/sega/runner/runner.hpp"
#include "runtime/sega/runner/rom_commands.hpp"
#include "naomi/naomi_module.hpp"
#include "naomi/naomi_romset.hpp"
#include "zn_runtime.hpp"
#include "native_z80.h"
#ifdef MULTIARCADERECOMP_HAS_SDL3_PRESENTER
#include "naomi/naomi_presenter.hpp"
#include "seibu/toki_presenter.hpp"
#include <SDL3/SDL.h>
#endif
#ifdef MULTIARCADERECOMP_HAS_GENERATED_RECOMP
#include <neogeorecomp/cpu.h>
#endif
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
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
#include <vector>
#include <dlfcn.h>

#ifndef MULTIARCADERECOMP_SOURCE_DIR
#define MULTIARCADERECOMP_SOURCE_DIR "."
#endif
#ifndef MULTIARCADERECOMP_PYTHON_EXECUTABLE
#define MULTIARCADERECOMP_PYTHON_EXECUTABLE "python3"
#endif

namespace multiarcaderecomp::app {
using multiarcaderecomp::app::read_binary_file;
using multiarcaderecomp::app::run_child_process;
using multiarcaderecomp::app::machine_runners::InputEvent;

std::uint64_t parse_u64(std::string_view value, std::string_view label) {
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
        base = 16;
    }
    std::uint64_t parsed_value = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parsed_value, base);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("invalid " + std::string(label) + ": " + std::string(value));
    }
    return parsed_value;
}


InputEvent parse_konami_button_event(std::string_view value) {
    const auto first = value.find(':');
    const auto second = first == value.npos ? value.npos : value.find(':', first + 1);
    if (first == value.npos || second == value.npos)
        throw std::runtime_error("--button-event format is <frame>:<button-name>:<duration-frames>");
    const auto frame = parse_u64(value.substr(0, first), "button event frame");
    const auto name = value.substr(first + 1, second - first - 1);
    const auto duration = parse_u64(value.substr(second + 1), "button event duration");
    if (duration == 0 || frame > std::numeric_limits<std::uint64_t>::max() - duration)
        throw std::runtime_error("button event duration must be positive and not overflow");

    InputEvent::Port port;
    unsigned bit = 0;
    if (name == "coin1") { port = InputEvent::Port::Service; bit = 0; }
    else if (name == "coin2") { port = InputEvent::Port::Service; bit = 1; }
    else if (name == "p1_left") { port = InputEvent::Port::P1; bit = 0; }
    else if (name == "p1_right") { port = InputEvent::Port::P1; bit = 1; }
    else if (name == "p1_up") { port = InputEvent::Port::P1; bit = 2; }
    else if (name == "p1_down") { port = InputEvent::Port::P1; bit = 3; }
    else if (name == "p1_button1") { port = InputEvent::Port::P1; bit = 4; }
    else if (name == "p1_button2") { port = InputEvent::Port::P1; bit = 5; }
    else if (name == "p1_button3") { port = InputEvent::Port::P1; bit = 6; }
    else if (name == "start1") { port = InputEvent::Port::P1; bit = 7; }
    else if (name == "p2_left") { port = InputEvent::Port::P2; bit = 0; }
    else if (name == "p2_right") { port = InputEvent::Port::P2; bit = 1; }
    else if (name == "p2_up") { port = InputEvent::Port::P2; bit = 2; }
    else if (name == "p2_down") { port = InputEvent::Port::P2; bit = 3; }
    else if (name == "p2_button1") { port = InputEvent::Port::P2; bit = 4; }
    else if (name == "p2_button2") { port = InputEvent::Port::P2; bit = 5; }
    else if (name == "p2_button3") { port = InputEvent::Port::P2; bit = 6; }
    else if (name == "start2") { port = InputEvent::Port::P2; bit = 7; }
    else throw std::runtime_error("unknown Konami button; use coin1/2, start1/2, and p1/p2 direction or button1/2/3");
    return {frame, duration, static_cast<std::uint8_t>(0xffU ^ (1U << bit)), port};
}

InputEvent parse_toki_button_event(std::string_view value) {
    const auto first = value.find(':');
    const auto second = first == value.npos ? value.npos : value.find(':', first + 1);
    if (first == value.npos || second == value.npos)
        throw std::runtime_error("--button-event format is <frame>:<button-name>:<duration-frames>");
    const auto frame = parse_u64(value.substr(0, first), "button event frame");
    const auto name = value.substr(first + 1, second - first - 1);
    const auto duration = parse_u64(value.substr(second + 1), "button event duration");
    if (duration == 0 || frame > std::numeric_limits<std::uint64_t>::max() - duration)
        throw std::runtime_error("button event duration must be positive and not overflow");
    InputEvent::Port port;
    unsigned bit = 0;
    if (name == "coin1") { port = InputEvent::Port::Coin; bit = 0; }
    else if (name == "coin2") { port = InputEvent::Port::Coin; bit = 1; }
    else if (name == "service1") { port = InputEvent::Port::Service; bit = 2; }
    else if (name == "start1") { port = InputEvent::Port::Service; bit = 3; }
    else if (name == "start2") { port = InputEvent::Port::Service; bit = 4; }
    else if (name == "p1_up") { port = InputEvent::Port::P1; bit = 0; }
    else if (name == "p1_down") { port = InputEvent::Port::P1; bit = 1; }
    else if (name == "p1_left") { port = InputEvent::Port::P1; bit = 2; }
    else if (name == "p1_right") { port = InputEvent::Port::P1; bit = 3; }
    else if (name == "p1_button1") { port = InputEvent::Port::P1; bit = 4; }
    else if (name == "p1_button2") { port = InputEvent::Port::P1; bit = 5; }
    else if (name == "p2_up") { port = InputEvent::Port::P2; bit = 0; }
    else if (name == "p2_down") { port = InputEvent::Port::P2; bit = 1; }
    else if (name == "p2_left") { port = InputEvent::Port::P2; bit = 2; }
    else if (name == "p2_right") { port = InputEvent::Port::P2; bit = 3; }
    else if (name == "p2_button1") { port = InputEvent::Port::P2; bit = 4; }
    else if (name == "p2_button2") { port = InputEvent::Port::P2; bit = 5; }
    else throw std::runtime_error("unknown Toki button; use coin1/2, start1/2, service1, or p1/p2 directions/buttons");
    const uint8_t mask = port == InputEvent::Port::Coin
        ? static_cast<uint8_t>(1U << bit)
        : static_cast<uint8_t>(0xffU ^ (1U << bit));
    return {frame, duration, mask, port};
}

InputEvent parse_zn_button_event(std::string_view value) {
    const auto first = value.find(':');
    const auto second = first == value.npos ? value.npos : value.find(':', first + 1);
    if (first == value.npos || second == value.npos)
        throw std::runtime_error("--button-event format is <frame>:<button-name>:<duration-frames>");
    const auto frame = parse_u64(value.substr(0, first), "button event frame");
    const auto name = value.substr(first + 1, second - first - 1);
    const auto duration = parse_u64(value.substr(second + 1), "button event duration");
    if (duration == 0 || frame > std::numeric_limits<std::uint64_t>::max() - duration)
        throw std::runtime_error("button event duration must be positive and not overflow");
    InputEvent::Port port;
    unsigned bit = 0;
    if (name == "coin1") { port = InputEvent::Port::System; bit = 4; }
    else if (name == "coin2") { port = InputEvent::Port::System; bit = 5; }
    else if (name == "start1") { port = InputEvent::Port::System; bit = 0; }
    else if (name == "start2") { port = InputEvent::Port::System; bit = 1; }
    else if (name == "service") { port = InputEvent::Port::Service; bit = 0; }
    else if (name == "service1") { port = InputEvent::Port::Service; bit = 1; }
    else if (name.starts_with("p1_") || name.starts_with("p2_")) {
        const bool p1 = name.starts_with("p1_");
        port = p1 ? InputEvent::Port::P1 : InputEvent::Port::P2;
        const auto control = name.substr(3);
        if (control == "up") bit = 0;
        else if (control == "down") bit = 1;
        else if (control == "left") bit = 2;
        else if (control == "right") bit = 3;
        else if (control == "button1") bit = 4;
        else if (control == "button2") bit = 5;
        else if (control == "button3") bit = 6;
        else if (control == "button4" || control == "button5" || control == "button6") {
            port = p1 ? InputEvent::Port::P1Kick : InputEvent::Port::P2Kick;
            bit = unsigned(control.back() - '4');
        } else throw std::runtime_error("unknown ZN player button or direction");
    } else throw std::runtime_error("unknown ZN button");
    return {frame, duration, static_cast<std::uint8_t>(0xffU ^ (1U << bit)), port};
}

void print_usage(const char* program) {
    std::cerr << "Usage:\n"
              << "  " << program << " build --machine=sega --board=<system16a|system16b|system18> --game=<profile> [--rom-dir <game.zip>] [--jobs <count>] [--rebuild]\n"
              << "  " << program << " audit --machine=sega --board=<system16a|system16b|system18> --game=<profile> [--rom-dir <game.zip>]\n"
              << "  " << program << " run --machine=sega --board=<system16a|system16b|system18> --game=<profile> [--rom-dir <game.zip>] [--window|--headless] [--frames <count>|--cycles <count>] [--audio] [--audio-pan game|spread] [--arranged-bgm] [--ym-mute-channel 1..8] [--ym-suppress-slot0-channel2] [--ym-suppress-slot3-channel5] [--ym-suppress-slot4-channel6] [--ym-suppress-slot5-channel7] [--video-backend software|opengl] [--compare-video] [--video-profile] [--aot-profile] [--uncapped] [--frame-out <image.ppm>] [--video-state-out <directory>]\n"
              << "  " << program << " build --machine=cps --board=cps1 --game=<sf2ce|dino> [--rom-dir <game.zip>] [--jobs <count>] [--rebuild]\n"
              << "  " << program << " run --machine=cps --board=cps1 --game=<sf2ce|dino> [--rom-dir <game.zip>]\n"
              << "  " << program << " build|run --machine=konami --board=tmnt2 --game=<tmnt2|ssriders> [--rom-dir <game.zip>] [--window|--headless --frames <count>|--cycles <count>] [--button-event <frame>:<button>:<duration>] [--frame-out <image.ppm>] [--frame-sequence-dir <directory>] [--frame-sequence-step <frames>]\n"
              << "  " << program << " run --machine=konami --board=tmnt --game=tmnt [--rom-dir <tmnt.zip>] [--window|--headless --frames <count>|--cycles <count>] [--frame-out <image.ppm>]\n"
              << "  " << program << " build|run --machine=seibu --board=toki --game=toki [--rom-dir <toki.zip>] [--window|--headless --frames <count>|--cycles <count>] [--frame-out <image.ppm>]\n"
              << "  " << program << " run --machine=sony --board=<zn1|zn2> --game=<bldyror2|sfex2pa> [--gpu-hw] [--rom-dir <game.zip>] [--qsound-mode true|mixed|hle] [--window|--headless --frames <count>|--cycles <count>] [--audio] [--button-event <frame>:<button>:<duration>] [--frame-out <image.ppm>] [--r3000a-no-aot] [--z80-no-aot] [--dsp16a-no-aot] [--no-aot]\n"
              << "  " << program << " run --machine=namco --board=system12 --game=tekken3 [--rom-dir <tekken3.zip>] [--window|--headless --frames <count>|--cycles <count>] [--frame-out <image.ppm>]\n"
              << "  " << program << " build --machine=konami --board=tmnt --game=tmnt [--rom-dir <tmnt.zip>]\n"
              << "  " << program << " build --machine=cps --board=cps2 --game=<sfa3|sfz2a|sfz2al|hsf2> [--rom-dir <game.zip>] [--jobs <count>] [--rebuild]\n"
              << "  " << program << " run --machine=cps --board=cps2 --game=<sfa3|sfz2a|sfz2al|hsf2> [--rom-dir <game.zip>] [--window|--headless --frames <count>]\n"
              << "  " << program << " build|run --machine=neogeo --board=<aes|mvs> --game=kof98 [--rom-dir <kof98.zip>] [--bios-zip <neogeo.zip>] [--bios <file>] [--jobs <count>] [--headless --frames <count>] [--input <events.txt>] [--nvram <path>] [--dump-misses <file>] [--dump-fallbacks <file>]\n"
              << "  " << program << " build --machine=midway --board=tunit --game=<mk|mk2> [--rom-dir <game.zip>] [--jobs <count>] [--rebuild]\n"
              << "  " << program << " run --machine=midway --board=tunit --game=<mk|mk2> [--rom-dir <game.zip>] [--window|--headless] [--frames <count>|--cycles <count>] [--frame-out <image.ppm>] [--input-replay <events.txt>] [--nvram <path>] [--dma-command-log <path>]\n"
              << "  " << program << " list-games\n"
              << "  " << program << " list-naomi-games\n"
              << "  " << program << " inspect-naomi-roms <mvsc2u|mvsc2> <romset.zip>\n"
              << "  " << program << " inspect-neogeo-roms <kof98> <romset.zip>\n"
              << "  " << program << " run-naomi-sh4 <mvsc2u> <romset.zip> [--bios <naomi.zip>] [--instructions <count>] [--interpreter-only] [--window]\n"
              << "  " << program << " map <profile> <romset.zip>\n"
              << "  " << program << " extract-graphics <profile> <romset.zip> [--out <directory>]\n"
              << "  " << program << " inspect <profile> <romset.zip> [--write <maincpu.bin>]\n"
                << "  " << program << " translate-full <profile> <romset.zip> [--out <directory>] [--files <count>] [--entry <address>]... [--entries-file <path>] [--pointer-scan]\n"
              ;
}


bool has_cps1_build_input_newer_than(const std::filesystem::path& root,
    const std::filesystem::file_time_type& stamp_time,
    const std::optional<std::filesystem::path>& rom_zip) {
    const auto newer = [&](const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return false;
        const auto modified = std::filesystem::last_write_time(path, error);
        return !error && modified > stamp_time;
    };
    if (rom_zip && newer(*rom_zip)) return true;

    const std::array<std::filesystem::path, 9> source_roots{
        root / "src/cps1", root / "core/cpus/m68000", root / "core/cpus/z80",
        root / "core/third_party/m68krecomp", root / "core/third_party/musashi",
        root / "core/third_party/z80-aot", root / "core/third_party/ymfm",
        root / "tools/generators/cps1", root / "tools/z80"
    };
    const auto is_source = [](const std::filesystem::path& path) {
        const auto extension = path.extension();
        return extension == ".c" || extension == ".cc" || extension == ".cpp"
            || extension == ".h" || extension == ".hpp" || extension == ".py";
    };
    for (const auto& source_root : source_roots) {
        std::error_code error;
        if (!std::filesystem::exists(source_root, error) || error) continue;
        std::filesystem::recursive_directory_iterator it(source_root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        for (; it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (it->is_regular_file(error) && !error && is_source(it->path()) && newer(it->path()))
                return true;
            error.clear();
        }
    }
    return newer(root / "CMakeLists.txt") || newer(root / "tools/recompile.py");
}

int ki_machine_command(const std::filesystem::path& root,
                        const std::string& game,
                        const std::optional<std::filesystem::path>& rom_arg,
                        bool build_mode, const std::string& jobs,
                        bool force_rebuild, bool window, std::uint64_t frames,
                        const std::optional<std::filesystem::path>& frame_out) {
    const auto rom_path = rom_arg ? std::filesystem::absolute(*rom_arg)
                                  : root.parent_path() / "mame/roms";
    const auto work = root / "build/ki/work" / game;
    const auto aot = work / "mips_aot";
    const auto adsp_aot = work / "adsp_aot";
    const auto host_build = game == "ki1" ? root / "build/ki/multiarcaderecomp-host"
                                           : root / "build/ki" / ("multiarcaderecomp-host-" + game);
    const auto runner = host_build / ("multiarcaderecomp_" + game);
    const auto stamp = work / ".multiarcaderecomp-build-stamp";

    bool stale = force_rebuild || !std::filesystem::is_regular_file(runner) ||
                 !std::filesystem::is_regular_file(aot / "rec_tab.c") ||
                 !std::filesystem::is_regular_file(adsp_aot / "rec_dsp.c");
    if (!stale && std::filesystem::is_regular_file(stamp)) {
        std::ifstream input(stamp);
        std::string line;
        bool same_rom = false;
        while (std::getline(input, line))
            if (line == "rom=" + rom_path.string()) same_rom = true;
        stale = !input.eof() || !same_rom;
        std::error_code error;
        const auto stamp_time = std::filesystem::last_write_time(stamp, error);
        if (error) stale = true;
        const std::array inputs{
            root / "CMakeLists.txt", root / "src/ki", root / "core/cpus/mips3",
            root / "core/cpus/adsp2105", root / "core/third_party/libchdr",
            root / "tools/ki", root / "tools/mips3", root / "tools/adsp2105",
            root / "core/third_party/ultracombo-adsp", root / "tools/recompile.py"
        };
        for (const auto& path : inputs) {
            if (error || !std::filesystem::exists(path)) continue;
            if (std::filesystem::is_regular_file(path)) {
                if (std::filesystem::last_write_time(path, error) > stamp_time) stale = true;
            } else {
                for (std::filesystem::recursive_directory_iterator it(path, error), end;
                     !error && it != end; it.increment(error)) {
                    if (it->is_regular_file(error) &&
                        std::filesystem::last_write_time(it->path(), error) > stamp_time) stale = true;
                }
            }
        }
    } else if (!std::filesystem::is_regular_file(stamp)) {
        stale = true;
    }

    if (build_mode) {
        if (!stale) {
            std::cout << game << " build is current; nothing to compile.\n" << std::flush;
            return 0;
        }
        std::cout << (force_rebuild ? "Forcing " + game + " rebuild.\n"
                                   : game + " runtime/AOT is missing or stale; building it.\n") << std::flush;
        std::vector<std::string> translate{
            MULTIARCADERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
            "--system", "ki", "--game", game, "--rom-dir", rom_path.string(),
            "--jobs", jobs
        };
        int status = run_child_process(translate);
        if (status != 0) return status;
        std::vector<std::string> configure{
            "cmake", "-S", root.string(), "-B", host_build.string(),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_C_FLAGS_RELEASE=-O3",
            "-DCMAKE_CXX_FLAGS_RELEASE=-O3",
            "-DMULTIARCADERECOMP_KI_MIPS_AOT_DIR=" + aot.string(),
            "-DMULTIARCADERECOMP_KI_MIPS_GAME_ID=" + game,
            "-DMULTIARCADERECOMP_KI_ADSP_AOT_DIR=" + adsp_aot.string()
        };
        status = run_child_process(configure);
        if (status != 0) return status;
        status = run_child_process({"cmake", "--build", host_build.string(),
                                    "--target", "multiarcaderecomp_ki1", "--parallel", jobs});
        if (status != 0) return status;
        std::filesystem::create_directories(work);
        std::ofstream output(stamp, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not write KI build stamp: " + stamp.string());
        output << "game=" << game << "\nrom=" << rom_path.string() << '\n';
        if (!output) throw std::runtime_error("failed writing KI build stamp: " + stamp.string());
        return 0;
    }

    if (stale)
        throw std::runtime_error(game + " build is missing or stale; run `multiarcaderecomp build --machine=ki --board=seattle --game=" + game + "` first");
    std::vector<std::string> command{runner.string(), "--rom-dir", rom_path.string(),
                                    "--work-dir", work.string(), "--game", game};
    if (window) command.emplace_back("--window");
    else command.insert(command.end(), {"--frames", std::to_string(frames ? frames : 1)});
    if (frame_out) command.insert(command.end(), {"--frame-out", frame_out->string()});
    std::cout << "Launching " << game << " on Seattle (MIPS III/R4600).\n" << std::flush;
    return run_child_process(command);
}

int midway_machine_command(const std::filesystem::path& root,
                           const std::string& game,
                           const std::optional<std::filesystem::path>& rom_arg,
                           bool build_mode, const std::string& jobs,
                           bool force_rebuild, bool window, bool headless,
                           std::uint64_t frames,
                           const std::optional<std::uint64_t>& cycles,
                           const std::optional<std::filesystem::path>& frame_out,
                           const std::optional<std::filesystem::path>& input_replay,
                           const std::optional<std::filesystem::path>& nvram,
                           const std::optional<std::filesystem::path>& dma_command_log) {
    if (game != "mk" && game != "mk2")
        throw std::runtime_error("T-Unit currently supports MAME sets --game=mk or --game=mk2");
    const auto rom_path = rom_arg ? std::filesystem::absolute(*rom_arg)
                                  : root.parent_path() / "mame/roms" / (game + ".zip");
    if (!std::filesystem::is_regular_file(rom_path))
        throw std::runtime_error("MAME ROM ZIP not found: " + rom_path.string());

    const auto work = root / "build" / game / "work";
    const auto game_build_root = root / "build" / game / "cmake";
    const bool mk2 = game == "mk2";
    const auto runner = game_build_root / (mk2 ? "multiarcaderecomp_mk2_trace" : "multiarcaderecomp_mk_trace");
    const auto tms_aot = work / "tms34010_aot.cpp";
    const auto sound_aot = work / (mk2 ? "adsp2105_aot.cpp" : "mc6809e_aot.cpp");
    if (build_mode) {
        if (window || headless || frames || cycles || frame_out || input_replay || nvram || dma_command_log)
            throw std::runtime_error("run-only options cannot be used with the Midway build command");
        const auto generator = root / "tools" / game / "build.py";
        std::vector<std::string> generate{
            MULTIARCADERECOMP_PYTHON_EXECUTABLE, generator.string(), "--romset",
            rom_path.string(), "--out", work.string()
        };
        if (force_rebuild)
            std::cout << "Forcing " << game << " T-Unit ROM/AOT rebuild.\n" << std::flush;
        else
            std::cout << "Generating " << game << " T-Unit ROM/AOT.\n" << std::flush;
        int status = run_child_process(generate);
        if (status != 0) return status;

        std::vector<std::string> configure{
            "cmake", "-S", root.string(), "-B", game_build_root.string(),
            "-DMULTIARCADERECOMP_TMS34010_AOT_SOURCE=" + (mk2 ? std::string{} : tms_aot.string()),
            "-DMULTIARCADERECOMP_M6809_AOT_SOURCE=" + (mk2 ? std::string{} : sound_aot.string()),
            "-DMULTIARCADERECOMP_MK2_TMS34010_AOT_SOURCE=" + (mk2 ? tms_aot.string() : std::string{}),
            "-DMULTIARCADERECOMP_ADSP2105_AOT_SOURCE=" + (mk2 ? sound_aot.string() : std::string{})
        };
        status = run_child_process(configure);
        if (status != 0) return status;
        status = run_child_process({"cmake", "--build", game_build_root.string(), "--target",
                                    runner.filename().string(),
                                    "--parallel", jobs});
        if (status != 0) return status;
        std::filesystem::create_directories(work);
        std::ofstream stamp(work / ".multiarcaderecomp-launcher-build", std::ios::trunc);
        if (!stamp) throw std::runtime_error("could not write T-Unit build stamp");
        stamp << "game=" << game << "\nrom=" << rom_path.string() << '\n';
        if (!stamp) throw std::runtime_error("failed writing T-Unit build stamp");
        return 0;
    }

    if (!std::filesystem::is_regular_file(runner) || !std::filesystem::is_regular_file(tms_aot) ||
        !std::filesystem::is_regular_file(sound_aot)) {
        throw std::runtime_error(game + " build is missing; run `multiarcaderecomp build --machine=midway --board=tunit --game="
            + game + "` first");
    }
    const auto stamp_path = work / ".multiarcaderecomp-launcher-build";
    std::ifstream stamp(stamp_path);
    std::string stamp_text((std::istreambuf_iterator<char>(stamp)), std::istreambuf_iterator<char>());
    if (!stamp || stamp_text.find("game=" + game + "\n") == std::string::npos ||
        stamp_text.find("rom=" + rom_path.string() + "\n") == std::string::npos) {
        throw std::runtime_error(game + " build does not match the requested ROM ZIP; rebuild it with `multiarcaderecomp build --machine=midway --board=tunit --game="
            + game + " --rom-dir " + rom_path.string() + "`");
    }
    if (headless && !frames && !cycles)
        throw std::runtime_error("headless T-Unit runs require --frames or --cycles");
    if (window && input_replay)
        throw std::runtime_error("--input-replay is only available for headless T-Unit runs");

    constexpr std::uint64_t tunit_cycles_per_second = 6'250'000;
    const auto budget = cycles.value_or(frames
        ? (frames * tunit_cycles_per_second) / 60u
        : std::uint64_t{0});
    const bool show_window = window || (!headless && !frames && !cycles);
    std::vector<std::string> command{runner.string(), work.string()};
    if (budget) command.push_back(std::to_string(budget));
    if (frame_out) command.push_back(std::filesystem::absolute(*frame_out).string());
    else if (budget) command.push_back((work / (game + "-frame.ppm")).string());
    if (show_window) command.emplace_back("--window");
    if (input_replay) command.push_back("--input-replay=" + std::filesystem::absolute(*input_replay).string());
    if (nvram) command.push_back("--nvram=" + std::filesystem::absolute(*nvram).string());
    if (dma_command_log) {
        const auto option = mk2 ? "--dma-command-log=" : "--dma-trace=";
        command.push_back(option + std::filesystem::absolute(*dma_command_log).string());
    }
    std::cout << "Launching MAME set " << game << " on Midway T-Unit.\n" << std::flush;
    return run_child_process(command);
}


int machine_game_command(int argc, char** argv, bool build_mode, bool audit_mode) {
    std::string machine;
    std::string board;
    std::string game;
    std::optional<std::filesystem::path> rom_zip;
    std::optional<std::filesystem::path> bios_zip;
    std::optional<std::string> bios_name;
    std::string neogeo_system = "aes";
    bool system_option_set = false;
    std::string jobs = "2";
    bool force_rebuild = false;
    bool window = false;
    bool headless = false;
    std::uint64_t frames = 0;
    std::optional<std::uint64_t> cycles;
    std::optional<std::filesystem::path> frame_out;
    std::optional<std::filesystem::path> frame_sequence_dir;
    std::uint64_t frame_sequence_step = 1;
    std::optional<std::filesystem::path> input_replay;
    std::optional<std::filesystem::path> nvram;
    std::optional<std::filesystem::path> dma_command_log;
    std::string qsound_mode_option = "true";
    bool qsound_mode_option_seen = false;
    bool r3000a_no_aot = false;
    bool z80_no_aot = false;
    bool dsp16a_no_aot = false;
    bool no_aot = false;
    bool neogeo_interp = false;
    bool neogeo_verify = false;
    std::optional<std::string> neogeo_dump_misses;
    std::optional<std::string> neogeo_dump_fallbacks;
    bool gpu_hw = false;
    bool audio_enabled = false;
    bool audio_stereo_spread = false;
    bool audio_pan_option_set = false;
    bool arranged_bgm = false;
    std::uint8_t ym2151_muted_channels = 0;
    bool ym2151_suppress_slot0_channel2 = false;
    bool ym2151_suppress_slot3_channel5 = false;
    bool ym2151_suppress_slot4_channel6 = false;
    bool ym2151_suppress_slot5_channel7 = false;
    std::optional<std::string> sega_video_backend;
    bool sega_compare_video = false;
    bool sega_video_profile = false;
    bool sega_aot_profile = false;
    bool sega_frame_limit_enabled = true;
    std::optional<std::filesystem::path> cpu_trace_out;
    std::optional<std::filesystem::path> neogeo_input_script;
    std::optional<std::filesystem::path> video_state_out;
    std::optional<std::uint64_t> cpu_trace_start;
    std::optional<std::uint64_t> cpu_trace_end;
    std::vector<std::string> button_event_specs;
    for (int i = 2; i < argc; ++i) {
        const std::string_view option(argv[i]);
        auto value_after = [&](std::string_view name) -> std::optional<std::string> {
            const std::string prefix = std::string(name) + "=";
            if (option.starts_with(prefix)) return std::string(option.substr(prefix.size()));
            if (option == name && i + 1 < argc) return std::string(argv[++i]);
            return std::nullopt;
        };
        if (const auto value = value_after("--machine")) machine = *value;
        else if (const auto value = value_after("--board")) board = *value;
        else if (const auto value = value_after("--game")) game = *value;
        else if (const auto value = value_after("--rom-dir")) rom_zip = *value;
        else if (const auto value = value_after("--bios-zip")) bios_zip = *value;
        else if (const auto value = value_after("--bios")) bios_name = *value;
        else if (const auto value = value_after("--system")) {
            neogeo_system = *value;
            system_option_set = true;
        }
        else if (const auto value = value_after("--dump-misses")) neogeo_dump_misses = *value;
        else if (const auto value = value_after("--dump-fallbacks")) neogeo_dump_fallbacks = *value;
        else if (const auto value = value_after("--input")) neogeo_input_script = *value;
        else if (const auto value = value_after("--frames")) frames = parse_u64(*value, "frame count");
        else if (const auto value = value_after("--cycles")) cycles = parse_u64(*value, "cycle count");
        else if (const auto value = value_after("--frame-out")) frame_out = *value;
        else if (const auto value = value_after("--video-state-out")) video_state_out = *value;
        else if (const auto value = value_after("--video-backend")) {
            if (*value != "software" && *value != "opengl")
                throw std::runtime_error("--video-backend must be software or opengl");
            sega_video_backend = *value;
        }
        else if (const auto value = value_after("--frame-sequence-dir")) frame_sequence_dir = *value;
        else if (const auto value = value_after("--frame-sequence-step")) frame_sequence_step = parse_u64(*value, "frame sequence step");
        else if (const auto value = value_after("--input-replay")) input_replay = *value;
        else if (const auto value = value_after("--button-event")) button_event_specs.push_back(*value);
        else if (const auto value = value_after("--nvram")) nvram = *value;
        else if (const auto value = value_after("--dma-command-log")) dma_command_log = *value;
        else if (const auto value = value_after("--cpu-trace")) cpu_trace_out = *value;
        else if (const auto value = value_after("--cpu-trace-cycles")) {
            const auto colon = value->find(':');
            if (colon == std::string::npos) throw std::runtime_error("--cpu-trace-cycles format is <start>:<end>");
            cpu_trace_start = parse_u64(std::string_view(*value).substr(0, colon), "CPU trace start cycle");
            cpu_trace_end = parse_u64(std::string_view(*value).substr(colon + 1), "CPU trace end cycle");
            if (*cpu_trace_start > *cpu_trace_end) throw std::runtime_error("CPU trace start must not exceed end");
        }
        else if (const auto value = value_after("--qsound-mode")) {
            qsound_mode_option_seen = true;
            if (*value != "true" && *value != "mixed" && *value != "hle")
                throw std::runtime_error("--qsound-mode must be true, mixed, or hle");
            qsound_mode_option = *value;
        }
        else if (option == "--audio") audio_enabled = true;
        else if (option == "--arranged-bgm") {
            arranged_bgm = true;
            audio_enabled = true;
            ym2151_muted_channels |= static_cast<std::uint8_t>((1U << 7) | (1U << 3) | (1U << 0)
                | (1U << 2));
            ym2151_suppress_slot0_channel2 = true;
            ym2151_suppress_slot3_channel5 = true;
            ym2151_suppress_slot4_channel6 = true;
            ym2151_suppress_slot5_channel7 = true;
        }
        else if (option == "--compare-video") sega_compare_video = true;
        else if (option == "--video-profile") sega_video_profile = true;
        else if (option == "--aot-profile") sega_aot_profile = true;
        else if (option == "--uncapped") sega_frame_limit_enabled = false;
        else if (option == "--psx-interpreter-only" || option == "--r3000a-no-aot") r3000a_no_aot = true;
        else if (option == "--z80-no-aot") z80_no_aot = true;
        else if (option == "--dsp16a-no-aot") dsp16a_no_aot = true;
        else if (option == "--no-aot") no_aot = true;
        else if (option == "--interp") neogeo_interp = true;
        else if (option == "--verify") neogeo_verify = true;
        else if (option == "--gpu-hw") gpu_hw = true;
        else if (const auto value = value_after("--audio-pan")) {
            if (*value == "game") audio_stereo_spread = false;
            else if (*value == "spread") audio_stereo_spread = true;
            else throw std::runtime_error("--audio-pan must be game or spread");
            audio_pan_option_set = true;
        }
        else if (const auto value = value_after("--ym-mute-channel")) {
            const auto channel = parse_u64(*value, "YM2151 channel");
            if (channel < 1 || channel > 8)
                throw std::runtime_error("--ym-mute-channel must be between 1 and 8");
            ym2151_muted_channels |= static_cast<std::uint8_t>(1U << (channel - 1));
        }
        else if (option == "--ym-suppress-slot0-channel2") ym2151_suppress_slot0_channel2 = true;
        else if (option == "--ym-suppress-slot3-channel5") ym2151_suppress_slot3_channel5 = true;
        else if (option == "--ym-suppress-slot4-channel6") ym2151_suppress_slot4_channel6 = true;
        else if (option == "--ym-suppress-slot5-channel7") ym2151_suppress_slot5_channel7 = true;
        else if (option == "--window") { window = true; headless = false; }
        else if (option == "--headless") { window = false; headless = true; }
        else if (option == "--jobs" && i + 1 < argc) {
            if (!build_mode) throw std::runtime_error("--jobs is only valid for the build command");
            jobs = argv[++i];
        } else if (option.starts_with("--jobs=")) {
            if (!build_mode) throw std::runtime_error("--jobs is only valid for the build command");
            jobs = std::string(option.substr(7));
        } else if (build_mode && option == "--rebuild") {
            force_rebuild = true;
        } else {
            throw std::runtime_error("unknown " + std::string(build_mode ? "build" : "run")
            + " option: " + std::string(option));
        }
    }
    if ((frame_sequence_dir || frame_sequence_step != 1) &&
        !(machine == "konami" && board == "tmnt2") &&
        !((machine == "sony" || machine == "zn") &&
          (board == "zn1" || board == "zn2")))
        throw std::runtime_error("frame sequence capture is supported for Konami TMNT2 and Sony ZN");
    if ((r3000a_no_aot || z80_no_aot || dsp16a_no_aot || no_aot) &&
        !(!build_mode && (machine == "sony" || machine == "zn") && board == "zn2" &&
          game == "sfex2pa"))
        throw std::runtime_error("per-CPU AoT controls are supported for Sony ZN-2 SFEX2PA run only");
    if (cpu_trace_out.has_value() != cpu_trace_start.has_value() ||
        cpu_trace_out.has_value() != cpu_trace_end.has_value())
        throw std::runtime_error("--cpu-trace and --cpu-trace-cycles must be specified together");
    if (cpu_trace_out && (build_mode || !(machine == "sega" || machine == "system16" || machine == "system18")))
        throw std::runtime_error("--cpu-trace is currently available for Sega System 16/18 runs only");
    if (video_state_out && (build_mode || !(machine == "sega" || machine == "system16" || machine == "system18")))
        throw std::runtime_error("--video-state-out is currently available for Sega System 16/18 runs only");
    if ((sega_video_backend || sega_compare_video)
        && (build_mode || !(machine == "sega" || machine == "system16" || machine == "system18")))
        throw std::runtime_error("--video-backend and --compare-video are available for Sega runs only");
    if ((sega_video_profile || sega_aot_profile || !sega_frame_limit_enabled)
        && (build_mode || !(machine == "sega" || machine == "system16" || machine == "system18")))
        throw std::runtime_error("--video-profile and --aot-profile are available for Sega runs only");
    const bool sega_system16b = machine == "sega" && board == "system16b";
    if (ym2151_muted_channels != 0 && (build_mode || !sega_system16b))
        throw std::runtime_error("--ym-mute-channel is currently supported for Sega System16B runs only");
    if (arranged_bgm && (build_mode || !sega_system16b || !audio_enabled))
        throw std::runtime_error("--arranged-bgm requires an audio-enabled Sega System16B run");
    if (ym2151_suppress_slot0_channel2 &&
        (build_mode || !sega_system16b || !audio_enabled))
        throw std::runtime_error("--ym-suppress-slot0-channel2 requires audio-enabled Sega System16B");
    if (ym2151_suppress_slot3_channel5 &&
        (build_mode || !sega_system16b || !audio_enabled))
        throw std::runtime_error("--ym-suppress-slot3-channel5 requires audio-enabled Sega System16B");
    if (ym2151_suppress_slot4_channel6 &&
        (build_mode || !sega_system16b || !audio_enabled))
        throw std::runtime_error("--ym-suppress-slot4-channel6 requires audio-enabled Sega System16B");
    if (ym2151_suppress_slot5_channel7 &&
        (build_mode || !sega_system16b || !audio_enabled))
        throw std::runtime_error("--ym-suppress-slot5-channel7 requires audio-enabled Sega System16B");
    if (audit_mode && machine != "sega" && machine != "system16" && machine != "system18")
        throw std::runtime_error("ROM audit is currently available for Sega System 16/18 profiles");
    if (no_aot)
        r3000a_no_aot = z80_no_aot = dsp16a_no_aot = true;
    if (gpu_hw && build_mode)
        throw std::runtime_error("--gpu-hw is only valid for run");
    if (gpu_hw && !((machine == "sony" || machine == "zn") ||
                    (machine == "namco" && board == "system12")))
        throw std::runtime_error("--gpu-hw is supported only on PSX GPU-based Sony ZN and Namco System 12 runs");
    if (qsound_mode_option_seen &&
        !((machine == "sony" || machine == "zn") && board == "zn2" &&
          game == "sfex2pa" && !build_mode))
        throw std::runtime_error("QSound mode options are supported for Sony ZN-2 SFEX2PA run only");
    if (machine == "konami") {
        if (input_replay || nvram || dma_command_log || audio_pan_option_set)
            throw std::runtime_error("input replay, NVRAM, DMA logging and audio pan are not supported for Konami");
        if (board == "tmnt") {
            if (game != "tmnt") throw std::runtime_error("Original TMNT board requires --game=tmnt");
            const bool show_window = !build_mode && (window || (!headless && !frames && !cycles));
            const bool play_audio = !build_mode && (audio_enabled || show_window);
            return multiarcaderecomp::app::machine_runners::run_konami_tmnt_machine(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR),
                rom_zip, build_mode, show_window, frames, cycles, frame_out, play_audio,
                jobs, force_rebuild);
        }
        if (board != "tmnt2")
            throw std::runtime_error("Konami adapter requires --board=tmnt or --board=tmnt2");
        std::vector<InputEvent> input_events;
        for (const auto& spec : button_event_specs)
            input_events.push_back(parse_konami_button_event(spec));
        const bool show_window = !build_mode && (window || (!headless && !frames && !cycles));
        return multiarcaderecomp::app::machine_runners::run_konami_tmnt2_machine(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR),
            game, rom_zip, build_mode, show_window, frames, cycles, frame_out, frame_sequence_dir,
            frame_sequence_step, audio_enabled || show_window,
            input_events, jobs, force_rebuild);
    }
    if (machine == "seibu") {
        if (board != "toki") throw std::runtime_error("Seibu adapter requires --board=toki");
        if (input_replay || nvram || dma_command_log || audio_pan_option_set)
            throw std::runtime_error("Toki does not use input replay, NVRAM, DMA logging, or audio pan");
        std::vector<InputEvent> toki_events;
        for (const auto& spec : button_event_specs) toki_events.push_back(parse_toki_button_event(spec));
        const bool show_window = !build_mode && (window || (!headless && !frames && !cycles));
        return multiarcaderecomp::app::machine_runners::run_seibu_toki_machine(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), game,
            rom_zip, build_mode, show_window, frames, cycles, frame_out, audio_enabled || show_window,
            toki_events, jobs, force_rebuild);
    }
    if (machine == "sony" || machine == "zn") {
        const bool valid_zn1 = board == "zn1" && game == "bldyror2";
        const bool valid_zn2 = board == "zn2" && game == "sfex2pa";
        if (!valid_zn1 && !valid_zn2)
            throw std::runtime_error("Sony ZN adapter currently supports --board=zn1 --game=bldyror2 or --board=zn2 --game=sfex2pa");
        if (build_mode && (r3000a_no_aot || z80_no_aot || dsp16a_no_aot || no_aot))
            throw std::runtime_error("per-CPU AoT controls are only valid for run");
        if (audio_pan_option_set || input_replay || dma_command_log)
            throw std::runtime_error("ZN audio pan, input replay and DMA logging are not integrated yet");
        std::vector<InputEvent> zn_events;
        for (const auto& spec : button_event_specs)
            zn_events.push_back(parse_zn_button_event(spec));
        const bool show_window = window || (!headless && !frames && !cycles);
        if (build_mode) {
            if (!valid_zn2)
                throw std::runtime_error("ZN-1 AOT recipe is not available yet");
            const std::filesystem::path source_root(MULTIARCADERECOMP_SOURCE_DIR);
            const auto build_root = source_root / "build";
            const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
                : source_root.parent_path() / "mame/roms" / (game + ".zip");
            if (!std::filesystem::is_regular_file(rom_path))
                throw std::runtime_error("ZN ROM ZIP not found: " + rom_path.string());
            const auto generated = build_root / "generated/sony/zn2/sfex2pa";
            std::cout << "Generating SFEX2PA QSound Z80 AOT from its ROMs.\n" << std::flush;
            int status = run_child_process({MULTIARCADERECOMP_PYTHON_EXECUTABLE,
                (source_root / "tools/psx/build_zn2_aot.py").string(),
                "--rom", rom_path.string(), "--out", generated.string(),
                "--python", MULTIARCADERECOMP_PYTHON_EXECUTABLE});
            if (status != 0) return status;
            status = run_child_process({"cmake", "-S", source_root.string(), "-B", build_root.string(),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DMULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR=" + generated.string()});
            if (status != 0) return status;
            status = run_child_process({"cmake", "--build", build_root.string(),
                "--target", "multiarcaderecomp_zn2_sfex2pa", "--parallel", jobs});
            if (status != 0) return status;
            const auto pc_list = generated / "sfex2pa_ram_pcs.txt";
            const auto ram_snapshot = generated / "sfex2pa_ram_snapshot.bin";
            std::cout << "Profiling executed R3000A game code before compiling its RAM overlay.\n" << std::flush;
            status = run_child_process({"env",
                "MULTIARCADERECOMP_PSX_AOT_PROFILE=" + pc_list.string(),
                "MULTIARCADERECOMP_ZN_DUMP_RAM=" + ram_snapshot.string(),
                (build_root / "multiarcaderecomp").string(), "run",
                "--machine=sony", "--board=zn2", "--game=sfex2pa", "--headless", "--frames=1620",
                "--button-event=300:coin1:3", "--button-event=950:start1:5",
                "--button-event=1205:p1_button1:5"});
            if (status != 0) return status;
            status = run_child_process({MULTIARCADERECOMP_PYTHON_EXECUTABLE,
                (source_root / "tools/psx/build_zn2_aot.py").string(),
                "--rom", rom_path.string(), "--out", generated.string(),
                "--python", MULTIARCADERECOMP_PYTHON_EXECUTABLE,
                "--ram-snapshot", ram_snapshot.string(), "--pc-list", pc_list.string()});
            if (status != 0) return status;
            status = run_child_process({"cmake", "-S", source_root.string(), "-B", build_root.string(),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DMULTIARCADERECOMP_ZN2_SFEX2PA_AOT_DIR=" + generated.string()});
            if (status != 0) return status;
            return run_child_process({"cmake", "--build", build_root.string(),
                "--target", "multiarcaderecomp_zn2_sfex2pa", "--parallel", jobs});
        }
        if (r3000a_no_aot || z80_no_aot || dsp16a_no_aot) {
            std::cout << "SFEX2PA AoT overrides:";
            if (r3000a_no_aot) std::cout << " R3000A=interpreter";
            if (z80_no_aot) std::cout << " Z80=interpreter";
            if (dsp16a_no_aot) std::cout << " DSP16A=interpreter";
            std::cout << ".\n";
        }
        return multiarcaderecomp::app::machine_runners::run_zn_machine(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), game, rom_zip,
                              show_window, audio_enabled, gpu_hw, zn_events, frames, cycles, frame_out,
                              frame_sequence_dir, frame_sequence_step, "Sony",
                              valid_zn1 ? "ZN-1" : "ZN-2", nvram,
                              r3000a_no_aot, z80_no_aot, dsp16a_no_aot,
                              qsound_mode_option == "true"
                                  ? multiarcaderecomp::chips::QSoundMode::Lle
                                  : qsound_mode_option == "mixed"
                                      ? multiarcaderecomp::chips::QSoundMode::Mixed
                                      : multiarcaderecomp::chips::QSoundMode::Hle,
                              qsound_mode_option == "mixed");
    }
    if (machine == "namco" && board == "system12") {
        if (build_mode)
            throw std::runtime_error("System 12 game AOT generation is not integrated yet; use run for the interpreter runtime");
        if (game != "tekken3")
            throw std::runtime_error("Namco System 12 currently supports --game=tekken3");
        if (audio_pan_option_set || input_replay || nvram || dma_command_log ||
            !button_event_specs.empty())
            throw std::runtime_error("System 12 audio pan, input replay and NVRAM options are not integrated yet");
        const bool show_window = window || (!headless && !frames && !cycles);
        return multiarcaderecomp::app::machine_runners::run_zn_machine(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), game, rom_zip,
                              show_window, audio_enabled, gpu_hw, {}, frames, cycles, frame_out,
                              frame_sequence_dir, frame_sequence_step, "Namco", "System 12", std::nullopt,
                              false, false, false, multiarcaderecomp::chips::QSoundMode::Lle, false);
    }
    if (!button_event_specs.empty())
        throw std::runtime_error("--button-event is currently supported for Konami games only");
    if (machine != "neogeo" && (bios_zip || bios_name || neogeo_system != "aes"))
        throw std::runtime_error("--bios, --bios-zip, and --system are Neo Geo options");
    if (machine != "neogeo" && (neogeo_interp || neogeo_verify ||
        neogeo_dump_misses || neogeo_dump_fallbacks))
        throw std::runtime_error("--interp and --verify are Neo Geo options");
    if (machine == "neogeo") {
        if (system_option_set)
            throw std::runtime_error("Neo Geo hardware mode is selected with --board=aes or --board=mvs; omit --system");
        if ((board != "mvs" && board != "aes") || game != "kof98")
            throw std::runtime_error("Neo Geo currently supports --board=aes or --board=mvs with --game=kof98");
        if (audit_mode) throw std::runtime_error("Neo Geo ROM audit is not exposed by this command yet");
        if (cycles || video_state_out || frame_sequence_dir || input_replay || dma_command_log ||
            cpu_trace_out || sega_video_backend || sega_compare_video || sega_video_profile ||
            sega_aot_profile || !sega_frame_limit_enabled || arranged_bgm || ym2151_muted_channels ||
            ym2151_suppress_slot0_channel2 || ym2151_suppress_slot3_channel5 ||
            ym2151_suppress_slot4_channel6 || ym2151_suppress_slot5_channel7 ||
            r3000a_no_aot || z80_no_aot || dsp16a_no_aot || no_aot || gpu_hw ||
            qsound_mode_option_seen || !button_event_specs.empty())
            throw std::runtime_error("one or more requested options are not supported by Neo Geo yet");
        if (build_mode && (window || headless || frames || audio_enabled || nvram))
            throw std::runtime_error("run-only options cannot be used with the Neo Geo build command");
        if (build_mode && (neogeo_interp || neogeo_verify))
            throw std::runtime_error("--interp and --verify are run-only Neo Geo options");
        if (build_mode && (neogeo_dump_misses || neogeo_dump_fallbacks))
            throw std::runtime_error("--dump-misses and --dump-fallbacks are run-only Neo Geo options");
        if (neogeo_input_script && build_mode)
            throw std::runtime_error("--input is a run-only Neo Geo scripted-input option");

        const std::filesystem::path root(MULTIARCADERECOMP_SOURCE_DIR);
        const auto default_rom_dir = root.parent_path() / "mame/roms";
        const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
                                      : default_rom_dir / "kof98.zip";
        if (!std::filesystem::is_regular_file(rom_path))
            throw std::runtime_error("KOF98 ROM ZIP not found: " + rom_path.string());

        if (build_mode) {
            std::vector<std::string> build_command{
                MULTIARCADERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
                "--system", "neogeo", "--game", "kof98", "--jobs", jobs,
                "--rom-dir", rom_path.string(), "--hardware-mode", board
            };
            if (force_rebuild) build_command.emplace_back("--rebuild");
            if (bios_zip) { build_command.emplace_back("--bios-zip"); build_command.push_back(std::filesystem::absolute(*bios_zip).string()); }
            if (bios_name) { build_command.emplace_back("--bios"); build_command.push_back(*bios_name); }
            std::cout << "Building KOF98 modules through the MultiArcadeRecomp Neo Geo pipeline.\n" << std::flush;
            return run_child_process(build_command);
        }

#ifdef MULTIARCADERECOMP_BINARY_DIR
        const std::filesystem::path default_module_dir(MULTIARCADERECOMP_BINARY_DIR);
#else
        const std::filesystem::path default_module_dir = root / "build";
#endif
        const char* configured_module_dir = std::getenv("MULTIARCADERECOMP_MODULE_DIR");
        const auto module_dir = configured_module_dir && *configured_module_dir
            ? std::filesystem::path(configured_module_dir) : default_module_dir;
        const auto runtime_module = module_dir / "multiarcaderecomp_neogeo_runtime.so";
        const auto window_module = module_dir / "multiarcaderecomp_neogeo_window.so";
        const auto game_module = module_dir / ("multiarcaderecomp_game_kof98_" + board + ".so");
        if (!std::filesystem::is_regular_file(runtime_module) || !std::filesystem::is_regular_file(game_module))
            throw std::runtime_error("Neo Geo modules for " + board + " mode are missing; run `multiarcaderecomp build --machine=neogeo --board=" + board + " --game=kof98` first");

#if defined(__unix__) || defined(__APPLE__)
        void* window_handle = nullptr;
        if (std::filesystem::is_regular_file(window_module)) {
            int window_load_flags = RTLD_NOW | RTLD_LOCAL;
#if defined(__GLIBC__) && defined(RTLD_DEEPBIND)
            // This C-only helper links SDL2 while the application links SDL3.
            // Isolate just its SDL symbols; deep-binding the C++ board runtime
            // would also split libstdc++ state from the host executable.
            window_load_flags |= RTLD_DEEPBIND;
#endif
            window_handle = dlopen(window_module.c_str(), window_load_flags);
            if (!window_handle)
                throw std::runtime_error("could not load Neo Geo SDL window module " + window_module.string() + ": " + dlerror());
        }
        void* handle = dlopen(runtime_module.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            const char* loader_error = dlerror();
            const std::string error = loader_error ? loader_error : "unknown dynamic loader error";
            if (window_handle) dlclose(window_handle);
            throw std::runtime_error("could not load Neo Geo board module " + runtime_module.string() + ": " + error);
        }
        dlerror();
        using AbiFn = unsigned (*)();
        using IdFn = const char* (*)();
        using RunFn = int (*)(int, char**);
        auto abi = reinterpret_cast<AbiFn>(dlsym(handle, "multiarcaderecomp_board_module_abi"));
        auto id = reinterpret_cast<IdFn>(dlsym(handle, "multiarcaderecomp_board_module_id"));
        auto run = reinterpret_cast<RunFn>(dlsym(handle, "multiarcaderecomp_board_run"));
        const char* symbol_error = dlerror();
        if (symbol_error || !abi || !id || !run || abi() != 1 || std::string_view(id()) != "neogeo") {
            dlclose(handle);
            if (window_handle) dlclose(window_handle);
            throw std::runtime_error("Neo Geo board module ABI mismatch: " + runtime_module.string());
        }
        std::cout << "Launching " << game << " on Neo Geo "
                  << (board == "mvs" ? "MVS" : "AES") << " board module.\n" << std::flush;
        const int status = run(argc, argv);
        dlclose(handle);
        if (window_handle) dlclose(window_handle);
        return status;
#else
        throw std::runtime_error("Neo Geo board modules require a POSIX dynamic loader");
#endif
    }
    if (machine == "ki" && board == "seattle") {
        if (audio_pan_option_set) throw std::runtime_error("--audio-pan is only supported for Sega games");
        if (game != "ki1" && game != "ki2")
            throw std::runtime_error("Seattle currently supports --game=ki1 or --game=ki2");
        if (build_mode && (window || frames || frame_out))
            throw std::runtime_error("--window, --frames, and --frame-out are run-only options");
        return ki_machine_command(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), game,
                                   rom_zip, build_mode, jobs, force_rebuild,
                                   build_mode ? false : (window || (!headless && frames == 0)),
                                   frames, frame_out);
    }
    if (machine == "sega" || machine == "system16" || machine == "system18") {
        if (!multiarcaderecomp::sega::find_board(board))
            throw std::runtime_error("Sega adapter requires --board=system16a, system16b, or system18");
        if (audio_stereo_spread && !audio_enabled)
            throw std::runtime_error("--audio-pan spread requires --audio");
        return multiarcaderecomp::sega::run_machine_command(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), board, game, rom_zip,
                                    build_mode, audit_mode, jobs, force_rebuild, window, headless,
                                    frames ? std::optional<std::uint64_t>(frames) : std::nullopt,
                                    cycles, frame_out, video_state_out, audio_enabled, audio_stereo_spread,
                                    audio_pan_option_set, ym2151_muted_channels, ym2151_suppress_slot0_channel2,
                                    ym2151_suppress_slot3_channel5,
                                    ym2151_suppress_slot4_channel6, ym2151_suppress_slot5_channel7,
                                    arranged_bgm,
                                    sega_video_backend, sega_compare_video, sega_video_profile,
                                    sega_aot_profile, sega_frame_limit_enabled, cpu_trace_out,
                                    cpu_trace_start.value_or(0), cpu_trace_end.value_or(0),
                                    run_child_process);
    }
    if (machine == "midway" || machine == "tunit") {
        if (board != "tunit")
            throw std::runtime_error("Midway T-Unit adapter requires --board=tunit");
        if (audio_enabled || audio_pan_option_set)
            throw std::runtime_error("T-Unit audio is enabled by its game runner; --audio options are not available");
        return midway_machine_command(std::filesystem::path(MULTIARCADERECOMP_SOURCE_DIR), game,
                                      rom_zip, build_mode, jobs, force_rebuild,
                                      window, headless, frames, cycles, frame_out,
                                      input_replay, nvram, dma_command_log);
    }
    if (input_replay || dma_command_log || (nvram && machine != "neogeo"))
        throw std::runtime_error("--input-replay and --dma-command-log are available for Midway T-Unit; --nvram is available for T-Unit and Neo Geo");
    if (audio_pan_option_set) throw std::runtime_error("--audio-pan is only supported for Sega games");
    if (machine != "cps") throw std::runtime_error("CPS adapter requires --machine=cps");
    if (board == "cps2") {
        if (cycles || frame_out || audio_enabled)
            throw std::runtime_error("CPS2 supports --frames and --headless; --cycles, --frame-out, and --audio are not available yet");
        if (headless && !frames)
            throw std::runtime_error("CPS2 --headless requires --frames <count>");
        if (build_mode && (window || headless || frames))
            throw std::runtime_error("--window, --headless, and --frames are run-only options");

        const std::filesystem::path root(MULTIARCADERECOMP_SOURCE_DIR);
        const auto work_dir = root / "build/cps2/work" / game;
        const auto module = root / "build/cps2" / ("libmultiarcaderecomp_cps2_" + game + ".so");
        const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
            : root.parent_path() / "mame/roms" / (game + ".zip");
        if (!std::filesystem::is_regular_file(rom_path))
            throw std::runtime_error("CPS2 ROM ZIP not found: " + rom_path.string());

        if (build_mode) {
            std::vector<std::string> build_command{
                MULTIARCADERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
                "--system", "cps2", "--game", game, "--jobs", jobs,
                "--rom-dir", rom_path.string()
            };
            if (force_rebuild) build_command.emplace_back("--rebuild");
            std::cout << (force_rebuild ? "Forcing CPS2 rebuild.\n"
                                        : "Building CPS2 game through the CPS2Recomp backend.\n") << std::flush;
            return run_child_process(build_command);
        }

        if (!std::filesystem::is_regular_file(module)) {
            throw std::runtime_error("CPS2 board module is missing; run `multiarcaderecomp build --machine=cps --board=cps2 --game="
                + game + "` first");
        }
        std::vector<std::string> run_arguments{
            "multiarcaderecomp-cps2-" + game, "--rom", rom_path.string(),
            "--eeprom", (work_dir / "eeprom.bin").string(),
            "--settings", (work_dir / "settings.ini").string(),
            "--record-entries", (work_dir / "entries.txt").string(),
            "--cpu", "native"
        };
        if (headless) run_arguments.emplace_back("--headless");
        if (frames) {
            run_arguments.emplace_back("--frames");
            run_arguments.push_back(std::to_string(frames));
        }
        std::vector<char*> argv;
        argv.reserve(run_arguments.size());
        for (auto& argument : run_arguments) argv.push_back(argument.data());

#if defined(__unix__) || defined(__APPLE__)
        void* handle = dlopen(module.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) throw std::runtime_error("could not load CPS2 module " + module.string() + ": " + dlerror());
        dlerror();
        using Cps2RunFn = int (*)(int, char**);
        const std::string entry_name = "multiarcaderecomp_cps2_" + game + "_run";
        auto run = reinterpret_cast<Cps2RunFn>(dlsym(handle, entry_name.c_str()));
        const char* symbol_error = dlerror();
        if (symbol_error || !run) {
            dlclose(handle);
            throw std::runtime_error("CPS2 module entry point is missing: "
                + std::string(symbol_error ? symbol_error : "unknown dynamic loader error"));
        }
        std::cout << "Launching " << game << " on cps/cps2 board module.\n" << std::flush;
        const int status = run(static_cast<int>(argv.size()), argv.data());
        dlclose(handle);
        return status;
#else
        throw std::runtime_error("CPS2 board modules currently require a POSIX dynamic loader");
#endif
    }
    if (board != "cps1")
        throw std::runtime_error("CPS adapter requires --board=cps1 or --board=cps2");
    if (game != "sf2ce" && game != "dino")
        throw std::runtime_error("CPS1 adapter currently supports only --game=sf2ce or --game=dino");

    const std::filesystem::path root(MULTIARCADERECOMP_SOURCE_DIR);
    const auto work_dir = root / "build/cps1/work" / game;
    const auto build_dir = root / "build/cps1/multiarcaderecomp-host";
    const auto generated_roms = work_dir / "roms";
    const auto runner = build_dir / ("multiarcaderecomp_" + game);
    const auto rom_path = rom_zip ? std::filesystem::absolute(*rom_zip)
        : root.parent_path() / "mame/roms" / (game + ".zip");
    if (rom_zip && !std::filesystem::is_regular_file(rom_path))
        throw std::runtime_error("ROM ZIP not found: " + rom_path.string());

    const auto stamp = work_dir / ".multiarcaderecomp-build-stamp";
    bool needs_build = force_rebuild;
    const std::array<std::filesystem::path, 11> required_artifacts{
        runner, build_dir / "libmultiarcaderecomp_cps1.so", build_dir / ("libmultiarcaderecomp_game_" + game + ".so"),
        work_dir / "program.bin", work_dir / "m68k_dynamic_entries.txt",
        work_dir / "z80_program.bin", work_dir / "z80_banked.bin",
        generated_roms / "maincpu.bin", generated_roms / "gfx.bin",
        generated_roms / "audiocpu.bin", generated_roms / (game == "dino" ? "qsound.bin" : "oki.bin")
    };
    for (const auto& artifact : required_artifacts) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(artifact, error) || error) needs_build = true;
    }
    std::error_code stamp_error;
    if (!std::filesystem::is_regular_file(stamp, stamp_error) || stamp_error) {
        needs_build = true;
    } else if (!needs_build) {
        std::ifstream stamp_input(stamp);
        std::string line;
        bool matching_rom_path = false;
        while (std::getline(stamp_input, line)) {
            if (line == "rom=" + rom_path.string()) matching_rom_path = true;
        }
        if (!stamp_input.eof() || !matching_rom_path) needs_build = true;
        const auto stamp_time = std::filesystem::last_write_time(stamp, stamp_error);
        if (stamp_error || has_cps1_build_input_newer_than(root, stamp_time, rom_path))
            needs_build = true;
    }

    if (build_mode) {
        if (!needs_build) {
            std::cout << "CPS-1 build is current; nothing to compile.\n" << std::flush;
            return 0;
        }
        std::vector<std::string> build_command{
            MULTIARCADERECOMP_PYTHON_EXECUTABLE, (root / "tools/recompile.py").string(),
            "--system", "cps1", "--game", game, "--jobs", jobs
        };
        if (rom_zip) {
            build_command.emplace_back("--rom-dir");
            build_command.push_back(rom_path.string());
        }
        std::cout << (force_rebuild ? "Forcing CPS-1 rebuild.\n"
                                    : "CPS-1 build missing or stale; recompiling from the ROM driver.\n") << std::flush;
        const int build_status = run_child_process(build_command);
        if (build_status != 0) return build_status;
        std::filesystem::create_directories(work_dir);
        std::ofstream stamp_file(stamp, std::ios::binary | std::ios::trunc);
        if (!stamp_file) throw std::runtime_error("could not write CPS-1 build stamp: " + stamp.string());
        stamp_file << "game=" << game << '\n' << "rom=" << rom_path.string() << '\n';
        if (!stamp_file) throw std::runtime_error("failed writing CPS-1 build stamp: " + stamp.string());
        return 0;
    }

    if (needs_build) {
        std::string message = "CPS-1 build is missing or stale; run `multiarcaderecomp build --machine=cps --board=cps1 --game="
            + game + "` first";
        if (rom_zip) message += " with the same `--rom-dir " + rom_path.string() + "`";
        throw std::runtime_error(message);
    }

    std::vector<std::string> run_command{
        runner.string(), (work_dir / "program.bin").string(), "9223372036854775807",
        (work_dir / "m68k_dynamic_entries.txt").string(),
        (work_dir / "z80_program.bin").string(), (work_dir / "z80_banked.bin").string(),
        (work_dir / "z80_misses.txt").string(), (work_dir / "z80_fallbacks.log").string(),
        (generated_roms / "gfx.bin").string(), (work_dir / "live-frame.ppm").string(),
        "--window",
        "--m68k-ram-entries", (work_dir / "m68k_ram_entries.txt").string(),
        "--gfxram-snapshot", (work_dir / "m68k_gfxram_snapshot.bin").string()
    };
    if (game == "dino") {
        run_command.emplace_back("--qsound-program");
        run_command.push_back((generated_roms / "audiocpu.bin").string());
        run_command.emplace_back("--qsound-samples");
        run_command.push_back((generated_roms / "qsound.bin").string());
    } else {
        run_command.emplace_back("--oki");
        run_command.push_back((generated_roms / "oki.bin").string());
    }
    std::cout << "Launching " << game << " on " << machine << '/' << board << ".\n" << std::flush;
    return run_child_process(run_command);
}


} // namespace multiarcaderecomp::app
