#include "mips3/mips3.h"
#include "mips3/rec_runtime.h"
#include "core/cpus/interpreter_trace.h"
#include "ki/games/game.h"
#include "ki/runtime/dcs/sound.h"
#include "ki/runtime/seattle/ide/ide.h"
#include "ki/runtime/seattle/machine.hpp"
#include "ki/runtime/seattle/mem.h"

#include <archive.h>
#include <archive_entry.h>

#ifdef MAMERECOMP_KI_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr double host_frame_rate_limit = 60.0;
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool extract_members(const fs::path& zip, const fs::path& output,
                     const std::vector<std::string>& wanted) {
    struct archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_zip(reader);
    if (archive_read_open_filename(reader, zip.c_str(), 64 * 1024) != ARCHIVE_OK) {
        const std::string error = archive_error_string(reader) ? archive_error_string(reader) : "unknown archive error";
        archive_read_free(reader);
        throw std::runtime_error(zip.string() + ": " + error);
    }
    std::vector<bool> found(wanted.size());
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const char* raw = archive_entry_pathname(entry);
        const std::string member = raw ? lowercase(fs::path(raw).filename().string()) : std::string{};
        for (std::size_t i = 0; i < wanted.size(); ++i) {
            if (member != lowercase(wanted[i]) || found[i]) continue;
            fs::create_directories(output);
            std::ofstream file(output / wanted[i], std::ios::binary | std::ios::trunc);
            std::array<char, 64 * 1024> buffer{};
            la_ssize_t count;
            while ((count = archive_read_data(reader, buffer.data(), buffer.size())) > 0)
                file.write(buffer.data(), count);
            if (count < 0 || !file) {
                archive_read_free(reader);
                throw std::runtime_error("failed extracting " + wanted[i] + " from " + zip.string());
            }
            found[i] = true;
            break;
        }
        if (std::all_of(found.begin(), found.end(), [](bool value) { return value; })) break;
        archive_read_data_skip(reader);
    }
    archive_read_free(reader);
    for (std::size_t i = 0; i < found.size(); ++i)
        if (!found[i]) return false;
    return true;
}

std::vector<std::uint8_t> read_file(const fs::path& path, std::size_t expected) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("missing file: " + path.string());
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::size_t>(size) != expected)
        throw std::runtime_error(path.string() + ": unexpected size");
    std::vector<std::uint8_t> data(expected);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input) throw std::runtime_error("failed reading " + path.string());
    return data;
}

fs::path find_rom_zip(const fs::path& rom_arg, const game_t& profile) {
    if (fs::is_regular_file(rom_arg)) return fs::absolute(rom_arg);
    const std::array candidates{
        rom_arg / (std::string(profile.set) + ".zip"),
        rom_arg / profile.set / (std::string(profile.set) + ".zip")
    };
    for (const auto& candidate : candidates) if (fs::is_regular_file(candidate)) return candidate;
    throw std::runtime_error("cannot find " + std::string(profile.set) + ".zip under " + rom_arg.string());
}

fs::path find_chd(const fs::path& rom_root, const fs::path& zip, const fs::path& work,
                  const game_t& profile) {
    const std::array candidates{
        rom_root / profile.set / profile.chd,
        zip.parent_path() / profile.chd,
        zip.parent_path() / profile.set / profile.chd,
        work / "mame_chd_roms" / profile.set / profile.chd,
    };
    for (const auto& candidate : candidates) if (fs::is_regular_file(candidate)) return candidate;
    throw std::runtime_error("CHD not found: expected " + std::string(profile.set) + "/" + profile.chd
        + " under ROM root or build/ki/work/ki1/mame_chd_roms");
}

void save_ppm(const fs::path& path) {
    std::vector<std::uint8_t> rgb(mamerecomp::ki::seattle::frame_width *
                                  mamerecomp::ki::seattle::frame_height * 3);
    mamerecomp::ki::seattle::copy_rgb(rgb);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create framebuffer output: " + path.string());
    out << "P6\n320 240\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
}

