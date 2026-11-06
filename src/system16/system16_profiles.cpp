#include "system16.hpp"
#include "romsets/profiles.hpp"
#include <array>
#include <algorithm>
namespace mamerecomp {
namespace {
const std::array<GameProfile, 7> profiles{{
    romsets::shinobi(), romsets::shinobi5(), romsets::goldnaxe(), romsets::goldnaxeud(), romsets::shdancer(),
    romsets::mwalkd(), romsets::eswat()
}};
}
std::span<const GameProfile> game_profiles() { return profiles; }
const GameProfile* find_game_profile(std::string_view id) {
    const auto found = std::find_if(profiles.begin(), profiles.end(), [id](const GameProfile& profile) {
        return profile.id == id;
    });
    return found == profiles.end() ? nullptr : &*found;
}
}
