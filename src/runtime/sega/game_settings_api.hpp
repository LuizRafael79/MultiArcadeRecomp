#pragma once

#include <cstddef>
#include <cstdint>

namespace mamerecomp::sega {

enum class GameInputPort : std::uint8_t { Service = 0, Player1 = 1, Player2 = 2 };

struct GameInputBinding {
    GameInputPort port;
    std::uint8_t bit;
    const char* key_name;
    const char* control_name;
};

// Stable, C-compatible hooks implemented by an optional per-game module.
// The host owns the ImGui context and calls draw_game_tab inside its menu.
struct GameSettingsApi {
    std::uint32_t abi_revision;
    void* (*create)(const char* settings_path, const char* legacy_path);
    void (*destroy)(void* state);
    bool (*draw_game_tab)(void* state);
    const GameInputBinding* (*input_bindings)(std::size_t* count);
    std::uint8_t (*dsw1)(const void* state);
    std::uint8_t (*dsw2)(const void* state);
};

} // namespace mamerecomp::sega