int run(const fs::path& rom_arg, const fs::path& work, std::uint64_t frame_limit,
        bool window, bool interpreter_only, const fs::path& frame_out,
        const std::string& game_id) {
    const auto* selected_game = game_find(game_id.c_str());
    if (!selected_game) throw std::runtime_error("unknown KI game: " + game_id);
    game_select(selected_game);
    const auto zip = find_rom_zip(rom_arg, game);
    const auto rom_root = fs::is_directory(rom_arg) ? fs::absolute(rom_arg) : zip.parent_path();
    const auto staged = work / "runtime_roms";
    std::vector<std::string> names{game.versions[0].rom};
    for (const auto* name : game.sound_roms) if (name) names.emplace_back(name);
    bool staged_ok = true;
    for (const auto& name : names) staged_ok &= fs::is_regular_file(staged / name);
    if (!staged_ok && !extract_members(zip, staged, names))
        throw std::runtime_error("the selected MAME set ZIP is missing one or more game ROMs");

    const auto boot = read_file(staged / game.versions[0].rom, ROM_SIZE);
    std::copy(boot.begin(), boot.end(), rom);
    if (!sound_init(staged.c_str()))
        throw std::runtime_error("could not load all eight DCS ROMs from the selected set");

    const auto chd = find_chd(rom_root, zip, work, game);
    const auto nvram = work / (std::string(game.set) + ".nvram");
    if (!ide_open(chd.c_str(), nvram.c_str(), game.cyls, game.heads,
                  game.secs, game.serial_offset))
        throw std::runtime_error("could not open the Killer Instinct CHD: " + chd.string());
    if (const char* sha1 = ide_chd_sha1(); !sha1 || lowercase(sha1) != lowercase(game.chd_sha1))
        throw std::runtime_error("CHD SHA1 does not match the selected MAME parent-set declaration");

    mamerecomp::ki::seattle::power_on();
    rec_expect_game(game.id);
    rec_init();
    if (interpreter_only) rec_enabled = false;
    sound_set_interpreter_only(interpreter_only);
    const auto summary_path = work / (std::string(game.id) + "-runtime-summary.log");
    const auto fallback_path = work / (std::string(game.id) + "-interpreter-summary.log");
    std::error_code directory_error;
    fs::create_directories(work, directory_error);
    mamerecomp_interpreter_trace_set_summary_log(fallback_path.c_str());
    std::cout << game.title << ": runtime started; R4600 AOT=" << (rec_enabled ? "active" : "interpreter")
              << ", CHD=" << chd << ", NVRAM=" << nvram << '\n' << std::flush;

#ifdef MAMERECOMP_KI_SDL3
    SDL_Window* display = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    if (window) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) throw std::runtime_error(SDL_GetError());
        display = SDL_CreateWindow(game.title, 960, 720, 0);
        if (!display) throw std::runtime_error(SDL_GetError());
        renderer = SDL_CreateRenderer(display, nullptr);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                    SDL_TEXTUREACCESS_STREAMING, 320, 240);
        if (!renderer || !texture) throw std::runtime_error(SDL_GetError());
        SDL_AudioSpec audio_spec{SDL_AUDIO_S16, 1, 31250};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &audio_spec, nullptr, nullptr);
        if (audio) SDL_ResumeAudioStreamDevice(audio);
        else std::cerr << game.id << " audio output unavailable: " << SDL_GetError() << '\n';
    }
#else
    if (window) throw std::runtime_error("this runner was built without SDL3 window support");
