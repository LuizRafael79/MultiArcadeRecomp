#include "midway/vunit/vunit_machine.hpp"
#include "core/cpus/interpreter_trace.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef MAMERECOMP_VUNIT_SDL
#include <SDL3/SDL.h>
#endif

namespace {
enum class VideoBackend { Cpu, OpenGL };

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void load_nvram(const std::filesystem::path& path,
                mamerecomp::vunit::Machine& machine) {
    if (!std::filesystem::exists(path)) return;
    const auto bytes = read_file(path);
    constexpr std::size_t word_count = 0x2000;
    if (bytes.size() != word_count * 4)
        throw std::runtime_error("NVRAM has wrong size (expected " +
                                 std::to_string(word_count * 4) + " bytes): " +
                                 path.string());
    std::vector<std::uint32_t> words(word_count);
    for (std::size_t i = 0; i < word_count; ++i) {
        const auto offset = i * 4;
        words[i] = std::uint32_t(bytes[offset]) |
                   (std::uint32_t(bytes[offset + 1]) << 8) |
                   (std::uint32_t(bytes[offset + 2]) << 16) |
                   (std::uint32_t(bytes[offset + 3]) << 24);
    }
    machine.load_nvram(words);
}

void save_nvram(const std::filesystem::path& path,
                std::span<const std::uint32_t> words) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write NVRAM " + temporary);
        for (const auto word : words) {
            const char bytes[] = {
                static_cast<char>(word), static_cast<char>(word >> 8),
                static_cast<char>(word >> 16), static_cast<char>(word >> 24)};
            output.write(bytes, sizeof(bytes));
        }
        if (!output) throw std::runtime_error("failed writing NVRAM " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("cannot replace NVRAM " + path.string() +
                                 ": " + error.message());
    }
}

void write_ppm(const std::filesystem::path& path,
               std::span<const std::uint8_t> rgb) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    output << "P6\n512 400\n255\n";
    output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
}

std::uint64_t hash_frame(std::span<const std::uint8_t> rgb) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    // Sample one RGB pixel per 64 pixels. It is enough to distinguish held
    // frames from broad 3D scene updates without adding a full-frame hash pass
    // to the timing measurement.
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 64 * 3) {
        hash ^= rgb[i];
        hash *= 1099511628211ull;
        hash ^= rgb[i + 1];
        hash *= 1099511628211ull;
        hash ^= rgb[i + 2];
        hash *= 1099511628211ull;
    }
    return hash;
}

