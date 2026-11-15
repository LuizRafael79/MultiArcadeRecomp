#include "runtime/sega/runner/generated_board_module.hpp"

#include <stdexcept>
#include <string>

#include <dlfcn.h>

namespace multiarcaderecomp::sega {
namespace {
void* load_module(const std::filesystem::path& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void unload_module(void* handle) {
    if (!handle) return;
    dlclose(handle);
}

void* find_symbol(void* handle, const char* name) {
    return dlsym(handle, name);
}

std::string module_load_error() {
    const auto* error = dlerror();
    return error ? error : "unknown dynamic loader error";
}
}

GeneratedBoardModule::GeneratedBoardModule(const BoardRegistration& registration, const char* game_id) {
    const auto module_name = std::string("multiarcaderecomp_") + std::string(registration.module_id)
        + "_" + game_id + ".so";
    const auto path = multiarcaderecomp::sega::module_directory() / module_name;
    m_handle = load_module(path);
    if (!m_handle) throw std::runtime_error("could not load board module " + path.string() + ": " + module_load_error());

    const auto abi = reinterpret_cast<unsigned (*)()>(find_symbol(m_handle, "multiarcaderecomp_board_abi"));
    const auto module_revision = reinterpret_cast<int (*)()>(find_symbol(m_handle, "multiarcaderecomp_board_revision"));
    m_generated_table = reinterpret_cast<GeneratedTableFn>(find_symbol(m_handle, "multiarcaderecomp_generated_table"));
    m_z80_code = reinterpret_cast<const z80::Z80Code* (*)()>(
        find_symbol(m_handle, "multiarcaderecomp_sega_z80_code"));
    using GameAudioFn = const GameAudioApi* (*)();
    if (const auto api_fn = reinterpret_cast<GameAudioFn>(find_symbol(m_handle, "multiarcaderecomp_game_audio_api")))
        m_game_audio_api = api_fn();
    if (!abi || !module_revision || !m_generated_table) {
        const auto error = module_load_error();
        unload_module(m_handle);
        m_handle = nullptr;
        throw std::runtime_error("board module has an incomplete ABI: " + path.string()
            + ": " + error);
    }
    if (abi() != 1 || module_revision() != static_cast<int>(registration.module_revision)) {
        unload_module(m_handle);
        m_handle = nullptr;
        throw std::runtime_error("board module ABI/revision mismatch: " + path.string());
    }
    if (m_game_audio_api && (m_game_audio_api->abi_revision != 1 || !m_game_audio_api->on_sound_command)) {
        unload_module(m_handle);
        m_handle = nullptr;
        m_game_audio_api = nullptr;
        throw std::runtime_error("game audio module has an incompatible API revision: " + path.string());
    }
}

GeneratedBoardModule::~GeneratedBoardModule() {
    unload_module(m_handle);
}

const ng_func_entry_t* GeneratedBoardModule::generated_table(const char* profile_id,
    std::size_t* count) const {
    return m_generated_table(profile_id, count);
}

const z80::Z80Code* GeneratedBoardModule::z80_code() const {
    return m_z80_code ? m_z80_code() : nullptr;
}

bool GeneratedBoardModule::on_sound_command(std::uint8_t command, const GameAudioHostApi* host) const {
    return m_game_audio_api && m_game_audio_api->on_sound_command(command, host);
}

} // namespace multiarcaderecomp::sega
