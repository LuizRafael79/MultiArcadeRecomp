#pragma once

#include "system16_video.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace mamerecomp {

struct System16GameSettings {
    System16AVideoBackend video_backend = System16AVideoBackend::Software;
    bool fullscreen = false;
    std::uint8_t window_scale = 3;
    bool audio_enabled = true;
    bool audio_stereo_spread = false;
    std::uint8_t audio_volume = 100;

    bool free_play = false;
    std::uint8_t coin_a = 0x0f;
    std::uint8_t coin_b = 0x0f;
    std::uint8_t lives = 3;
    std::uint8_t difficulty = 0x30;
    bool slow_enemy_bullets = true;
    bool english = true;
    bool cocktail = false;
    bool demo_sounds = true;

    std::uint8_t dsw1() const;
    std::uint8_t dsw2() const;
};

std::filesystem::path system16_settings_path(const char* profile_id);
System16GameSettings load_system16_settings(const std::filesystem::path& path, System16GameSettings defaults = {});
bool save_system16_settings(const std::filesystem::path& path, const System16GameSettings& settings, std::string* error = nullptr);

} // namespace mamerecomp
