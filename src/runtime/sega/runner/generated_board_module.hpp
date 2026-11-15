#pragma once

#include "runtime/sega/board_registry.hpp"
#include "runtime/sega/runner/module_location.hpp"
#include "runtime/sega/game_audio_api.hpp"
#include "native_z80.h"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <neogeorecomp/neogeo.h>

namespace mamerecomp::sega {

class GeneratedBoardModule {
public:
    GeneratedBoardModule(const BoardRegistration& registration, const char* game_id);
    ~GeneratedBoardModule();
    GeneratedBoardModule(const GeneratedBoardModule&) = delete;
    GeneratedBoardModule& operator=(const GeneratedBoardModule&) = delete;

    const ng_func_entry_t* generated_table(const char* profile_id, std::size_t* count) const;
    const z80::Z80Code* z80_code() const;
    bool has_game_audio_api() const { return m_game_audio_api != nullptr; }
    bool on_sound_command(std::uint8_t command, const GameAudioHostApi* host) const;

private:
    using GeneratedTableFn = const ng_func_entry_t* (*)(const char*, std::size_t*);
    void* m_handle = nullptr;
    GeneratedTableFn m_generated_table = nullptr;
    const z80::Z80Code* (*m_z80_code)() = nullptr;
    const GameAudioApi* m_game_audio_api = nullptr;
};

} // namespace mamerecomp::sega
