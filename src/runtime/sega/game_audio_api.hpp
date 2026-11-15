#pragma once

#include <cstdint>

namespace mamerecomp::sega {

// Game-specific sound command handling is implemented by the selected game
// module. The host only exposes neutral playback operations.
struct GameAudioHostApi {
    void* context;
    bool (*play_track)(void* context, const char* track_id);
    bool (*play_one_shot)(void* context, const char* track_id);
    void (*stop_track)(void* context);
    void (*set_track_gain)(void* context, float gain);
};

struct GameAudioApi {
    std::uint32_t abi_revision;
    // Return true when this game command should be suppressed on native audio.
    bool (*on_sound_command)(std::uint8_t command, const GameAudioHostApi* host);
};

} // namespace mamerecomp::sega
