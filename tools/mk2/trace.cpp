// Bounded diagnostic runner for the MK2 T-Unit prototype.
#include "mk2/mk2_machine.hpp"
#include "core/cpus/interpreter_trace.h"
#include "core/cpus/tms34010/interpreter.hpp"
#include "core/cpus/tms34010/mame_interpreter.hpp"
#include "core/cpus/tms34010/runtime.hpp"
#include "core/cpus/adsp2105/runtime.hpp"
#include "core/cpus/interpreter_trace.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <unordered_map>

#ifdef MULTIARCADERECOMP_MK2_SDL
#include <SDL3/SDL.h>
#endif

namespace {
struct InputReplayEvent {
    std::uint64_t cycle{};
    std::uint16_t in0{0xffff};
    std::uint16_t in1{0xffff};
};

std::vector<InputReplayEvent> read_input_replay(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open input replay " + path.string());
    std::vector<InputReplayEvent> events;
    std::string line;
    std::uint64_t previous_cycle = 0;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::istringstream row(line);
        std::uint64_t cycle{};
        std::string in0_text, in1_text;
        if (!(row >> cycle >> in0_text >> in1_text))
            throw std::runtime_error("invalid input replay row in " + path.string());
        std::size_t consumed0 = 0, consumed1 = 0;
        const auto in0 = std::stoul(in0_text, &consumed0, 0);
        const auto in1 = std::stoul(in1_text, &consumed1, 0);
        if (consumed0 != in0_text.size() || consumed1 != in1_text.size() ||
            in0 > 0xffff || in1 > 0xffff || (!events.empty() && cycle < previous_cycle))
            throw std::runtime_error("invalid or out-of-order input replay row in " + path.string());
        events.push_back({cycle, static_cast<std::uint16_t>(in0),
                          static_cast<std::uint16_t>(in1)});
        previous_cycle = cycle;
    }
    return events;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

void save_cmos_file(const std::filesystem::path& path,
                    std::span<const std::uint8_t> data) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + temporary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out) throw std::runtime_error("failed writing " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot replace NVRAM " + path.string() +
                                 ": " + error.message());
    }
}

void write_ppm(std::span<const std::uint32_t> frame,
               const std::filesystem::path& path) {
    constexpr auto width = multiarcaderecomp::mk::TUnit::frame_width;
    constexpr auto height = multiarcaderecomp::mk::TUnit::frame_height;
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto pixel : frame) {
        const char rgb[] = {static_cast<char>(pixel >> 16),
                            static_cast<char>(pixel >> 8),
                            static_cast<char>(pixel)};
        out.write(rgb, sizeof(rgb));
    }
}

#ifdef MULTIARCADERECOMP_MK2_SDL
struct LiveWindow {
    static constexpr auto frame_period = std::chrono::nanoseconds(18'279'250);
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    SDL_AudioStream* audio_stream{};
    bool running{true};
    bool coin_pulse{};
    std::chrono::steady_clock::time_point next_frame{};
    std::uint64_t frames{};
    bool deadline_initialized{};

