#pragma once

#include "boards/sega/common/video_frame.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace mamerecomp {

enum class SegaDisplayMode : std::uint8_t {
    Windowed,
    FullscreenExclusive,
    FullscreenBorderless,
};

struct SegaSharedSettings {
    SegaVideoBackend video_backend = SegaVideoBackend::Software;
    SegaDisplayMode display_mode = SegaDisplayMode::Windowed;
    std::uint8_t window_scale = 3;
    bool audio_enabled = true;
    bool audio_stereo_spread = false;
    std::uint8_t audio_volume = 100;

};

std::filesystem::path sega_settings_path(const char* profile_id);
SegaSharedSettings load_sega_settings(const std::filesystem::path& path, SegaSharedSettings defaults = {});
bool save_sega_settings(const std::filesystem::path& path, const SegaSharedSettings& settings, std::string* error = nullptr);

} // namespace mamerecomp