#endif
    if (!window && frame_limit == 0) frame_limit = 1;
    std::vector<std::uint8_t> rgb(320 * 240 * 3);
    std::uint32_t p1 = 0xffffffffu, p2 = 0xffffffffu;
    bool alive = true;
    auto next_frame = std::chrono::steady_clock::now();
    const auto host_frame_period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / host_frame_rate_limit));
    double frame_compute_ms = 0.0;
    std::uint64_t late_frames = 0;
    std::uint64_t audio_submitted_samples = 0;
    std::uint64_t presented_frames = 0;
    std::uint16_t audio_output_peak = 0;
    const auto host_run_begin = std::chrono::steady_clock::now();
    while (alive && (window || mamerecomp::ki::seattle::frame_number() < frame_limit)) {
#ifdef MAMERECOMP_KI_SDL3
        if (window) {
            // Pace at the start of each emulated frame. This makes each
            // run_frame() advance at most once per host-frame interval.
            std::this_thread::sleep_until(next_frame);
            const auto frame_start = std::chrono::steady_clock::now();
            next_frame = frame_start + host_frame_period;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) alive = false;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) alive = false;
            }
            const bool* keys = SDL_GetKeyboardState(nullptr);
            p1 = p2 = 0xffffffffu;
            auto press = [](std::uint32_t& port, std::uint32_t bit, bool down) {
                if (down) port &= ~bit;
            };
            press(p1, 0x0040, keys[SDL_SCANCODE_UP]);
            press(p1, 0x0080, keys[SDL_SCANCODE_DOWN]);
            press(p1, 0x0100, keys[SDL_SCANCODE_LEFT]);
            press(p1, 0x0200, keys[SDL_SCANCODE_RIGHT]);
            press(p1, 0x0001, keys[SDL_SCANCODE_A]);
            press(p1, 0x0002, keys[SDL_SCANCODE_S]);
            press(p1, 0x0004, keys[SDL_SCANCODE_D]);
            press(p1, 0x0008, keys[SDL_SCANCODE_Q]);
            press(p1, 0x0010, keys[SDL_SCANCODE_W]);
            press(p1, 0x0020, keys[SDL_SCANCODE_E]);
            press(p1, 0x0400, keys[SDL_SCANCODE_1]);
            press(p1, 0x0800, keys[SDL_SCANCODE_5]);
            press(p2, 0x0040, keys[SDL_SCANCODE_I]);
            press(p2, 0x0080, keys[SDL_SCANCODE_K]);
            press(p2, 0x0100, keys[SDL_SCANCODE_J]);
            press(p2, 0x0200, keys[SDL_SCANCODE_L]);
            press(p2, 0x0001, keys[SDL_SCANCODE_U]);
            press(p2, 0x0002, keys[SDL_SCANCODE_O]);
            press(p2, 0x0004, keys[SDL_SCANCODE_P]);
            press(p2, 0x0400, keys[SDL_SCANCODE_2]);
            press(p2, 0x0800, keys[SDL_SCANCODE_6]);
        }
#endif
        mamerecomp::ki::seattle::set_inputs(p1, p2);
        const auto frame_begin = std::chrono::steady_clock::now();
        mamerecomp::ki::seattle::run_frame();
        const auto frame_end = std::chrono::steady_clock::now();
        const double compute_ms = std::chrono::duration<double, std::milli>(frame_end - frame_begin).count();
        frame_compute_ms += compute_ms;
        if (compute_ms > 1000.0 / mamerecomp::ki::seattle::frame_rate) ++late_frames;
        std::array<std::int16_t, 4096> audio_samples{};
        const int audio_count = sound_fetch(audio_samples.data(), audio_samples.size());
        for (int sample_index = 0; sample_index < audio_count; ++sample_index) {
            const auto sample = audio_samples[static_cast<std::size_t>(sample_index)];
            const auto magnitude = static_cast<std::uint16_t>(sample < 0
                ? -static_cast<std::int32_t>(sample) : sample);
            audio_output_peak = std::max(audio_output_peak, magnitude);
        }
#ifdef MAMERECOMP_KI_SDL3
        if (window) {
            if (audio && audio_count > 0) {
                SDL_PutAudioStreamData(audio, audio_samples.data(), audio_count * sizeof(std::int16_t));
                audio_submitted_samples += static_cast<std::uint64_t>(audio_count);
            }
            mamerecomp::ki::seattle::copy_rgb(rgb);
            SDL_UpdateTexture(texture, nullptr, rgb.data(), 320 * 3);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
            ++presented_frames;
            const auto now = std::chrono::steady_clock::now();
            // If rendering/emulation missed its deadline, re-anchor to now;
            // never run catch-up frames in a burst.
            if (next_frame < now) next_frame = now;
        }
