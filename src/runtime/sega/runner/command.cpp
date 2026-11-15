#include "runtime/sega/runner/runner.hpp"
#include "runtime/sega/board_registry.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "runtime/sega/game_settings_api.hpp"
#include "runtime/sega/presentation/sega_config.hpp"
#include "runtime/sega/presentation/sega_presenter.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef MAMERECOMP_SOURCE_DIR
#define MAMERECOMP_SOURCE_DIR "."
#endif
#ifndef MAMERECOMP_BINARY_DIR
#define MAMERECOMP_BINARY_DIR "build"
#endif
#ifndef MAMERECOMP_PYTHON_EXECUTABLE
#define MAMERECOMP_PYTHON_EXECUTABLE "python3"
#endif

namespace mamerecomp::sega {
namespace {

class GameSettingsModule {
public:
    GameSettingsModule(const std::filesystem::path& module_path,
        const std::filesystem::path& settings_path, const std::filesystem::path& legacy_path) {
        m_handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_handle) {
            const auto* error = dlerror();
            throw std::runtime_error("could not load game settings module " + module_path.string()
                + ": " + (error ? error : "unknown dynamic loader error"));
        }
        using ApiFn = const GameSettingsApi* (*)();
        auto api_fn = reinterpret_cast<ApiFn>(dlsym(m_handle, "mamerecomp_game_settings_api"));
        if (!api_fn || !(m_api = api_fn()) || m_api->abi_revision != 1
            || !m_api->create || !m_api->destroy
            || !m_api->draw_game_tab || !m_api->input_bindings || !m_api->dsw1 || !m_api->dsw2) {
            dlclose(m_handle); m_handle = nullptr;
            throw std::runtime_error("game module has no complete settings API: " + module_path.string());
        }
        m_state = m_api->create(settings_path.c_str(), legacy_path.c_str());
        if (!m_state) {
            dlclose(m_handle); m_handle = nullptr;
            throw std::runtime_error("game settings module failed to create settings: " + module_path.string());
        }
    }
    ~GameSettingsModule() {
        if (m_state && m_api) m_api->destroy(m_state);
        if (m_handle) dlclose(m_handle);
    }
    GameSettingsModule(const GameSettingsModule&) = delete;
    GameSettingsModule& operator=(const GameSettingsModule&) = delete;
    const GameSettingsApi* api() const { return m_api; }
    void* state() const { return m_state; }
    std::uint8_t dsw1() const { return m_api->dsw1(m_state); }
    std::uint8_t dsw2() const { return m_api->dsw2(m_state); }
    std::vector<GameInputBinding> input_bindings() const {
        std::size_t count = 0;
        const auto* bindings = m_api->input_bindings(&count);
        return bindings ? std::vector<GameInputBinding>(bindings, bindings + count) : std::vector<GameInputBinding>{};
    }
private:
    void* m_handle = nullptr;
    const GameSettingsApi* m_api = nullptr;
    void* m_state = nullptr;
};

} // namespace