void write_wav(const std::filesystem::path& path,
               std::span<const std::int16_t> samples) {
    if (samples.size() > (0xffffffffu - 36u) / sizeof(std::int16_t))
        throw std::runtime_error("WAV capture exceeds RIFF size limit");
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    const auto u16 = [&output](std::uint16_t v) {
        const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8)};
        output.write(b, sizeof(b));
    };
    const auto u32 = [&output](std::uint32_t v) {
        const char b[] = {static_cast<char>(v), static_cast<char>(v >> 8),
                          static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
        output.write(b, sizeof(b));
    };
    const auto data_bytes = static_cast<std::uint32_t>(samples.size_bytes());
    output.write("RIFF", 4); u32(36 + data_bytes); output.write("WAVE", 4);
    output.write("fmt ", 4); u32(16); u16(1); u16(1); u32(31'250);
    u32(31'250 * sizeof(std::int16_t)); u16(sizeof(std::int16_t)); u16(16);
    output.write("data", 4); u32(data_bytes);
    for (const auto sample : samples) u16(static_cast<std::uint16_t>(sample));
    if (!output) throw std::runtime_error("failed writing " + path.string());
}

#ifdef MAMERECOMP_VUNIT_SDL
struct LiveWindow {
    mamerecomp::vunit::Video* video{};
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    SDL_Texture* texture{};
    SDL_AudioStream* audio_stream{};
    SDL_DisplayMode fullscreen_mode{};
    std::uint64_t submitted_audio_samples{};
    std::uint64_t nonzero_audio_samples{};
    std::uint64_t min_queued_audio_bytes{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_queued_audio_bytes{};
    std::uint32_t audio_input_rate{31'250};
    std::uint32_t audio_input_bytes_per_frame{sizeof(std::int16_t)};
    bool running{true};
    bool fullscreen{};

    explicit LiveWindow(mamerecomp::vunit::Video& video_ref) : video(&video_ref) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL video/audio init failed: ") + SDL_GetError());
        window = SDL_CreateWindow("Cruis'n USA - V-Unit (prototype)", 768, 600,
                                  SDL_WINDOW_RESIZABLE);
        if (!window)
            throw std::runtime_error(std::string("SDL window failed: ") + SDL_GetError());
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) renderer = SDL_CreateRenderer(window, "software");
        if (!renderer)
            throw std::runtime_error(std::string("SDL renderer failed: ") + SDL_GetError());
        // The machine loop owns frame pacing. Do not let SDL's renderer add a
        // second vblank wait on top of it.
        SDL_SetRenderVSync(renderer, 0);
        int renderer_vsync = -1;
        SDL_GetRenderVSync(renderer, &renderer_vsync);
        std::cerr << "V-Unit video renderer=" << SDL_GetRendererName(renderer)
                  << " vsync=" << renderer_vsync << '\n';
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING, 512, 400);
        if (!texture)
            throw std::runtime_error(std::string("SDL texture failed: ") + SDL_GetError());
        const SDL_AudioSpec audio_spec{SDL_AUDIO_S16, 1, 31'250};
        audio_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
        if (!audio_stream)
            throw std::runtime_error(std::string("SDL audio stream failed: ") + SDL_GetError());
        SDL_AudioSpec input_spec{}, output_spec{};
        if (!SDL_GetAudioStreamFormat(audio_stream, &input_spec, &output_spec))
            throw std::runtime_error(std::string("SDL audio format query failed: ") + SDL_GetError());
        audio_input_rate = static_cast<std::uint32_t>(input_spec.freq);
        audio_input_bytes_per_frame = sizeof(std::int16_t) * input_spec.channels;
        std::cerr << "V-Unit SDL audio format input=" << input_spec.freq << "Hz/"
                  << static_cast<unsigned>(input_spec.channels) << "ch output="
                  << output_spec.freq << "Hz/"
                  << static_cast<unsigned>(output_spec.channels) << "ch\n";
        if (!SDL_ResumeAudioStreamDevice(audio_stream))
            throw std::runtime_error(std::string("SDL audio start failed: ") + SDL_GetError());
    }

    ~LiveWindow() {
        if (video) video->disable_gpu_renderer();
        if (audio_stream) {
            std::cerr << "V-Unit SDL audio submitted=" << submitted_audio_samples
                      << " nonzero=" << nonzero_audio_samples
                      << " queued-bytes=" << SDL_GetAudioStreamQueued(audio_stream)
                      << " queue-range-bytes="
                      << (min_queued_audio_bytes == std::numeric_limits<std::uint64_t>::max()
                              ? 0 : min_queued_audio_bytes)
                      << ".." << max_queued_audio_bytes
                      << " queue-min-ms="
                      << (min_queued_audio_bytes == std::numeric_limits<std::uint64_t>::max()
                              ? 0.0 : 1000.0 * min_queued_audio_bytes /
                                  (audio_input_rate * audio_input_bytes_per_frame))
                      << '\n';
        }
        if (audio_stream) SDL_DestroyAudioStream(audio_stream);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void poll(mamerecomp::vunit::Machine& machine) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE))
                running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                (event.key.key == SDLK_F11 ||
                 (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT))))
                toggle_fullscreen();
        }
        const bool* keys = SDL_GetKeyboardState(nullptr);
        std::uint16_t in0 = 0xffff, in1 = 0x007f;
        if (keys && keys[SDL_SCANCODE_5]) in0 &= ~std::uint16_t{0x0001}; // coin
        if (keys && keys[SDL_SCANCODE_1]) in0 &= ~std::uint16_t{0x0004}; // start
        if (keys && (keys[SDL_SCANCODE_F1] || keys[SDL_SCANCODE_F2] ||
                     keys[SDL_SCANCODE_RETURN]))
            in0 &= ~std::uint16_t{0x0010}; // enter/test switch (MAME defaults to F2)
        machine.set_inputs(in0, in1, 0xf9fe);
        // Match the MAME WHEEL port's full calibration range (0x10..0xf0).
        const std::uint8_t wheel = keys && keys[SDL_SCANCODE_LEFT] ? 0x10
            : keys && keys[SDL_SCANCODE_RIGHT] ? 0xf0 : 0x80;
        machine.set_analog(wheel,
            keys && keys[SDL_SCANCODE_UP] ? 0xff : 0x00,
            keys && keys[SDL_SCANCODE_DOWN] ? 0xff : 0x00);
    }

    void toggle_fullscreen() {
        if (fullscreen) {
            if (!SDL_SetWindowFullscreen(window, false)) {
                std::cerr << "[V-Unit window] leaving fullscreen failed: "
                          << SDL_GetError() << '\n';
                return;
            }
            SDL_SetWindowFullscreenMode(window, nullptr);
            fullscreen = false;
            std::cerr << "[V-Unit window] windowed mode\n";
            return;
        }

        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
        if (!display || !desktop || !SDL_GetClosestFullscreenDisplayMode(
                display, desktop->w, desktop->h, desktop->refresh_rate, true,
                &fullscreen_mode)) {
            std::cerr << "[V-Unit window] cannot select an exclusive fullscreen mode: "
                      << SDL_GetError() << '\n';
            return;
        }
        if (!SDL_SetWindowFullscreenMode(window, &fullscreen_mode) ||
            !SDL_SetWindowFullscreen(window, true)) {
            SDL_SetWindowFullscreenMode(window, nullptr);
            std::cerr << "[V-Unit window] entering exclusive fullscreen failed: "
                      << SDL_GetError() << '\n';
            return;
        }
        fullscreen = true;
        std::cerr << "[V-Unit window] exclusive fullscreen " << fullscreen_mode.w
                  << 'x' << fullscreen_mode.h << " @ " << fullscreen_mode.refresh_rate
                  << " Hz\n";
    }

    void present(std::span<const std::uint8_t> rgb,
                 std::span<const std::int16_t> audio_samples) {
        if (!audio_samples.empty()) {
            submitted_audio_samples += audio_samples.size();
            nonzero_audio_samples += static_cast<std::uint64_t>(std::count_if(
                audio_samples.begin(), audio_samples.end(),
                [](std::int16_t sample) { return sample != 0; }));
            if (!SDL_PutAudioStreamData(audio_stream, audio_samples.data(),
                    static_cast<int>(audio_samples.size_bytes())))
                throw std::runtime_error(std::string("SDL audio queue failed: ") + SDL_GetError());
            const auto queued = SDL_GetAudioStreamQueued(audio_stream);
            if (queued >= 0) {
                const auto queued_bytes = static_cast<std::uint64_t>(queued);
                min_queued_audio_bytes = std::min(min_queued_audio_bytes, queued_bytes);
                max_queued_audio_bytes = std::max(max_queued_audio_bytes, queued_bytes);
            }
        }
        if (!SDL_UpdateTexture(texture, nullptr, rgb.data(), 512 * 3))
            throw std::runtime_error(std::string("SDL texture update failed: ") + SDL_GetError());
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        int out_w = 0, out_h = 0;
        SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h);
        const float scale = std::min(out_w / 512.0f, out_h / 400.0f);
        SDL_FRect dst{(out_w - 512.0f * scale) * 0.5f,
                      (out_h - 400.0f * scale) * 0.5f,
                      512.0f * scale, 400.0f * scale};
        SDL_RenderTexture(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);
    }
};
#endif
}

