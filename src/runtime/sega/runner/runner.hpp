#pragma once

#include "runtime/sega/game_settings_api.hpp"
#include "runtime/sega/presentation/sega_config.hpp"

#include "boards/sega/common/video_frame.hpp"
#include "runtime/sega/profiles/sega_profiles.hpp"
#include "runtime/sega/board_registry.hpp"
#include "runtime/sega/runner/module_location.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace multiarcaderecomp::sega {

struct InputEvent {
    std::uint64_t first_frame;
    std::uint64_t duration_frames;
    std::uint8_t active_low_mask;
    enum class Port { Service, P1, P2, Coin, System, P1Kick, P2Kick } port;
};

int run_machine_command(const std::filesystem::path& root, const std::string& board,
    const std::string& game, const std::optional<std::filesystem::path>& rom_arg,
    bool build_mode, bool audit_mode, const std::string& jobs, bool force_rebuild,
    bool window, bool headless, std::optional<std::uint64_t> frames,
    std::optional<std::uint64_t> cycles,
    const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::filesystem::path>& video_state_out,
    bool audio_enabled, bool audio_stereo_spread, bool audio_pan_option_set,
    std::uint8_t ym2151_muted_channels, bool ym2151_suppress_slot0_channel2,
    bool ym2151_suppress_slot3_channel5, bool ym2151_suppress_slot4_channel6,
    bool ym2151_suppress_slot5_channel7, bool arranged_bgm,
    const std::optional<std::string>& video_backend_option, bool compare_video, bool video_profile_enabled,
    bool aot_profile_enabled, bool frame_limit_enabled,
    const std::optional<std::filesystem::path>& cpu_trace_out,
    std::uint64_t cpu_trace_start, std::uint64_t cpu_trace_end,
    const std::function<int(const std::vector<std::string>&)>& launch_child);

#ifdef MULTIARCADERECOMP_HAS_GENERATED_RECOMP
int run_game(const multiarcaderecomp::GameProfile& profile, const std::filesystem::path& zip_path,
    std::uint64_t cycle_budget, const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::uint64_t>& frame_out_vblank,
    const std::optional<std::filesystem::path>& video_state_out,
    const std::optional<std::filesystem::path>& runtime_entry_out,
    const std::optional<std::filesystem::path>& nvram_in, bool mame_nvram_in, bool video_profile_enabled,
    bool aot_profile_enabled, bool frame_limit_enabled,
    multiarcaderecomp::SegaVideoBackend video_backend, bool compare_cpu_video,
    const std::vector<InputEvent>& input_events,
    std::optional<std::uint8_t> dsw1_override, std::optional<std::uint8_t> dsw2_override, bool debugger_enabled,
    const std::optional<std::filesystem::path>& cpu_trace_out,
    std::uint64_t cpu_trace_start, std::uint64_t cpu_trace_end, bool window_enabled, bool audio_enabled,
    bool audio_stereo_spread, std::uint8_t audio_volume,
    multiarcaderecomp::SegaDisplayMode display_mode, std::uint8_t window_scale,
    std::uint8_t ym2151_muted_channels, bool ym2151_suppress_slot0_channel2,
    bool ym2151_suppress_slot3_channel5, bool ym2151_suppress_slot4_channel6,
    bool ym2151_suppress_slot5_channel7, bool arranged_bgm,
    const std::vector<multiarcaderecomp::sega::GameInputBinding>& game_input_bindings = {});
#endif

} // namespace multiarcaderecomp::sega