    LiveWindow() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL video/audio init failed: ") + SDL_GetError());
        window = SDL_CreateWindow("Mortal Kombat II - T-Unit (prototype)", 1012, 578,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(std::string("SDL window failed: ") + SDL_GetError());
        renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) throw std::runtime_error(std::string("SDL renderer failed: ") + SDL_GetError());
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, multiarcaderecomp::mk::TUnit::frame_width,
            multiarcaderecomp::mk::TUnit::frame_height);
        if (!texture) throw std::runtime_error(std::string("SDL texture failed: ") + SDL_GetError());
        const SDL_AudioSpec audio_spec{SDL_AUDIO_S16, 1, 31'250};
        audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
        if (!audio_stream)
            throw std::runtime_error(std::string("SDL audio stream failed: ") + SDL_GetError());
        if (!SDL_ResumeAudioStreamDevice(audio_stream))
            throw std::runtime_error(std::string("SDL audio start failed: ") + SDL_GetError());
    }

    ~LiveWindow() {
        if (audio_stream) SDL_DestroyAudioStream(audio_stream);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void poll_inputs(multiarcaderecomp::mk::TUnit& board, bool scripted_start,
                     std::uint64_t start_cycle, std::uint64_t cpu_cycles) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
                running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.key == SDLK_5) coin_pulse = true;
        }
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const auto down = [&](SDL_Scancode sc) { return keys && keys[sc]; };
        auto press = [](std::uint16_t& port, bool held, unsigned bit) {
            if (held) port &= static_cast<std::uint16_t>(~(1u << bit));
        };
        std::uint16_t controls = 0xffff;
        press(controls, down(SDL_SCANCODE_W), 0); press(controls, down(SDL_SCANCODE_S), 1);
        press(controls, down(SDL_SCANCODE_A), 2); press(controls, down(SDL_SCANCODE_D), 3);
        press(controls, down(SDL_SCANCODE_J) || down(SDL_SCANCODE_SPACE), 4);
        press(controls, down(SDL_SCANCODE_K), 5); press(controls, down(SDL_SCANCODE_L), 6);
        press(controls, down(SDL_SCANCODE_UP), 8); press(controls, down(SDL_SCANCODE_DOWN), 9);
        press(controls, down(SDL_SCANCODE_LEFT), 10); press(controls, down(SDL_SCANCODE_RIGHT), 11);
        press(controls, down(SDL_SCANCODE_KP_1), 12);
        press(controls, down(SDL_SCANCODE_KP_2), 13);
        press(controls, down(SDL_SCANCODE_KP_3), 14);
        board.set_input(0, controls);
        std::uint16_t extra = 0xffff;
        press(extra, down(SDL_SCANCODE_U), 0); press(extra, down(SDL_SCANCODE_I), 1);
        press(extra, down(SDL_SCANCODE_O), 2);
        press(extra, down(SDL_SCANCODE_KP_4), 4);
        press(extra, down(SDL_SCANCODE_KP_5), 5);
        press(extra, down(SDL_SCANCODE_KP_6), 6);
        board.set_input(2, extra);
        std::uint16_t system = 0xffff;
        press(system, coin_pulse, 0);
        press(system, down(SDL_SCANCODE_1) || down(SDL_SCANCODE_RETURN) ||
                    (scripted_start && cpu_cycles >= start_cycle), 2);
        press(system, down(SDL_SCANCODE_2), 5);
        board.set_input(1, system);
        board.set_input(3, 0xfc7d);
        coin_pulse = false;
    }

    void present(std::span<const std::uint32_t> frame,
                 const multiarcaderecomp::mk::TUnit::DisplayParams& display,
                 std::span<const std::int16_t> audio_samples) {
        if (!audio_samples.empty() &&
            !SDL_PutAudioStreamData(audio_stream, audio_samples.data(),
                static_cast<int>(audio_samples.size_bytes())))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        constexpr int width = multiarcaderecomp::mk::TUnit::frame_width;
        constexpr int height = multiarcaderecomp::mk::TUnit::frame_height;
        SDL_UpdateTexture(texture, nullptr, frame.data(), width * sizeof(std::uint32_t));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const int left = std::clamp<int>(display.hblank_start, 0, width);
        const int right = std::clamp<int>(display.hblank_end, left, width);
        const int top = std::clamp<int>(display.visible_start, 0, height);
        const int bottom = std::clamp<int>(display.blank_start, top, height);
        SDL_FRect source{static_cast<float>(left), static_cast<float>(top),
            static_cast<float>(right - left), static_cast<float>(bottom - top)};
        if (source.w <= 0 || source.h <= 0)
            source = SDL_FRect{0, 0, static_cast<float>(width), static_cast<float>(height)};
        int out_w = 0, out_h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h);
        float dst_w = static_cast<float>(out_w);
        float dst_h = dst_w * source.h / source.w;
        if (dst_h > out_h) {
            dst_h = static_cast<float>(out_h);
            dst_w = dst_h * source.w / source.h;
        }
        SDL_FRect destination{(out_w - dst_w) * 0.5f, (out_h - dst_h) * 0.5f,
                              dst_w, dst_h};
        SDL_RenderTexture(renderer, texture, &source, &destination);
        SDL_RenderPresent(renderer);
        const auto now = std::chrono::steady_clock::now();
        if (!deadline_initialized) {
            next_frame = now;
            deadline_initialized = true;
        }
        next_frame += frame_period;
        if (now < next_frame) std::this_thread::sleep_until(next_frame);
        else if (now - next_frame > frame_period) next_frame = now;
        ++frames;
    }
};
#endif
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: multiarcaderecomp_mk2_trace <work-dir> [cycles] [frame.ppm] [--window] [--input-replay=PATH] [--press-continue-at=CYCLES] [--press-coin-at=CYCLES ...] [--press-start-at=CYCLES] [--nvram=PATH] [--summary-log=PATH] [--adsp-overlay-snapshot=PATH] [--dma-boundary-log=PATH] [--dma-command-log=PATH] [--tms-interpreter-only|--interpreter-only|--recompiler-only]\n";
        return 2;
    }
    try {
        const std::filesystem::path work = argv[1];
        std::uint64_t limit = 50'000'000ull;
        bool explicit_limit = false;
        std::uint64_t start_cycle = 0;
        bool start_scheduled = false;
        std::uint64_t continue_cycle = 0;
        bool continue_scheduled = false;
        std::vector<std::uint64_t> coin_cycles;
        bool window_requested = false;
        bool tms_interpreter_only = false;
        bool interpreter_only = false;
        bool recompiler_only = false;
        std::filesystem::path dma_boundary_log;
        std::filesystem::path dma_command_log;
        std::filesystem::path adsp_overlay_snapshot;
        std::filesystem::path nvram_path;
        std::filesystem::path input_replay_path;
        std::filesystem::path frame_path = work / "mk2-frame.ppm";
        std::filesystem::path summary_path = work / "mk2-runtime-summary.log";
        int next_arg = 2;
        if (next_arg < argc && argv[next_arg][0] != '-') {
            limit = std::stoull(argv[next_arg++], nullptr, 0);
            explicit_limit = true;
        }
        if (next_arg < argc && argv[next_arg][0] != '-')
            frame_path = argv[next_arg++];
        for (; next_arg < argc; ++next_arg) {
            const std::string arg = argv[next_arg];
            if (arg == "--window") {
                window_requested = true;
            } else if (arg == "--tms-interpreter-only") {
                tms_interpreter_only = true;
            } else if (arg == "--interpreter-only") {
                interpreter_only = true;
                tms_interpreter_only = true;
            } else if (arg == "--recompiler-only") {
                recompiler_only = true;
            } else if (arg.rfind("--summary-log=", 0) == 0) {
                summary_path = arg.substr(14);
            } else if (arg.rfind("--dma-boundary-log=", 0) == 0) {
                dma_boundary_log = arg.substr(19);
            } else if (arg.rfind("--dma-command-log=", 0) == 0) {
                dma_command_log = arg.substr(18);
            } else if (arg.rfind("--adsp-overlay-snapshot=", 0) == 0) {
                adsp_overlay_snapshot = arg.substr(24);
            } else if (arg.rfind("--nvram=", 0) == 0) {
                nvram_path = arg.substr(8);
            } else if (arg.rfind("--input-replay=", 0) == 0) {
                input_replay_path = arg.substr(15);
            } else if (arg.rfind("--press-start-at=", 0) == 0) {
                start_cycle = std::stoull(arg.substr(17), nullptr, 0);
                start_scheduled = true;
            } else if (arg.rfind("--press-continue-at=", 0) == 0) {
                continue_cycle = std::stoull(arg.substr(20), nullptr, 0);
                continue_scheduled = true;
            } else if (arg.rfind("--press-coin-at=", 0) == 0) {
                coin_cycles.push_back(std::stoull(arg.substr(16), nullptr, 0));
            } else {
                throw std::runtime_error("unknown option " + arg);
            }
        }
        if (interpreter_only && recompiler_only)
            throw std::runtime_error("--interpreter-only and --recompiler-only are mutually exclusive");
#ifndef MULTIARCADERECOMP_MK2_SDL
        if (window_requested)
            throw std::runtime_error("--window requires an SDL3-enabled build");
#endif
        if (window_requested && !explicit_limit)
            limit = ~std::uint64_t{0};
        if (!input_replay_path.empty() && window_requested)
            throw std::runtime_error("--input-replay cannot be combined with --window");
        const auto input_replay = input_replay_path.empty()
            ? std::vector<InputReplayEvent>{} : read_input_replay(input_replay_path);
        std::size_t input_replay_index = 0;
        const auto main = read_file(work / "maincpu.bin");
        const auto graphics = read_file(work / "video.bin");
        const auto sound = read_file(work / "dcs_sound.bin");
        multiarcaderecomp::mk2::MK2Machine machine(main, graphics, sound);
        if (nvram_path.empty()) nvram_path = work / "mk2.nvram";
        if (std::filesystem::exists(nvram_path)) {
            const auto saved_cmos = read_file(nvram_path);
            if (!machine.tunit().load_cmos(saved_cmos))
                throw std::runtime_error("NVRAM has wrong size: " + nvram_path.string());
        }
        multiarcaderecomp_interpreter_trace_set_summary_log(summary_path.c_str());
        auto& board = machine.tunit();
        if (!dma_boundary_log.empty() || !dma_command_log.empty())
            board.enable_dma_history(true);
        auto tms_bus = board.cpu_bus();
        multiarcaderecomp::tms34010::State cpu{};
        cpu.pc = multiarcaderecomp::tms34010::entry_pc();
        cpu.st = 0x10;
        std::uint64_t native = 0, interpreted = 0, interrupts = 0;
        std::unordered_map<std::uint32_t, std::uint64_t> native_pc_hits;
        MultiArcadeRecompFallbackReason reason = MULTIARCADERECOMP_UNCOVERED_ENTRY;
        auto& adsp = machine.dcs();
        adsp.set_reset(true);
        bool recompiler_only_stop = false;
        auto next_sound_cycle = std::uint64_t{0};
        std::uint64_t sound_ratio_remainder = 0;
#ifdef MULTIARCADERECOMP_MK2_SDL
        std::unique_ptr<LiveWindow> live;
        if (window_requested) live = std::make_unique<LiveWindow>();
#endif
        while (cpu.cycles < limit) {
#ifdef MULTIARCADERECOMP_MK2_SDL
            if (live && !live->running) break;
#endif
            if (!input_replay.empty()) {
                while (input_replay_index < input_replay.size() &&
                       input_replay[input_replay_index].cycle <= cpu.cycles) {
                    const auto& event = input_replay[input_replay_index++];
                    board.set_input(0, event.in0);
                    board.set_input(1, event.in1);
                }
            } else {
                auto system = board.input(1);
                constexpr std::uint64_t scripted_input_pulse_cycles = 2'500'000;
                const auto scripted_pulse = [&](bool scheduled, std::uint64_t at) {
                    return scheduled && cpu.cycles >= at &&
                        cpu.cycles - at < scripted_input_pulse_cycles;
                };
                auto controls = board.input(0);
                if (scripted_pulse(continue_scheduled, continue_cycle))
                    controls &= static_cast<std::uint16_t>(~0x0040u);
                else
                    controls |= 0x0040u;
                board.set_input(0, controls);
                const bool coin_pressed = std::any_of(coin_cycles.begin(), coin_cycles.end(),
                    [&](std::uint64_t at) { return scripted_pulse(true, at); });
                if (!coin_cycles.empty()) {
                    if (coin_pressed)
                        system &= static_cast<std::uint16_t>(~0x0001u);
                    else
                        system |= 0x0001u;
                }
                if (start_scheduled) {
                    if (scripted_pulse(true, start_cycle))
                        system &= static_cast<std::uint16_t>(~0x0004u);
                    else
                        system |= 0x0004u;
                }
                board.set_input(1, system);
            }
            const auto before = cpu.cycles;
            const auto instruction_pc = cpu.pc;
            board.note_cpu_state(cpu);
            auto result = multiarcaderecomp::tms34010::BlockExit::NeedsInterpreter;
            bool native_candidate = false;
            if (!tms_interpreter_only) {
                if (const auto block = multiarcaderecomp::tms34010::lookup(cpu.pc)) {
                    native_candidate = true;
                    result = block(cpu, tms_bus, limit);
                }
            }
            if (result == multiarcaderecomp::tms34010::BlockExit::NeedsInterpreter &&
                !recompiler_only) {
                result = multiarcaderecomp::tms34010::execute_mame_interpreter(
                    cpu, tms_bus, limit);
                if (result == multiarcaderecomp::tms34010::BlockExit::NeedsInterpreter)
                    result = multiarcaderecomp::tms34010::interpreter_step(
                        cpu, tms_bus, limit, reason);
                if (result == multiarcaderecomp::tms34010::BlockExit::Dispatch)
                    ++interpreted;
            } else if (result == multiarcaderecomp::tms34010::BlockExit::Dispatch) {
                ++native;
                if (native_candidate) ++native_pc_hits[instruction_pc];
            }
            if (result != multiarcaderecomp::tms34010::BlockExit::Dispatch &&
                result != multiarcaderecomp::tms34010::BlockExit::Budget) {
                const auto stop_pc = cpu.pc;
                const auto stop_opcode = tms_bus.read16
                    ? tms_bus.read16(tms_bus.opaque, stop_pc) : 0;
                const bool has_aot_block =
                    multiarcaderecomp::tms34010::lookup(stop_pc) != nullptr;
                std::cerr << "TMS stopped at PC=0x" << std::hex << stop_pc
                          << " opcode=0x" << stop_opcode << " reason="
                          << static_cast<int>(result)
                          << (has_aot_block ? " (AOT block declined execution)"
                                            : " (no AOT block)")
                          << std::dec << '\n';
                break;
            }
            const auto elapsed = cpu.cycles - before;
            const auto already_advanced = board.take_pre_advanced_cycles();
            if (already_advanced < elapsed)
                board.advance_cycles(elapsed - already_advanced);
            if (result != multiarcaderecomp::tms34010::BlockExit::Budget &&
                multiarcaderecomp::tms34010::service_interrupt(cpu, tms_bus))
                ++interrupts;
            sound_ratio_remainder += elapsed * 8;
            next_sound_cycle += sound_ratio_remainder / 5; // 6.25 MHz : 10 MHz.
            sound_ratio_remainder %= 5;
            if (next_sound_cycle > adsp.cpu().cycles) {
                const auto adsp_exit = adsp.run_until(
                    next_sound_cycle, interpreter_only, recompiler_only);
                if (recompiler_only &&
                    adsp_exit == multiarcaderecomp::adsp2105::BlockExit::NeedsInterpreter) {
                    const auto pc = adsp.cpu().pc & multiarcaderecomp::adsp2105::pc_mask;
                    const auto stop_opcode = adsp.diagnostics().stopped_opcode;
                    const bool has_aot_address = multiarcaderecomp::adsp2105::lookup(pc) != nullptr;
                    const bool has_exact_aot_block =
                        multiarcaderecomp::adsp2105::lookup(pc, stop_opcode) != nullptr;
                    const bool dynamic_code = machine.dcs().program_word_was_written(pc);
                    std::cerr << "ADSP-2105 stopped at PC=0x" << std::hex
                              << pc << " opcode=0x" << stop_opcode
                              << (has_exact_aot_block
                                  ? " (AOT block declined execution)"
                                  : has_aot_address
                                  ? " (no AOT variant for live opcode)"
                                  : (dynamic_code
                                      ? " (dynamic program word has no AOT block)"
                                      : " (no AOT block)"))
                              << std::dec << '\n';
                    recompiler_only_stop = true;
                }
            }
#ifdef MULTIARCADERECOMP_MK2_SDL
            if (live && board.take_completed_frames()) {
                if (input_replay.empty())
                    live->poll_inputs(board, start_scheduled, start_cycle, cpu.cycles);
                const auto samples = adsp.take_samples();
                live->present(board.framebuffer(), board.current_display_params(), samples);
            }
#endif
            if (recompiler_only_stop) break;
            if (result == multiarcaderecomp::tms34010::BlockExit::Budget) break;
        }
        write_ppm(board.framebuffer(), frame_path);
        const auto d = adsp.diagnostics();
        const auto tms_total = native + interpreted;
        const auto& adsp_cpu = adsp.cpu();
        const auto adsp_total = adsp_cpu.native_instructions +
                                adsp_cpu.interpreted_instructions;
        std::cout << "TMS34010 instructions: native/recompiled=" << native
                  << " interpreted=" << interpreted << " total="
                  << tms_total << '\n';
        std::cout << "ADSP-2105 instructions: native/recompiled="
                  << adsp_cpu.native_instructions << " interpreted="
                  << adsp_cpu.interpreted_instructions << " total="
                  << adsp_total << '\n';
        multiarcaderecomp_interpreter_trace_flush();
        std::vector<std::pair<std::uint32_t, std::uint64_t>> hot_native(
            native_pc_hits.begin(), native_pc_hits.end());
        std::sort(hot_native.begin(), hot_native.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        std::cout << "TMS AOT hot PCs:";
        for (std::size_t i = 0; i < std::min<std::size_t>(16, hot_native.size()); ++i)
            std::cout << " 0x" << std::hex << hot_native[i].first << std::dec
                      << '=' << hot_native[i].second;
        std::cout << '\n';
        std::ofstream runtime_summary(summary_path, std::ios::app);
        if (runtime_summary) {
            runtime_summary << "TMS AOT hot PCs:";
            for (std::size_t i = 0; i < std::min<std::size_t>(16, hot_native.size()); ++i)
                runtime_summary << " 0x" << std::hex << hot_native[i].first << std::dec
                                << '=' << hot_native[i].second;
            runtime_summary << '\n';
            runtime_summary << "TMS34010 instructions: native/recompiled="
                << native << " interpreted=" << interpreted << " total="
                << tms_total << '\n';
            runtime_summary << "ADSP-2105 instructions: native/recompiled="
                << adsp_cpu.native_instructions << " interpreted="
                << adsp_cpu.interpreted_instructions << " total="
                << adsp_total << '\n';
        }
        std::cout << "MK2 TMS cycles=" << cpu.cycles << " pc=0x" << std::hex
                  << cpu.pc << std::dec
                  << " interrupts=" << interrupts << " frames="
                  << board.video_diagnostics().completed_frames << '\n';
        const auto video = board.video_diagnostics();
        const auto frame = board.framebuffer();
        const auto nonblack = std::count_if(frame.begin(), frame.end(),
            [](std::uint32_t pixel) { return (pixel & 0x00ffffffu) != 0; });
        const auto palette = board.palette_ram();
        const auto palette_nonzero = std::count_if(palette.begin(), palette.end(),
            [](std::uint16_t color) { return color != 0; });
        const auto input_reads = board.input_read_counts();
        const auto input_pcs = board.input_last_read_pc();
        const auto dma = board.dma_diagnostics();
        std::cout << "MK2 video scanlines=" << video.scanlines
                  << " visible-lines=" << video.enabled_visible_scanlines
                  << " source-nonzero=" << video.nonzero_source_words
                  << " scanout-unmapped=" << video.scanout_unmapped_indices
                  << " scanout-colored=" << video.scanout_colored_pixels
                  << " last-frame-colored=" << video.last_frame_colored_pixels
                  << " frame-nonblack=" << nonblack
                  << " palette-nonzero=" << palette_nonzero
                  << " DPYCTL=0x" << std::hex << board.io_register(8)
                  << " DPYSTRT=0x" << board.io_register(9)
                  << " DPYADR=0x" << board.io_register(30)
                  << " DPYTAP=0x" << board.io_register(27)
                  << " VEBLNK=0x" << board.io_register(5)
                  << " VSBLNK=0x" << board.io_register(6)
                  << " VTOTAL=0x" << board.io_register(7)
                  << " HEBLNK=0x" << board.io_register(1)
                  << " HSBLNK=0x" << board.io_register(2) << std::dec << '\n';
        std::cout << "MK2 input ports IN0=" << std::hex << board.input(0)
                  << " IN1=" << board.input(1) << " IN2=" << board.input(2)
                  << " DSW=" << board.input(3) << std::dec
                  << " reads=" << input_reads[0] << ',' << input_reads[1]
                  << ',' << input_reads[2] << ',' << input_reads[3]
                  << " last-PC=0x" << std::hex << input_pcs[0] << "/0x"
                  << input_pcs[1] << "/0x" << input_pcs[2] << "/0x"
                  << input_pcs[3] << std::dec << '\n';
        std::cout << "MK2 TMS state ST=0x" << std::hex << cpu.st
                  << " SP=0x" << cpu.sp << " A0=0x" << cpu.a[0]
                  << " A1=0x" << cpu.a[1] << " A2=0x" << cpu.a[2]
                  << " INTPEND=0x" << board.io_register(18)
                  << " INTENB=0x" << board.io_register(17) << std::dec << '\n';
        std::cout << "MK2 DMA commands=" << dma.commands
                  << " completed=" << dma.completed
                  << " output-pixels=" << dma.output_pixels
                  << " current=0x" << std::hex << dma.current_command
                  << " last=0x" << dma.last_command
                  << " source=0x" << dma.last_source << std::dec
                  << " busy-cycles=" << dma.cycles_remaining << '\n';
        if (!dma_boundary_log.empty()) {
            std::ofstream dma_log(dma_boundary_log, std::ios::trunc);
            if (!dma_log)
                throw std::runtime_error("cannot write " + dma_boundary_log.string());
            dma_log << "# T-Unit DMA commands near horizontal screen boundaries\n"
                    << "# seq cmd x y width height clip-left clip-right source pixels-written\n";
            std::uint64_t logged = 0;
            for (const auto& command : board.dma_command_history()) {
                const unsigned x = command.x_start & 0x03ff;
                const unsigned width = command.width & 0x03ff;
                const bool near_boundary = x < 64 || x >= 442 ||
                    x + width > 506 || command.left_clip < 64 ||
                    command.right_clip >= 442;
                if (!near_boundary) continue;
                dma_log << command.sequence << " 0x" << std::hex << command.command
                        << std::dec << ' ' << x << ' '
                        << (command.y_start & 0x01ff) << ' ' << width << ' '
                        << (command.height & 0x03ff) << ' '
                        << (command.left_clip & 0x03ff) << ' '
                        << (command.right_clip & 0x03ff) << " 0x"
                        << std::hex << command.source << std::dec << ' '
                        << command.pixels_written << '\n';
                ++logged;
            }
            std::cout << "MK2 DMA boundary log=" << dma_boundary_log
                      << " records=" << logged << '\n';
        }
        if (!dma_command_log.empty()) {
            std::ofstream dma_log(dma_command_log, std::ios::trunc);
            if (!dma_log)
                throw std::runtime_error("cannot write " + dma_command_log.string());
            dma_log << "# seq cmd config source x y width height palette color xstep ystep "
                       "r12 r13 r16 r17 cpu_pc cpu_cycles A0 A1 A2 A3 A4 A5 A6 A7 "
                       "A8 A9 A10 A11 A12 A13 A14 B0 B1 B2 B3 B4 B5 B6 B7 "
                       "B8 B9 B10 B11 B12 B13 B14\n";
            for (const auto& command : board.dma_command_history()) {
                dma_log << std::dec << command.sequence << " 0x" << std::hex << command.command
                        << " 0x" << command.config << " 0x" << command.source
                        << std::dec << ' ' << (command.x_start & 0x03ff) << ' '
                        << (command.y_start & 0x01ff) << ' '
                        << (command.width & 0x03ff) << ' '
                        << (command.height & 0x03ff) << " 0x" << std::hex
                        << command.palette << " 0x" << command.color << " 0x"
                        << command.x_step << " 0x" << command.y_step << " 0x"
                        << command.top_clip << " 0x" << command.bottom_clip << " 0x"
                        << command.left_clip << " 0x" << command.right_clip << " 0x"
                        << command.cpu_pc << ' ' << std::dec
                        << command.cpu_cycles << std::hex;
                for (const auto value : command.cpu_registers)
                    dma_log << " 0x" << value;
                dma_log << '\n';
            }
            std::cout << "MK2 DMA command log=" << dma_command_log
                      << " records=" << board.dma_command_history().size() << '\n';
        }
        std::cout << "MK2 palette entries:";
        for (std::size_t i = 0, shown = 0; i < palette.size() && shown < 16; ++i) {
            if (palette[i]) {
                std::cout << ' ' << std::hex << i << "=0x" << palette[i];
                ++shown;
            }
        }
        std::cout << std::dec << '\n';
        std::cout << "DCS2K commands=" << d.host_commands << " reads="
                  << d.host_reads << " empty-reads=" << d.empty_output_reads
                  << " host-stall-cycles=" << d.host_stall_cycles_total
                  << " ADSP cycles=" << adsp.cpu().cycles
                  << " pc=0x" << std::hex << adsp.cpu().pc << std::dec
                  << " native=" << adsp.cpu().native_instructions
                  << " interpreted=" << adsp.cpu().interpreted_instructions
                  << " samples=" << d.sport_words
                  << " nonzero-samples=" << d.nonzero_samples
                  << " peak=" << d.sample_peak
                  << " last=0x" << std::hex
                  << static_cast<std::uint16_t>(d.last_sample) << std::dec
                  << " local-RAM-writes=" << d.local_ram_writes
                  << " local-RAM-nonzero=" << d.local_ram_nonzero_words
                  << " local-RAM-last=0x" << std::hex
                  << d.local_ram_last_address << ":0x"
                  << d.local_ram_last_value << "@PC=0x"
                  << d.local_ram_last_pc << std::dec
                  << " stream-I" << d.stream_ireg << "=0x" << std::hex
                  << d.stream_address << " base=0x" << d.stream_base
                  << " stop-PC=0x" << d.stopped_pc
                  << " stop-opcode=0x" << d.stopped_opcode
                  << " stop-reason=0x" << static_cast<unsigned>(d.stop_reason)
                  << " L=0x" << d.stream_length
                  << " M=0x" << d.stream_increment
                  << " bank=0x" << d.sound_bank << std::dec
                  << " stalled=" << d.cpu_stalled << '\n';
        save_cmos_file(nvram_path, board.cmos());
        if (!adsp_overlay_snapshot.empty()) {
            const auto overlay = machine.dcs().program_overlay();
            if (!adsp_overlay_snapshot.parent_path().empty())
                std::filesystem::create_directories(adsp_overlay_snapshot.parent_path());
            std::ofstream out(adsp_overlay_snapshot, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot write " + adsp_overlay_snapshot.string());
            for (const auto& [address, word] : overlay) {
                const char record[] = {
                    static_cast<char>(address >> 8), static_cast<char>(address),
                    static_cast<char>(word >> 16), static_cast<char>(word >> 8),
                    static_cast<char>(word)};
                out.write(record, sizeof(record));
            }
            if (!out) throw std::runtime_error("failed writing " + adsp_overlay_snapshot.string());
            std::cout << "ADSP program overlay snapshot=" << adsp_overlay_snapshot
                      << " observed-variants=" << overlay.size() << '\n';
        }
        std::cout << "MK2 CMOS NVRAM=" << nvram_path << " bytes="
                  << board.cmos().size() << '\n';
        std::cout << "frame: " << frame_path << '\n';
        std::cout << "interpreter/runtime summary: " << summary_path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mk2-trace: " << e.what() << '\n';
        return 1;
    }
}