int main(int argc, char** argv) try {
    if (argc < 3) {
        std::cerr << "usage: mamerecomp_vunit_trace main.bin dcs.bin "
                     "[--frames=N (0=until window closes)] [--frame=PATH.ppm] "
                     "[--nvram=PATH] [--audio=PATH.wav] [--window] [--profile] "
                     "[--interpreter-only | --c31-interpreter-only | "
                     "--adsp-interpreter-only] "
                     "[--fullscreen] [--frame-stats] [--video-backend=cpu|opengl] "
                     "[--fps] [--scanlines=0..1]\n";
        return 2;
    }
    std::uint64_t frames = 600;
    std::filesystem::path frame_path = "crusnusa-frame.ppm";
    std::filesystem::path nvram_path;
    std::filesystem::path audio_path;
#ifdef MAMERECOMP_VUNIT_SDL
    bool window_requested = false;
    bool fullscreen_requested = false;
#endif
    bool profile_requested = false;
    bool frame_stats_requested = false;
    bool fps_requested = false;
    bool c31_interpreter_only = false;
    bool adsp_interpreter_only = false;
    VideoBackend video_backend = VideoBackend::Cpu;
    float scanline_strength = 0.0f;
    for (int i = 3; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg.starts_with("--frames=")) frames = std::stoull(std::string(arg.substr(9)));
        else if (arg.starts_with("--frame=")) frame_path = arg.substr(8);
        else if (arg.starts_with("--nvram=")) nvram_path = arg.substr(8);
        else if (arg.starts_with("--audio=")) audio_path = arg.substr(8);
#ifdef MAMERECOMP_VUNIT_SDL
        else if (arg == "--window") window_requested = true;
        else if (arg == "--fullscreen") { window_requested = true; fullscreen_requested = true; }
#else
        else if (arg == "--window")
            throw std::runtime_error("--window requires an SDL3-enabled build");
#endif
        else if (arg == "--profile") profile_requested = true;
        else if (arg == "--interpreter-only")
            c31_interpreter_only = adsp_interpreter_only = true;
        else if (arg == "--c31-interpreter-only") c31_interpreter_only = true;
        else if (arg == "--adsp-interpreter-only") adsp_interpreter_only = true;
        else if (arg == "--frame-stats") frame_stats_requested = true;
        else if (arg == "--fps") fps_requested = true;
        else if (arg.starts_with("--scanlines=")) {
            scanline_strength = std::stof(std::string(arg.substr(12)));
            if (scanline_strength < 0.0f || scanline_strength > 1.0f)
                throw std::runtime_error("--scanlines must be between 0 and 1");
        }
        else if (arg.starts_with("--video-backend=")) {
            const auto backend = arg.substr(16);
            if (backend == "cpu") video_backend = VideoBackend::Cpu;
            else if (backend == "opengl") video_backend = VideoBackend::OpenGL;
            else throw std::runtime_error("--video-backend must be cpu or opengl");
        }
        else throw std::runtime_error("unknown option: " + std::string(arg));
    }
    mamerecomp_interpreter_trace_set_profile_enabled(profile_requested);
    auto main_bytes = read_file(argv[1]);
    auto dcs_bytes = read_file(argv[2]);
    if (main_bytes.size() != 0x1000000 || dcs_bytes.size() != 0x1000000)
        throw std::runtime_error("expected 16 MiB main.bin and dcs.bin regions");
    std::vector<std::uint32_t> main_words(main_bytes.size() / 4);
    for (std::size_t i = 0; i < main_words.size(); ++i) {
        const auto j = i * 4;
        main_words[i] = std::uint32_t(main_bytes[j]) |
                        (std::uint32_t(main_bytes[j + 1]) << 8) |
                        (std::uint32_t(main_bytes[j + 2]) << 16) |
                        (std::uint32_t(main_bytes[j + 3]) << 24);
    }
    mamerecomp::vunit::Machine machine(main_words, dcs_bytes);
    machine.set_interpreter_modes(c31_interpreter_only, adsp_interpreter_only);
    const char* execution_mode = c31_interpreter_only && adsp_interpreter_only
        ? "interpreter-only"
        : c31_interpreter_only ? "c31-interpreter-only"
        : adsp_interpreter_only ? "adsp-interpreter-only"
        : "aot+interpreter";
    std::cout << "[V-Unit] execution-mode="
              << execution_mode << '\n';
    machine.video().set_scanline_strength(scanline_strength);
    if (video_backend == VideoBackend::OpenGL) {
#ifdef MAMERECOMP_VUNIT_OPENGL
        machine.video().enable_gpu_renderer();
#else
        throw std::runtime_error("OpenGL V-Unit backend is not in this build; configure with "
                                 "-DMAMERECOMP_ENABLE_VUNIT_OPENGL=ON");
#endif
    }
    if (nvram_path.empty())
        nvram_path = std::filesystem::path(argv[1]).parent_path() / "crusnusa.nvram";
    load_nvram(nvram_path, machine);