int run_machine_command(const std::filesystem::path& root, const std::string& board,
                         const std::string& game,
                         const std::optional<std::filesystem::path>& rom_arg,
                         bool build_mode, bool audit_mode, const std::string& jobs, bool force_rebuild,
                         bool window, bool headless, std::optional<std::uint64_t> frames,
                         std::optional<std::uint64_t> cycles,
                         const std::optional<std::filesystem::path>& frame_out,
                         const std::optional<std::filesystem::path>& video_state_out,
                         bool audio_enabled, bool audio_stereo_spread,
                         bool audio_pan_option_set,
                         std::uint8_t ym2151_muted_channels,
                         bool ym2151_suppress_slot0_channel2,
                         bool ym2151_suppress_slot3_channel5,
                         bool ym2151_suppress_slot4_channel6,
                         bool ym2151_suppress_slot5_channel7, bool arranged_bgm,
                         const std::optional<std::string>& video_backend_option, bool compare_video,
                         bool video_profile_enabled,
                         bool aot_profile_enabled,
                         bool frame_limit_enabled,
                         const std::optional<std::filesystem::path>& cpu_trace_out,
                         std::uint64_t cpu_trace_start, std::uint64_t cpu_trace_end,
    const std::function<int(const std::vector<std::string>&)>& launch_child) {
    const auto* profile = mamerecomp::find_game_profile(game);
    if (!profile) throw std::runtime_error("unknown Sega game profile: " + game);
    const auto* registration = find_board(mamerecomp::board_family(*profile));
    if (!registration) throw std::runtime_error("Sega profile board family is not registered");
    const std::string expected_board(registration->id);
    if (board != expected_board) {
        throw std::runtime_error(game + " belongs to --board=" + expected_board +
                                 ", not --board=" + board);
    }

    const auto rom_path = rom_arg ? std::filesystem::absolute(*rom_arg)
                                  : root.parent_path() / "mame/roms" / (game + ".zip");
#ifdef MAMERECOMP_BINARY_DIR
    const auto module_root = std::filesystem::path(MAMERECOMP_BINARY_DIR);
#else
    const auto module_root = root / "build";
#endif
    const auto generated_root = module_root / "generated/sega";
    const auto game_output = generated_root / board / game;
    const std::string target = "mamerecomp_" + board + "_" + game;
    const std::string runtime_target = "mamerecomp_sega_runtime_" + board;
    const std::string cache_var = board == "system16a" ? "MAMERECOMP_SYSTEM16A_GENERATED_DIRS"
        : board == "system16b" ? "MAMERECOMP_SYSTEM16B_GENERATED_DIRS"
                                : "MAMERECOMP_SYSTEM18_GENERATED_DIRS";

    if (build_mode || audit_mode) {
        if (window || headless || frames || cycles || frame_out || video_state_out || audio_enabled
            || audio_pan_option_set || video_backend_option || compare_video || video_profile_enabled || !frame_limit_enabled
            || aot_profile_enabled)
            throw std::runtime_error("run-only options cannot be used with the Sega audit/build command");
        if (!std::filesystem::is_regular_file(rom_path))
            throw std::runtime_error("ROM ZIP not found: " + rom_path.string());

        std::vector<std::string> audit_errors;
        const auto audit_region = [&](std::string_view region, auto&& load) {
            try {
                load();
            } catch (const std::exception& error) {
                audit_errors.emplace_back(std::string(region) + ": " + error.what());
            }
        };
        audit_region("maincpu", [&] { (void)mamerecomp::load_maincpu_roms(*profile, rom_path); });
        audit_region("graphics", [&] { (void)mamerecomp::load_graphics_roms(*profile, rom_path); });
        audit_region("sound/sample ROMs", [&] { mamerecomp::validate_profile_sound_roms(*profile, rom_path); });
        if (profile->mcu_region_size != 0)
            audit_region("mcu", [&] { (void)mamerecomp::load_mcu_roms(*profile, rom_path); });
        if (profile->fd1094_key_region_size != 0)
            audit_region("maincpu:key", [&] { (void)mamerecomp::load_fd1094_key_roms(*profile, rom_path); });
        if (profile->pld_region_size != 0)
            audit_region("plds", [&] { (void)mamerecomp::load_pld_roms(*profile, rom_path); });
        if (!audit_errors.empty()) {
            std::ostringstream message;
            message << "ROM audit failed for '" << game << "' (" << rom_path << ")";
            for (const auto& error : audit_errors) message << "\n  - " << error;
            throw std::runtime_error(message.str());
        }
        std::cout << "ROM audit passed for '" << game
                  << "': required runtime ROMs match size, CRC32 and SHA-1.\n";
        if (audit_mode) return 0;

        // Build only the selected board runtime first. FD1094 translation and
        // other board services are provided by this dynamically loaded module.
        const int runtime_configure_status = launch_child({"cmake", "-S", root.string(), "-B", module_root.string(),
            "-DMAMERECOMP_GENERATED_DIR=", "-D" + cache_var + "="});
        if (runtime_configure_status != 0) return runtime_configure_status;
        std::cout << "Building the " << board << " runtime module.\n" << std::flush;
        const int runtime_status = launch_child({"cmake", "--build", module_root.string(), "--target",
            runtime_target, "-j", jobs});
        if (runtime_status != 0) return runtime_status;

        std::filesystem::create_directories(game_output);
        std::cout << (force_rebuild ? "Forcing Sega translation rebuild.\n"
                                    : "Generating Sega translation.\n") << std::flush;
        std::error_code ec;
        const auto executable = std::filesystem::canonical("/proc/self/exe", ec);
        if (ec) throw std::runtime_error("could not resolve the running mamerecomp executable: " + ec.message());
        std::vector<std::string> translate_command{executable.string(), "translate-full", game,
            rom_path.string(), "--out", game_output.string(), "--files", "16"};
        const auto runtime_entries = game_output / "m68k_runtime_entries.txt";
        if (std::filesystem::is_regular_file(runtime_entries)) {
            translate_command.emplace_back("--entries-file");
            translate_command.push_back(runtime_entries.string());
        }
        const int translate_status = launch_child(translate_command);
        if (translate_status != 0) return translate_status;

        const auto z80_generated = game_output / "z80";
        const auto z80_args = std::vector<std::string>{MAMERECOMP_PYTHON_EXECUTABLE,
            (root / "tools/z80/recomp.py").string(), "--program",
            (game_output / "z80_input/sound.bin").string(), "--entries",
            (game_output / "z80_input/entries.txt").string(), "--out", z80_generated.string(),
            "--precise-cycle-yield"};
        const int z80_status = launch_child(z80_args);
        if (z80_status != 0) return z80_status;

        const int game_configure_status = launch_child({"cmake", "-S", root.string(), "-B", module_root.string(),
            "-DMAMERECOMP_GENERATED_DIR=", "-D" + cache_var + "=" + game_output.string()});
        if (game_configure_status != 0) return game_configure_status;
        std::cout << "Building the " << game << " AOT module for Sega " << board << ".\n" << std::flush;
        return launch_child({"cmake", "--build", module_root.string(), "--target", target,
            "-j", jobs});
    }

    const auto runner = module_root / "mamerecomp";
    const auto runtime_module = module_root / ("mamerecomp_" + board + "_runtime.so");
    const auto module = module_root / ("mamerecomp_" + board + "_" + game + ".so");
    if (!std::filesystem::is_regular_file(runner) || !std::filesystem::is_regular_file(runtime_module) ||
        !std::filesystem::is_regular_file(module) ||
        !std::filesystem::is_regular_file(game_output / (game + "_recomp_table.c"))) {
        throw std::runtime_error("Sega game build is missing; run `mamerecomp build --machine=sega --board=" +
            board + " --game=" + game + "` first");
    }
    std::cout << "Launching " << game << " on Sega " << board << ".\n" << std::flush;
#ifdef MAMERECOMP_HAS_GENERATED_RECOMP
    std::uint64_t cycle_budget = cycles.value_or(1000000);
    const bool show_window = !headless && (window || (!frames && !cycles));
    if (!cycles && frames) {
        const double fps = static_cast<double>(registration->refresh_rate_num) / registration->refresh_rate_den;
        cycle_budget = static_cast<std::uint64_t>(static_cast<double>(*frames) * registration->maincpu_clock_hz / fps);
    } else if (!cycles && show_window) {
        cycle_budget = static_cast<std::uint64_t>(INT64_MAX);
    }
    std::filesystem::path settings_path;
    mamerecomp::SegaVideoBackend video_backend = mamerecomp::SegaVideoBackend::Software;
    if (video_backend_option) {
        if (*video_backend_option == "software") video_backend = mamerecomp::SegaVideoBackend::Software;
        else if (*video_backend_option == "opengl") video_backend = mamerecomp::SegaVideoBackend::OpenGL;
        else throw std::runtime_error("--video-backend must be software or opengl");
    }
    if (video_backend == mamerecomp::SegaVideoBackend::OpenGL && !registration->supports_opengl_video)
        throw std::runtime_error("OpenGL tilemap video is not available for board " + board);
    if (compare_video && video_backend != mamerecomp::SegaVideoBackend::OpenGL)
        throw std::runtime_error("--compare-video requires --video-backend=opengl");
    mamerecomp::SegaDisplayMode display_mode = mamerecomp::SegaDisplayMode::Windowed;
    std::uint8_t window_scale = 3;
    std::uint8_t audio_volume = 100;
    std::optional<std::uint8_t> dsw1_override;
    std::optional<std::uint8_t> dsw2_override;
    std::unique_ptr<GameSettingsModule> game_settings;
    std::vector<GameInputBinding> game_input_bindings;
    if (show_window && profile->has_interactive_configuration) {
        auto game_settings_path = mamerecomp::sega_settings_path(profile->id.data());
        game_settings_path.replace_extension(".game.ini");
        const auto legacy_settings_path = mamerecomp::sega_settings_path(profile->id.data());
        game_settings = std::make_unique<GameSettingsModule>(module, game_settings_path, legacy_settings_path);
        game_input_bindings = game_settings->input_bindings();
    }
    if (show_window && !cycles) {
        mamerecomp::SegaSharedSettings settings;
        settings.audio_enabled = true;
        settings.audio_stereo_spread = audio_stereo_spread;
        settings_path = mamerecomp::sega_settings_path(profile->id.data());
        bool shared_settings_found = std::filesystem::exists(settings_path);
        settings = mamerecomp::load_sega_settings(settings_path, settings);
        if (!shared_settings_found) {
            const mamerecomp::GameProfile* parent = profile;
            while (!parent->parent_id.empty()) {
                parent = mamerecomp::find_game_profile(parent->parent_id);
                if (!parent) break;
                const auto parent_settings_path = mamerecomp::sega_settings_path(parent->id.data());
                if (!std::filesystem::exists(parent_settings_path)) continue;
                settings = mamerecomp::load_sega_settings(parent_settings_path, settings);
                shared_settings_found = true;
                std::cout << "Using shared Sega settings from parent profile " << parent->id << '\n';
                break;
            }
        }
        if (!shared_settings_found && !profile->has_interactive_configuration)
            settings.audio_enabled = audio_enabled;
        if (audio_enabled) settings.audio_enabled = true;
        if (audio_pan_option_set) settings.audio_stereo_spread = audio_stereo_spread;
        if (profile->has_interactive_configuration) {
            mamerecomp::SegaPresenter configuration_presenter(std::string(profile->title) + " - MameRecomp",
                {}, true, false, registration->refresh_rate_num, registration->refresh_rate_den);
            if (!configuration_presenter.show_configuration_menu(settings, settings_path,
                    std::string(profile->title), game_settings ? game_settings->api() : nullptr,
                    game_settings ? game_settings->state() : nullptr)) return 0;
        }
        audio_enabled = settings.audio_enabled;
        audio_stereo_spread = settings.audio_stereo_spread;
        audio_volume = settings.audio_volume;
        display_mode = settings.display_mode;
        window_scale = settings.window_scale;
        video_backend = video_backend_option
            ? (*video_backend_option == "opengl" ? mamerecomp::SegaVideoBackend::OpenGL
                                                  : mamerecomp::SegaVideoBackend::Software)
            : settings.video_backend;
        if (game_settings) {
            dsw1_override = game_settings->dsw1();
            dsw2_override = game_settings->dsw2();
        }
    }
    const auto runtime_entries = game_output / "m68k_runtime_entries.txt";
    return mamerecomp::sega::run_game(*profile, rom_path, cycle_budget, frame_out, std::nullopt, video_state_out,
        runtime_entries,
        std::nullopt, false, video_profile_enabled, aot_profile_enabled, frame_limit_enabled, video_backend, compare_video,
        {}, dsw1_override, dsw2_override, false, cpu_trace_out, cpu_trace_start, cpu_trace_end,
        show_window, audio_enabled, audio_stereo_spread, audio_volume, display_mode, window_scale,
        ym2151_muted_channels, ym2151_suppress_slot0_channel2, ym2151_suppress_slot3_channel5,
        ym2151_suppress_slot4_channel6, ym2151_suppress_slot5_channel7,
        arranged_bgm,
        game_input_bindings);
#else
    throw std::runtime_error("this mamerecomp executable was built without a System 16/18 AOT module; run the build command first");
#endif
}

} // namespace mamerecomp::sega
