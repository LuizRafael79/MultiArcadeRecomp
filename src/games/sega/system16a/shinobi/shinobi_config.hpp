#pragma once

#include "runtime/sega/game_settings_api.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace mamerecomp::games::sega::system16a::shinobi {

struct LivesSetting {
    std::uint8_t dip_bits = 0x0c;
    bool infinite = false;
};

void select_lives(std::string_view profile_id, std::string_view option, LivesSetting& setting);
void select_coin_mode(std::string_view option, bool lives_option_set, LivesSetting& setting, bool& freeplay);
void apply_coin_mode_to_dsw(bool freeplay, std::optional<std::uint8_t>& dsw1_override);
std::uint8_t lives_count(const LivesSetting& setting);
const char* lives_mode_name(std::uint8_t dsw2, bool infinite);

// Exported through the selected game's shared module and consumed by the
// generic Sega presenter; Shinobi DIP meaning stays in this game module.
const mamerecomp::sega::GameSettingsApi* game_settings_api();

} // namespace mamerecomp::games::sega::system16a::shinobi