#endif
    }
#ifdef MAMERECOMP_KI_SDL3
    if (window) {
        if (audio) SDL_DestroyAudioStream(audio);
        SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(display); SDL_Quit();
    }
#endif
    if (!frame_out.empty()) save_ppm(frame_out);
    const auto mips_total = cpu_native_count + cpu_interp_count;
    const double host_run_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - host_run_begin).count();
    const double presented_fps = host_run_seconds > 0.0
        ? static_cast<double>(presented_frames) / host_run_seconds : 0.0;
    {
        std::ofstream summary(summary_path, std::ios::trunc);
        if (summary) {
            summary << game.id << " frames=" << mamerecomp::ki::seattle::frame_number() << '\n'
                    << "MIPS III instructions: native/recompiled=" << cpu_native_count
                    << " interpreted=" << cpu_interp_count << " total=" << mips_total << '\n'
                    << "avg-frame-compute-ms="
                    << (mamerecomp::ki::seattle::frame_number()
                            ? frame_compute_ms / mamerecomp::ki::seattle::frame_number() : 0.0)
                    << " late-frames=" << late_frames
                    << " presented-fps=" << presented_fps << '\n'
                    << "audio-output: submitted-samples=" << audio_submitted_samples
                    << " peak=" << audio_output_peak << " SDL="
#ifdef MAMERECOMP_KI_SDL3
                    << (window ? (audio ? "opened" : "unavailable") : "not-requested")
#else
                    << "not-built"
#endif
                    << '\n';
        }
    }
    sound_report_file(summary_path.c_str());
    std::cout << game.id << " frames=" << mamerecomp::ki::seattle::frame_number()
              << " MIPS III instructions: native/recompiled=" << cpu_native_count
              << " interpreted=" << cpu_interp_count << " total=" << mips_total
              << " avg-frame-compute-ms="
              << (mamerecomp::ki::seattle::frame_number()
                    ? frame_compute_ms / mamerecomp::ki::seattle::frame_number() : 0.0)
              << " late-frames=" << late_frames
              << " presented-fps=" << presented_fps << '\n'
              << game.id << " audio output: submitted-samples=" << audio_submitted_samples
              << " peak=" << audio_output_peak << '\n'
              << game.id << " runtime summary: " << summary_path << '\n'
              << game.id << " categorized fallback summary: " << fallback_path << '\n';
    sound_report();
    ide_flush();
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        fs::path rom_dir = fs::path("/home/luiz/git/mame/roms");
        fs::path work = fs::current_path() / "build/ki/work/ki1";
        std::string game_id = "ki1";
        fs::path frame_out;
        std::uint64_t frames = 0;
        bool window = false;
        bool interpreter_only = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto take = [&](const char* option) -> std::string {
                const std::string prefix = std::string(option) + "=";
                if (arg.starts_with(prefix)) return arg.substr(prefix.size());
                if (arg == option && i + 1 < argc) return argv[++i];
                return {};
            };
            if (auto value = take("--rom-dir"); !value.empty()) rom_dir = value;
            else if (auto value = take("--work-dir"); !value.empty()) work = value;
            else if (auto value = take("--game"); !value.empty()) game_id = value;
            else if (auto value = take("--frames"); !value.empty()) frames = std::stoull(value);
            else if (auto value = take("--frame-out"); !value.empty()) frame_out = value;
            else if (arg == "--window") window = true;
            else if (arg == "--interpreter-only") interpreter_only = true;
            else throw std::runtime_error("unknown KI runner option: " + arg);
        }
        fs::create_directories(work);
        return run(rom_dir, work, frames, window, interpreter_only, frame_out, game_id);
    } catch (const std::exception& error) {
        std::cerr << "mamerecomp_ki: " << error.what() << '\n';
        return 1;
    }
}
