#include "system16_config.hpp"

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

struct CoinageValue { std::uint8_t raw; const char* id; };
constexpr CoinageValue coinage_values[] = {
    {0x07, "4c1c"}, {0x08, "3c1c"}, {0x09, "2c1c"}, {0x05, "2c1c_5_3_6_4"},
    {0x04, "2c1c_4_3"}, {0x0f, "1c1c"}, {0x03, "1c1c_5_6"}, {0x02, "1c1c_4_5"},
    {0x01, "1c1c_2_3"}, {0x06, "2c3c"}, {0x0e, "1c2c"}, {0x0d, "1c3c"},
    {0x0c, "1c4c"}, {0x0b, "1c5c"}, {0x0a, "1c6c"},
};

std::uint8_t parse_coinage(const std::string& id, std::uint8_t fallback) {
    for (const auto& option : coinage_values) if (id == option.id) return option.raw;
    return fallback;
}

const char* coinage_id(std::uint8_t value) {
    for (const auto& option : coinage_values) if (value == option.raw) return option.id;
    return "1c1c";
}

const char* difficulty_id(std::uint8_t value) {
    switch (value & 0x30U) {
    case 0x20: return "easy";
    case 0x30: return "normal";
    case 0x10: return "hard";
    default: return "very_hard";
    }
}

std::uint8_t parse_difficulty(const std::string& id, std::uint8_t fallback) {
    if (id == "easy") return 0x20;
    if (id == "normal") return 0x30;
    if (id == "hard") return 0x10;
    if (id == "very_hard") return 0x00;
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

std::uint8_t System16GameSettings::dsw1() const {
    if (free_play) return 0x00;
    return static_cast<std::uint8_t>((coin_a & 0x0fU) | ((coin_b & 0x0fU) << 4));
}

std::uint8_t System16GameSettings::dsw2() const {
    std::uint8_t value = cocktail ? 0x01 : 0x00;
    value |= demo_sounds ? 0x00 : 0x02;
    value |= lives == 2 ? 0x08 : lives == 5 ? 0x04 : 0x0c;
    value |= difficulty & 0x30U;
    value |= slow_enemy_bullets ? 0x40 : 0x00;
    value |= english ? 0x00 : 0x80;
    return value;
}

std::filesystem::path system16_settings_path(const char* profile_id) {
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

System16GameSettings load_system16_settings(const std::filesystem::path& path, System16GameSettings settings) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "video_backend") {
            if (value == "opengl") settings.video_backend = System16AVideoBackend::OpenGL;
            else if (value == "software") settings.video_backend = System16AVideoBackend::Software;
        } else if (key == "fullscreen") settings.fullscreen = parse_bool(value, settings.fullscreen);
        else if (key == "window_scale") {
            const auto scale = parse_byte(value, settings.window_scale);
            if (scale >= 2 && scale <= 5) settings.window_scale = scale;
        } else if (key == "audio_enabled") settings.audio_enabled = parse_bool(value, settings.audio_enabled);
        else if (key == "audio_pan") settings.audio_stereo_spread = value == "spread" ? true : value == "game" ? false : settings.audio_stereo_spread;
        else if (key == "audio_volume") settings.audio_volume = std::min<std::uint8_t>(parse_byte(value, settings.audio_volume), 100);
        else if (key == "free_play") settings.free_play = parse_bool(value, settings.free_play);
        else if (key == "coin_a") settings.coin_a = parse_coinage(value, settings.coin_a);
        else if (key == "coin_b") settings.coin_b = parse_coinage(value, settings.coin_b);
        else if (key == "lives") {
            const auto lives = parse_byte(value, settings.lives);
            if (lives == 2 || lives == 3 || lives == 5) settings.lives = lives;
        } else if (key == "difficulty") settings.difficulty = parse_difficulty(value, settings.difficulty);
        else if (key == "slow_enemy_bullets") settings.slow_enemy_bullets = parse_bool(value, settings.slow_enemy_bullets);
        else if (key == "english") settings.english = parse_bool(value, settings.english);
        else if (key == "cocktail") settings.cocktail = parse_bool(value, settings.cocktail);
        else if (key == "demo_sounds") settings.demo_sounds = parse_bool(value, settings.demo_sounds);
    }
    return settings;
}

bool save_system16_settings(const std::filesystem::path& path, const System16GameSettings& settings, std::string* error) {
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
           << "video_backend=" << (settings.video_backend == System16AVideoBackend::OpenGL ? "opengl" : "software") << '\n'
           << "fullscreen=" << (settings.fullscreen ? 1 : 0) << '\n'
           << "window_scale=" << static_cast<unsigned>(settings.window_scale) << '\n'
           << "audio_enabled=" << (settings.audio_enabled ? 1 : 0) << '\n'
           << "audio_pan=" << (settings.audio_stereo_spread ? "spread" : "game") << '\n'
           << "audio_volume=" << static_cast<unsigned>(settings.audio_volume) << '\n'
           << "free_play=" << (settings.free_play ? 1 : 0) << '\n'
           << "coin_a=" << coinage_id(settings.coin_a & 0x0fU) << '\n'
           << "coin_b=" << coinage_id(settings.coin_b & 0x0fU) << '\n'
           << "lives=" << static_cast<unsigned>(settings.lives) << '\n'
           << "difficulty=" << difficulty_id(settings.difficulty) << '\n'
           << "slow_enemy_bullets=" << (settings.slow_enemy_bullets ? 1 : 0) << '\n'
           << "english=" << (settings.english ? 1 : 0) << '\n'
           << "cocktail=" << (settings.cocktail ? 1 : 0) << '\n'
           << "demo_sounds=" << (settings.demo_sounds ? 1 : 0) << '\n';
    if (!output) {
        if (error) *error = "failed while writing settings file: " + path.string();
        return false;
    }
    return true;
}

} // namespace mamerecomp