#ifdef MAMERECOMP_VUNIT_SDL
    std::unique_ptr<LiveWindow> window;
    if (window_requested) window = std::make_unique<LiveWindow>(machine.video());
    if (window && fullscreen_requested) window->toggle_fullscreen();
#endif
    std::uint64_t completed = 0;
    const bool performance_metrics_requested = fps_requested || frame_stats_requested;
    std::chrono::nanoseconds machine_time{};
    std::chrono::nanoseconds framebuffer_time{};
    std::chrono::nanoseconds present_time{};
    using SteadyClock = std::chrono::steady_clock;
    std::vector<std::int16_t> captured_audio;
    const auto frame_period = std::chrono::duration_cast<SteadyClock::duration>(
        std::chrono::duration<double>(
            (666.0 * 432.0) / (33'333'333.0 / 2.0)));
    const double target_fps = 1.0 /
        std::chrono::duration<double>(frame_period).count();
    constexpr double target_main_clock_hz = 25'000'000.0;
    const auto run_begin = SteadyClock::now();
    auto fps_window_begin = run_begin;
    std::uint64_t fps_window_frames = 0;
    std::uint64_t fps_window_cycles = machine.cpu().cycles;
    std::uint64_t fps_window_page_flips = machine.video().display_page_flips();
    std::uint64_t fps_window_dma_commands = machine.video().dma_commands();
    std::uint64_t fps_window_content_changes = 0;
    std::chrono::nanoseconds fps_window_machine_time{};
    std::chrono::nanoseconds fps_window_framebuffer_time{};
    std::chrono::nanoseconds fps_window_present_time{};
    std::uint64_t previous_frame_hash = 0;
    bool have_previous_frame_hash = false;
    bool logged_video_backend = false;
    auto next_present_deadline = SteadyClock::now();
    for (; (frames == 0 || completed < frames)
#ifdef MAMERECOMP_VUNIT_SDL
         && (!window || window->running)
#endif
         ; ++completed) {
#ifdef MAMERECOMP_VUNIT_SDL
        if (window) window->poll(machine);
#endif
        const auto machine_begin = performance_metrics_requested
            ? std::chrono::steady_clock::now() : SteadyClock::time_point{};
        machine.run_frame();
        if (performance_metrics_requested) {
            const auto machine_end = std::chrono::steady_clock::now();
            const auto elapsed = machine_end - machine_begin;
            machine_time += elapsed;
            fps_window_machine_time += elapsed;
        }
        auto audio_samples = machine.take_audio_samples();
        if (!audio_path.empty())
            captured_audio.insert(captured_audio.end(), audio_samples.begin(),
                                  audio_samples.end());
        bool need_framebuffer = frame_stats_requested;
#ifdef MAMERECOMP_VUNIT_SDL
        need_framebuffer = need_framebuffer || window != nullptr;
#endif
        std::vector<std::uint8_t> rgb;
        if (need_framebuffer) {
            const auto framebuffer_begin = performance_metrics_requested
                ? std::chrono::steady_clock::now() : SteadyClock::time_point{};
            rgb = machine.framebuffer_rgb();
            const auto framebuffer_end = performance_metrics_requested
                ? std::chrono::steady_clock::now() : SteadyClock::time_point{};
            if (!logged_video_backend && video_backend == VideoBackend::OpenGL) {
                const auto& renderer = machine.video().gpu_renderer_name();
                std::cerr << "V-Unit video backend=OpenGL rasterizer="
                          << (renderer.empty() ? "initializing" : renderer) << '\n';
                logged_video_backend = true;
            }
#ifdef MAMERECOMP_VUNIT_SDL
            if (window && performance_metrics_requested) {
                framebuffer_time += framebuffer_end - framebuffer_begin;
                fps_window_framebuffer_time += framebuffer_end - framebuffer_begin;
            }
#else
            (void)framebuffer_begin;
            (void)framebuffer_end;
#endif
            if (frame_stats_requested) {
                const auto hash = hash_frame(rgb);
                if (have_previous_frame_hash && hash != previous_frame_hash)
                    ++fps_window_content_changes;
                previous_frame_hash = hash;
                have_previous_frame_hash = true;
            }
        }
#ifdef MAMERECOMP_VUNIT_SDL
        if (window) {
            const auto present_begin = performance_metrics_requested
                ? std::chrono::steady_clock::now() : SteadyClock::time_point{};
            window->present(rgb, audio_samples);
            const auto present_elapsed = performance_metrics_requested
                ? std::chrono::steady_clock::now() - present_begin
                : SteadyClock::duration{};
            if (performance_metrics_requested) {
                present_time += present_elapsed;
                fps_window_present_time += present_elapsed;
            }
            // SDL's audio stream is asynchronous and the renderer is created
            // without VSync. Pace presentation to MAME's raw screen timing
            // (16.6666665 MHz / 666 / 432 ~= 57.93 Hz) so silence cannot make
            // the game run at host speed.
            next_present_deadline += frame_period;
            std::this_thread::sleep_until(next_present_deadline);
            const auto now = SteadyClock::now();
            if (now - next_present_deadline > frame_period)
                next_present_deadline = now;
        }
#endif
        if (performance_metrics_requested) {
            ++fps_window_frames;
            const auto meter_now = SteadyClock::now();
            const auto fps_window_seconds =
                std::chrono::duration<double>(meter_now - fps_window_begin).count();
            if (fps_window_seconds >= 1.0) {
            const auto current_cycles = machine.cpu().cycles;
            const double actual_fps = fps_window_frames / fps_window_seconds;
            const double main_clock_mhz =
                (current_cycles - fps_window_cycles) / fps_window_seconds / 1'000'000.0;
            const double speed_percent = 100.0 * main_clock_mhz /
                                         (target_main_clock_hz / 1'000'000.0);
            if (fps_requested)
                std::cerr << "[V-Unit FPS] actual=" << actual_fps
                          << " target=" << target_fps
                          << " speed=" << speed_percent << "% C31="
                          << main_clock_mhz << "MHz/25MHz\n";
            if (frame_stats_requested) {
                const auto page_flips = machine.video().display_page_flips();
                const auto dma_commands = machine.video().dma_commands();
                std::cerr << "[V-Unit frame-stats] presented=" << fps_window_frames
                          << " content-changes=" << fps_window_content_changes
                          << " display-page-flips="
                          << (page_flips - fps_window_page_flips)
                          << " dma-commands="
                          << (dma_commands - fps_window_dma_commands)
                          << " machine-ms/frame="
                          << std::chrono::duration<double, std::milli>(
                                 fps_window_machine_time).count() / fps_window_frames
                          << " framebuffer-ms/frame="
                          << std::chrono::duration<double, std::milli>(
                                 fps_window_framebuffer_time).count() / fps_window_frames
                          << " present-ms/frame="
                          << std::chrono::duration<double, std::milli>(
                                 fps_window_present_time).count() / fps_window_frames
                          << '\n';
                fps_window_content_changes = 0;
                fps_window_machine_time = {};
                fps_window_framebuffer_time = {};
                fps_window_present_time = {};
                fps_window_page_flips = page_flips;
                fps_window_dma_commands = dma_commands;
            }
            fps_window_begin = meter_now;
            fps_window_frames = 0;
            fps_window_cycles = current_cycles;
            }
        }
        const auto reason = machine.cpu().stop_reason;
        if (reason == mamerecomp::tms320c31::StopReason::MameUnimplemented ||
            reason == mamerecomp::tms320c31::StopReason::IllegalOpcode ||
            reason == mamerecomp::tms320c31::StopReason::BusFault) {
            ++completed;
            break;
        }
    }
    const auto report = machine.report();
    const auto frame = machine.framebuffer_rgb();
    write_ppm(frame_path, frame);
    if (!audio_path.empty()) {
        write_wav(audio_path, captured_audio);
        std::cout << "V-Unit audio capture=\"" << audio_path.string()
                  << "\" samples=" << captured_audio.size() << '\n';
    }
    save_nvram(nvram_path, machine.nvram());
    const double run_seconds =
        std::chrono::duration<double>(SteadyClock::now() - run_begin).count();
    if (fps_requested && run_seconds > 0.0) {
        const double actual_fps = completed / run_seconds;
        const double main_clock_mhz = report.main_cycles / run_seconds / 1'000'000.0;
        const double speed_percent = 100.0 * main_clock_mhz /
                                     (target_main_clock_hz / 1'000'000.0);
        std::cout << "V-Unit FPS summary actual=" << actual_fps
                  << " target=" << target_fps
                  << " speed=" << speed_percent << "% C31="
                  << main_clock_mhz << "MHz/25MHz wall-seconds="
                  << run_seconds << '\n';
    }
    std::cout << "V-Unit frames=" << completed
              << " execution-mode=" << execution_mode
              << " TMS320C31 instructions: native/recompiled="
              << machine.cpu().native_instructions
              << " interpreted=" << machine.cpu().interpreted_instructions
              << " total=" << report.main_instructions
              << " cycles=" << report.main_cycles
              << " PC=0x" << std::hex << report.main_pc << std::dec
              << " last-PC=0x" << std::hex << machine.cpu().last_pc
              << " opcode=0x" << machine.cpu().last_opcode << std::dec
              << " DMA commands=" << report.dma_commands << '\n';
    std::cout << "C31 fallback categories: AOT_OPCODE_MISMATCH="
              << machine.cpu().aot_opcode_mismatches
              << " UNCOVERED_ENTRY="
              << machine.cpu().uncovered_entry_instructions;
    if (machine.cpu().aot_opcode_mismatches)
        std::cout << " last-mismatch-PC=0x" << std::hex
                  << machine.cpu().last_aot_mismatch_pc
                  << " expected=0x" << machine.cpu().last_aot_mismatch_expected
                  << " live=0x" << machine.cpu().last_aot_mismatch_live << std::dec;
    if (machine.cpu().uncovered_entry_instructions)
        std::cout << " last-uncovered-PC=0x" << std::hex
                  << machine.cpu().last_uncovered_pc << " opcode=0x"
                  << machine.cpu().last_uncovered_opcode << std::dec;
    std::cout << '\n';
    if (std::getenv("MAMERECOMP_C31_HOT_SITES")) {
        std::vector<mamerecomp::tms320c31::State::HotPc> hot;
        for (const auto& site : machine.cpu().interpreted_hot_pcs)
            if (site.count) hot.push_back(site);
        std::sort(hot.begin(), hot.end(), [](const auto& a, const auto& b) {
            return a.count > b.count;
        });
        std::cout << "C31 interpreted hot PCs:";
        for (std::size_t i = 0; i < std::min<std::size_t>(16, hot.size()); ++i)
            std::cout << " 0x" << std::hex << hot[i].pc << std::dec
                      << '=' << hot[i].count;
        std::cout << '\n';
    }
    if (performance_metrics_requested && completed) {
        const auto milliseconds = [completed](std::chrono::nanoseconds elapsed) {
            return std::chrono::duration<double, std::milli>(elapsed).count() /
                   static_cast<double>(completed);
        };
        std::cout << "V-Unit timing avg-frame-ms="
                  << milliseconds(machine_time)
                  << " avg-framebuffer-ms=" << milliseconds(framebuffer_time)
                  << " avg-present-ms=" << milliseconds(present_time) << '\n';
    }
    const auto& cpu_state = machine.cpu();
    std::cout << "TMS320C31 registers" << std::hex;
    for (unsigned i = 0; i < 8; ++i)
        std::cout << " R" << i << "=0x" << cpu_state.r[i].integer();
    for (unsigned i = 0; i < 8; ++i)
        std::cout << " AR" << i << "=0x" << cpu_state.r[8 + i].integer();
    std::cout << " DP=0x" << cpu_state.r[16].integer()
              << " IR0=0x" << cpu_state.r[17].integer()
              << " IR1=0x" << cpu_state.r[18].integer()
              << " BK=0x" << cpu_state.r[19].integer()
              << " SP=0x" << cpu_state.r[20].integer()
              << " ST=0x" << cpu_state.r[21].integer()
              << " IE=0x" << cpu_state.r[22].integer()
              << " IF=0x" << cpu_state.r[23].integer()
              << " RC=0x" << cpu_state.r[27].integer()
              << std::dec << '\n';
    const auto dcs_diag = machine.dcs().diagnostics();
    std::cout << "ADSP-2105 instructions: native/recompiled="
              << machine.dcs().cpu().native_instructions
              << " interpreted=" << machine.dcs().cpu().interpreted_instructions
              << " total=" << (machine.dcs().cpu().native_instructions +
                                 machine.dcs().cpu().interpreted_instructions)
              << " cycles=" << report.audio_cycles
              << " DCS commands=" << dcs_diag.host_commands
              << " overwritten-commands=" << dcs_diag.overwritten_commands
              << " DSP-input-reads=" << dcs_diag.input_reads
              << " host-reads=" << dcs_diag.host_reads
              << " empty-host-reads=" << dcs_diag.empty_output_reads
              << " output-writes=" << dcs_diag.output_writes
              << " samples=" << dcs_diag.sport_words
              << " nonzero-samples=" << dcs_diag.nonzero_samples
              << " peak=" << dcs_diag.sample_peak
              << " last-sample=" << dcs_diag.last_sample << '\n'
              << "DCS SPORT1 I" << dcs_diag.stream_ireg
              << " base=0x" << std::hex << dcs_diag.stream_base
              << " length=0x" << dcs_diag.stream_length
              << " increment=" << std::dec << dcs_diag.stream_increment
              << " address=0x" << std::hex << dcs_diag.stream_address
              << " bank=0x" << dcs_diag.sound_bank << std::dec << '\n'
              << "DCS ROM reads=" << dcs_diag.sound_rom_reads
              << " nonzero=" << dcs_diag.sound_rom_nonzero_reads
              << " 0xffff=" << dcs_diag.sound_rom_ffff_reads
              << " last-address=0x" << std::hex << dcs_diag.sound_rom_last_address
              << " value=0x" << dcs_diag.sound_rom_last_value << std::dec << '\n'
              << "DCS bank-select writes=" << dcs_diag.sound_bank_writes
              << " last-value=0x" << std::hex << dcs_diag.sound_bank_last_value
              << " current=0x" << dcs_diag.sound_bank << std::dec
              << " SPORT-IRQ1-pulses=" << dcs_diag.sport_irq1_pulses
              << " active=" << dcs_diag.sport_irq1_active
              << " ICNTL=0x" << std::hex << static_cast<unsigned>(dcs_diag.adsp_icntl)
              << " IMASK=0x" << dcs_diag.adsp_imask << std::dec
              << " ADSP-timer=" << dcs_diag.timer_enabled
              << " scale=" << dcs_diag.timer_scale
              << " count=" << dcs_diag.timer_count
              << " period=" << dcs_diag.timer_period
              << " phase=" << dcs_diag.timer_phase
              << " IRQ-pending=" << dcs_diag.timer_irq_pending << '\n'
              << "ADSP PC=0x" << std::hex << machine.dcs().cpu().pc
              << " last-PC=0x" << machine.dcs().cpu().previous_pc
              << " opcode=0x";
    const auto dcs_bus = machine.dcs().cpu_bus();
    std::cout << std::hex << (dcs_bus.read_program24
        ? dcs_bus.read_program24(dcs_bus.opaque, machine.dcs().cpu().pc) : 0)
              << " IRQ=";
    for (unsigned i = 0; i < machine.dcs().cpu().irq.size(); ++i)
        std::cout << (machine.dcs().cpu().irq[i] ? '1' : '0');
    std::cout << " input-full=" << dcs_diag.input_full
              << " output-full=" << dcs_diag.output_full
              << " latch=0x" << std::hex << dcs_diag.input_latch << std::dec
              << " boot-count=" << dcs_diag.boot_count
              << " SYSCONTROL-writes=" << dcs_diag.syscontrol_writes
              << " last-SYSCONTROL=0x" << std::hex << dcs_diag.last_syscontrol_value
              << "@PC=0x" << dcs_diag.last_syscontrol_pc << std::dec
              << " software-reboots=" << dcs_diag.software_reboot_count
              << " last-software-reboot=PC:0x" << std::hex
              << dcs_diag.last_software_reboot_pc << ",SYSCONTROL:0x"
              << dcs_diag.last_software_reboot_value << std::dec
              << " reset-asserted=" << dcs_diag.reset_asserted << '\n'
              << "DCS RAM local-writes=" << dcs_diag.local_ram_writes
              << " local-nonzero-words=" << dcs_diag.local_ram_nonzero_words
              << " last-write=0x" << std::hex << dcs_diag.local_ram_last_address
              << ":0x" << dcs_diag.local_ram_last_value
              << "@PC=0x" << dcs_diag.local_ram_last_pc << std::dec << '\n';
    std::cout << "DCS shared-RAM writes=" << dcs_diag.shared_ram_writes
              << " data-writes=" << dcs_diag.shared_ram_data_writes
              << " program-writes=" << dcs_diag.shared_ram_program_writes
              << " nonzero-words=" << dcs_diag.shared_ram_nonzero_words
              << " SPORT-range-nonzero=" << dcs_diag.stream_buffer_nonzero_words
              << " SPORT-range-writes=" << dcs_diag.stream_buffer_writes
              << " SPORT-range-nonzero-writes="
              << dcs_diag.stream_buffer_nonzero_writes
              << " sample-min=0x" << std::hex << dcs_diag.stream_buffer_min
              << " sample-max=0x" << dcs_diag.stream_buffer_max
              << " SPORT-last-write=0x" << dcs_diag.stream_buffer_last_write_address
              << ":0x" << dcs_diag.stream_buffer_last_write_value
              << "@PC=0x" << dcs_diag.stream_buffer_last_write_pc
              << "/op=0x" << dcs_diag.stream_buffer_last_write_opcode
              << " I7=0x" << dcs_diag.stream_buffer_last_write_i7
              << " M7=0x" << static_cast<std::uint16_t>(dcs_diag.stream_buffer_last_write_m7)
              << " L7=0x" << dcs_diag.stream_buffer_last_write_l7
              << " AX0=0x" << static_cast<std::uint16_t>(dcs_diag.stream_buffer_last_write_ax0)
              << " AY0=0x" << static_cast<std::uint16_t>(dcs_diag.stream_buffer_last_write_ay0)
              << " AR=0x" << static_cast<std::uint16_t>(dcs_diag.stream_buffer_last_write_ar)
              << " last-write=0x" << std::hex << dcs_diag.shared_ram_last_address
              << ":0x" << dcs_diag.shared_ram_last_value
              << "@PC=0x" << dcs_diag.shared_ram_last_pc << std::dec << '\n';
    if (machine.cpu().stop_reason != mamerecomp::tms320c31::StopReason::None &&
        machine.cpu().stop_reason != mamerecomp::tms320c31::StopReason::Budget) {
        std::cout << "TMS320C31 stopped at PC=0x" << std::hex
                  << machine.cpu().last_pc << " opcode=0x"
                  << machine.cpu().last_opcode << std::dec
                  << " reason=" << static_cast<unsigned>(machine.cpu().stop_reason) << '\n';
    }
    std::cout << "framebuffer=" << frame_path << " (512x400)"
#ifdef MAMERECOMP_VUNIT_SDL
              << (window_requested ? "; live SDL window was enabled" : "")
#endif
              << '\n';
    std::cout << "V-Unit NVRAM=" << nvram_path << " bytes="
              << machine.nvram().size() * sizeof(std::uint32_t) << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
}
