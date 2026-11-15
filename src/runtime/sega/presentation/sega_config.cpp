#include "sega_config.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <string>

namespace mamerecomp {
namespace {

bool parse_bool(const std::string& value, bool fallback) {
    if (value == "1" || value == "true" || value == "on") return true;
    if (value == "0" || value == "false" || value == "off") return false;
    return fallback;
}

std::uint8_t parse_byte(const std::string& value, std::uint8_t fallback) {
    unsigned parsed_value = 0;
    const int base = value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X') ? 16 : 10;
    const char* begin = value.data() + (base == 16 ? 2 : 0);
    const char* end = value.data() + value.size();
    const auto parsed = std::from_chars(begin, end, parsed_value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != end || parsed_value > 0xffU) return fallback;
    return static_cast<std::uint8_t>(parsed_value);
}

} // namespace

std::filesystem::path sega_settings_path(const char* profile_id) {
    std::filesystem::path root;
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) root = appdata;
    else root = std::filesystem::current_path();
    return root / "MameRecomp" / (std::string(profile_id) + ".ini");
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) root = std::filesystem::path(home) / "Library" / "Application Support";
    else root = std::filesystem::current_path();
    return root / "MameRecomp" / (std::string(profile_id) + ".ini");
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) root = xdg;
    else if (const char* home = std::getenv("HOME")) root = std::filesystem::path(home) / ".config";
    else root = std::filesystem::current_path();
    return root / "mamerecomp" / (std::string(profile_id) + ".ini");
#endif
}

SegaSharedSettings load_sega_settings(const std::filesystem::path& path, SegaSharedSettings settings) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "video_backend") {
            if (value == "opengl") settings.video_backend = SegaVideoBackend::OpenGL;
            else if (value == "software") settings.video_backend = SegaVideoBackend::Software;
        } else if (key == "display_mode") {
            if (value == "windowed") settings.display_mode = SegaDisplayMode::Windowed;
            else if (value == "exclusive") settings.display_mode = SegaDisplayMode::FullscreenExclusive;
            else if (value == "borderless") settings.display_mode = SegaDisplayMode::FullscreenBorderless;
        } else if (key == "fullscreen") {
            settings.display_mode = parse_bool(value, false)
                ? SegaDisplayMode::FullscreenBorderless : SegaDisplayMode::Windowed;
        }
        else if (key == "window_scale") {
            const auto scale = parse_byte(value, settings.window_scale);
            if (scale >= 2 && scale <= 5) settings.window_scale = scale;
        } else if (key == "audio_enabled") settings.audio_enabled = parse_bool(value, settings.audio_enabled);
        else if (key == "audio_pan") settings.audio_stereo_spread = value == "spread" ? true : value == "game" ? false : settings.audio_stereo_spread;
        else if (key == "audio_volume") settings.audio_volume = std::min<std::uint8_t>(parse_byte(value, settings.audio_volume), 100);
    }
    return settings;
}

bool save_sega_settings(const std::filesystem::path& path, const SegaSharedSettings& settings, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "could not create settings directory: " + ec.message();
        return false;
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        if (error) *error = "could not open settings file for writing: " + path.string();
        return false;
    }
    output << "# MameRecomp per-game settings. Values are semantic menu choices.\n"
           << "video_backend=" << (settings.video_backend == SegaVideoBackend::OpenGL ? "opengl" : "software") << '\n'
           << "display_mode=" << (settings.display_mode == SegaDisplayMode::Windowed ? "windowed"
               : settings.display_mode == SegaDisplayMode::FullscreenExclusive ? "exclusive" : "borderless") << '\n'
           << "window_scale=" << static_cast<unsigned>(settings.window_scale) << '\n'
           << "audio_enabled=" << (settings.audio_enabled ? 1 : 0) << '\n'
           << "audio_pan=" << (settings.audio_stereo_spread ? "spread" : "game") << '\n'
           << "audio_volume=" << static_cast<unsigned>(settings.audio_volume) << '\n';
    if (!output) {
        if (error) *error = "failed while writing settings file: " + path.string();
        return false;
    }
    return true;
}

} // namespace mamerecomp
