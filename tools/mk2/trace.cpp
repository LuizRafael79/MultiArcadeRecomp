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
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <unordered_map>

#ifdef MAMERECOMP_MK2_SDL
#include <SDL3/SDL.h>
#endif

namespace {
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_ppm(std::span<const std::uint32_t> frame,
               const std::filesystem::path& path) {
    constexpr auto width = mamerecomp::mk::TUnit::frame_width;
    constexpr auto height = mamerecomp::mk::TUnit::frame_height;
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

#ifdef MAMERECOMP_MK2_SDL
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
            SDL_TEXTUREACCESS_STREAMING, mamerecomp::mk::TUnit::frame_width,
            mamerecomp::mk::TUnit::frame_height);
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

    void poll_inputs(mamerecomp::mk::TUnit& board, bool scripted_start,
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
                 const mamerecomp::mk::TUnit::DisplayParams& display,
                 std::span<const std::int16_t> audio_samples) {
        if (!audio_samples.empty() &&
            !SDL_PutAudioStreamData(audio_stream, audio_samples.data(),
                static_cast<int>(audio_samples.size_bytes())))
            throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
        constexpr int width = mamerecomp::mk::TUnit::frame_width;
        constexpr int height = mamerecomp::mk::TUnit::frame_height;
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
        std::cerr << "usage: mamerecomp_mk2_trace <work-dir> [cycles] [frame.ppm] [--window] [--press-start-at=CYCLES] [--summary-log=PATH] [--tms-interpreter-only]\n";
        return 2;
    }
    try {
        const std::filesystem::path work = argv[1];
        std::uint64_t limit = 50'000'000ull;
        bool explicit_limit = false;
        std::uint64_t start_cycle = 0;
        bool start_scheduled = false;
        bool window_requested = false;
        bool tms_interpreter_only = false;
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
            } else if (arg.rfind("--summary-log=", 0) == 0) {
                summary_path = arg.substr(14);
            } else if (arg.rfind("--press-start-at=", 0) == 0) {
                start_cycle = std::stoull(arg.substr(17), nullptr, 0);
                start_scheduled = true;
            } else {
                throw std::runtime_error("unknown option " + arg);
            }
        }
#ifndef MAMERECOMP_MK2_SDL
        if (window_requested)
            throw std::runtime_error("--window requires an SDL3-enabled build");
#endif
        if (window_requested && !explicit_limit)
            limit = ~std::uint64_t{0};
        const auto main = read_file(work / "maincpu.bin");
        const auto graphics = read_file(work / "video.bin");
        const auto sound = read_file(work / "dcs_sound.bin");
        mamerecomp::mk2::MK2Machine machine(main, graphics, sound);
        mamerecomp_interpreter_trace_set_summary_log(summary_path.c_str());
        auto& board = machine.tunit();
        auto tms_bus = board.cpu_bus();
        mamerecomp::tms34010::State cpu{};
        cpu.pc = mamerecomp::tms34010::entry_pc();
        cpu.st = 0x10;
        std::uint64_t native = 0, interpreted = 0, interrupts = 0;
        std::unordered_map<std::uint32_t, std::uint64_t> native_pc_hits;
        MamerecompFallbackReason reason = MAMERECOMP_UNCOVERED_ENTRY;
        auto& adsp = machine.dcs();
        adsp.set_reset(true);
        auto next_sound_cycle = std::uint64_t{0};
        std::uint64_t sound_ratio_remainder = 0;
#ifdef MAMERECOMP_MK2_SDL
        std::unique_ptr<LiveWindow> live;
        if (window_requested) live = std::make_unique<LiveWindow>();
#endif
        while (cpu.cycles < limit) {
#ifdef MAMERECOMP_MK2_SDL
            if (live && !live->running) break;
#endif
            auto system = board.input(1);
            if (start_scheduled) {
                constexpr std::uint64_t start_pulse_cycles = 120'000;
                if (cpu.cycles >= start_cycle &&
                    cpu.cycles - start_cycle < start_pulse_cycles)
                    system &= static_cast<std::uint16_t>(~0x0004u);
                else
                    system |= 0x0004u;
            }
            board.set_input(1, system);
            const auto before = cpu.cycles;
            const auto instruction_pc = cpu.pc;
            board.note_cpu_pc(instruction_pc);
            auto result = mamerecomp::tms34010::BlockExit::NeedsInterpreter;
            bool native_candidate = false;
            if (!tms_interpreter_only) {
                if (const auto block = mamerecomp::tms34010::lookup(cpu.pc)) {
                    native_candidate = true;
                    result = block(cpu, tms_bus, limit);
                }
            }
            if (result == mamerecomp::tms34010::BlockExit::NeedsInterpreter) {
                result = mamerecomp::tms34010::execute_mame_interpreter(
                    cpu, tms_bus, limit);
                if (result == mamerecomp::tms34010::BlockExit::NeedsInterpreter)
                    result = mamerecomp::tms34010::interpreter_step(
                        cpu, tms_bus, limit, reason);
                if (result == mamerecomp::tms34010::BlockExit::Dispatch)
                    ++interpreted;
            } else if (result == mamerecomp::tms34010::BlockExit::Dispatch) {
                ++native;
                if (native_candidate) ++native_pc_hits[instruction_pc];
            }
            if (result != mamerecomp::tms34010::BlockExit::Dispatch &&
                result != mamerecomp::tms34010::BlockExit::Budget) {
                std::cerr << "TMS stopped at PC=0x" << std::hex << cpu.pc
                          << " reason=" << static_cast<int>(result) << std::dec << '\n';
                break;
            }
            const auto elapsed = cpu.cycles - before;
            board.advance_cycles(elapsed);
            if (result != mamerecomp::tms34010::BlockExit::Budget &&
                mamerecomp::tms34010::service_interrupt(cpu, tms_bus))
                ++interrupts;
            sound_ratio_remainder += elapsed * 8;
            next_sound_cycle += sound_ratio_remainder / 5; // 6.25 : 10 MHz.
            sound_ratio_remainder %= 5;
            if (next_sound_cycle > adsp.cpu().cycles)
                (void)adsp.run_until(next_sound_cycle);
#ifdef MAMERECOMP_MK2_SDL
            if (live && board.take_completed_frames()) {
                live->poll_inputs(board, start_scheduled, start_cycle, cpu.cycles);
                const auto samples = adsp.take_samples();
                live->present(board.framebuffer(), board.current_display_params(), samples);
            }
#endif
            if (result == mamerecomp::tms34010::BlockExit::Budget) break;
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
        mamerecomp_interpreter_trace_flush();
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
        std::cout << "MK2 palette entries:";
        for (std::size_t i = 0, shown = 0; i < palette.size() && shown < 16; ++i) {
            if (palette[i]) {
                std::cout << ' ' << std::hex << i << "=0x" << palette[i];
                ++shown;
            }
        }
        std::cout << std::dec << '\n';
        std::cout << "DCS2K commands=" << d.host_commands << " reads="
                  << d.host_reads << " ADSP cycles=" << adsp.cpu().cycles
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
                  << " L=0x" << d.stream_length
                  << " M=0x" << d.stream_increment
                  << " bank=0x" << d.sound_bank << std::dec
                  << " stalled=" << d.cpu_stalled << '\n';
        std::cout << "frame: " << frame_path << '\n';
        std::cout << "interpreter/runtime summary: " << summary_path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mk2-trace: " << e.what() << '\n';
        return 1;
    }
}
