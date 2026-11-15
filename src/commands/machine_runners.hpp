#pragma once

#include "runtime/sega/runner/runner.hpp"
#include "chips/qsound.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mamerecomp::app::machine_runners {

using InputEvent = mamerecomp::sega::InputEvent;

int run_konami_tmnt2_machine(const std::filesystem::path& source_root, const std::string& game,
    const std::optional<std::filesystem::path>& explicit_zip, bool build_mode, bool window,
    std::uint64_t frames, const std::optional<std::uint64_t>& cycles,
    const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::filesystem::path>& frame_sequence_dir, std::uint64_t frame_sequence_step,
    bool audio_enabled, const std::vector<InputEvent>& input_events, const std::string& jobs, bool force_rebuild);

int run_seibu_toki_machine(const std::filesystem::path& source_root, const std::string& game,
    const std::optional<std::filesystem::path>& explicit_zip, bool build_mode, bool window,
    std::uint64_t frames, const std::optional<std::uint64_t>& cycles,
    const std::optional<std::filesystem::path>& frame_out, bool audio_enabled,
    const std::vector<InputEvent>& input_events, const std::string& jobs, bool force_rebuild);

int run_konami_tmnt_machine(const std::filesystem::path& source_root,
    const std::optional<std::filesystem::path>& explicit_zip, bool build_mode, bool window,
    std::uint64_t frames, const std::optional<std::uint64_t>& cycles,
    const std::optional<std::filesystem::path>& frame_out, bool audio_enabled,
    const std::string& jobs, bool force_rebuild);

int run_zn_machine(const std::filesystem::path& source_root, const std::string& game,
    const std::optional<std::filesystem::path>& rom_zip, bool show_window, bool audio_enabled, bool gpu_hw,
    const std::vector<InputEvent>& button_events, std::uint64_t frames,
    const std::optional<std::uint64_t>& cycles, const std::optional<std::filesystem::path>& frame_out,
    const std::optional<std::filesystem::path>& frame_sequence_dir, std::uint64_t frame_sequence_step,
    const char* platform_name, const char* board_name,
    const std::optional<std::filesystem::path>& nvram_option, bool r3000a_no_aot, bool z80_no_aot,
    bool dsp16a_no_aot, mamerecomp::chips::QSoundMode qsound_mode, bool qsound_mixed_requested);

} // namespace mamerecomp::app::machine_runners
