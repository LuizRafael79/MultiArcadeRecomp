#include "runtime/sega/board_runtime.hpp"
#include "runtime/sega/runner/module_location.hpp"

#include <dlfcn.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace multiarcaderecomp::sega {
namespace {
using RuntimeAbiFn = unsigned (*)();
using RuntimeRevisionFn = unsigned (*)();
using CreateRuntimeFn = BoardRuntimeHardware* (*)(const BoardRuntimeContext*);
using DestroyRuntimeFn = void (*)(BoardRuntimeHardware*);
using MemoryRegionVisitor = void (*)(void*, const char*, std::uint32_t, std::uint32_t,
    std::uint32_t, bool, bool);
using MemoryMapFn = bool (*)(const GameProfile*, const std::uint8_t*, std::size_t,
    MemoryRegionVisitor, void*);
using DecodeTilesFn = std::vector<std::uint8_t> (*)(const std::uint8_t*, std::size_t);
using DecryptInitialProgramFn = std::vector<std::uint8_t> (*)(const GameProfile*, const char*,
    const std::uint8_t*, std::size_t);

void close_module(void* handle) {
    if (handle) dlclose(handle);
}

void* required_symbol(void* handle, const char* name, const std::filesystem::path& module_path) {
    dlerror();
    auto* symbol = dlsym(handle, name);
    if (const auto* error = dlerror())
        throw std::runtime_error("board runtime " + module_path.string() + " is missing " + name + ": " + error);
    return symbol;
}
}

BoardRuntimeHardware make_board_runtime(const BoardRuntimeContext& context) {
    const auto* registration = find_board(board_family(context.profile));
    if (!registration) throw std::runtime_error("Sega board has no registered runtime");

    const auto module_path = module_directory()
        / ("multiarcaderecomp_" + std::string(registration->module_id) + "_runtime.so");
    dlerror();
    void* raw_handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!raw_handle) {
        const auto* error = dlerror();
        throw std::runtime_error("could not load board runtime " + module_path.string()
            + ": " + (error ? error : "unknown dynamic loader error")
            + "; build the selected board runtime first");
    }
    std::shared_ptr<void> module_owner(raw_handle, close_module);

    const auto abi = reinterpret_cast<RuntimeAbiFn>(required_symbol(
        raw_handle, "multiarcaderecomp_runtime_abi", module_path));
    const auto revision = reinterpret_cast<RuntimeRevisionFn>(required_symbol(
        raw_handle, "multiarcaderecomp_runtime_revision", module_path));
    const auto create = reinterpret_cast<CreateRuntimeFn>(required_symbol(
        raw_handle, "multiarcaderecomp_create_runtime", module_path));
    const auto destroy = reinterpret_cast<DestroyRuntimeFn>(required_symbol(
        raw_handle, "multiarcaderecomp_destroy_runtime", module_path));
    if (abi() != 1 || revision() != registration->module_revision)
        throw std::runtime_error("board runtime ABI/revision mismatch: " + module_path.string());

    std::unique_ptr<BoardRuntimeHardware, DestroyRuntimeFn> runtime(create(&context), destroy);
    if (!runtime) throw std::runtime_error("board runtime failed to create a session: " + module_path.string());
    BoardRuntimeHardware hardware = std::move(*runtime);
    runtime.reset();
    hardware.module_owner = std::move(module_owner);
    return hardware;
}

std::vector<BoardMemoryRegion> inspect_board_memory_map(const GameProfile& profile,
    std::span<const std::uint8_t> maincpu_rom) {
    const auto* registration = find_board(board_family(profile));
    if (!registration) throw std::runtime_error("Sega board has no registered runtime");
    const auto module_path = module_directory()
        / ("multiarcaderecomp_" + std::string(registration->module_id) + "_runtime.so");
    void* handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const auto* error = dlerror();
        throw std::runtime_error("could not load board runtime " + module_path.string()
            + ": " + (error ? error : "unknown dynamic loader error"));
    }
    std::shared_ptr<void> owner(handle, close_module);
    const auto visit = reinterpret_cast<MemoryMapFn>(required_symbol(
        handle, "multiarcaderecomp_runtime_memory_map", module_path));
    std::vector<BoardMemoryRegion> regions;
    const auto append = [](void* user, const char* name, std::uint32_t start,
        std::uint32_t size, std::uint32_t mirror, bool writable, bool device) {
        auto& out = *static_cast<std::vector<BoardMemoryRegion>*>(user);
        out.push_back({name, start, size, mirror, writable, device});
    };
    if (!visit(&profile, maincpu_rom.data(), maincpu_rom.size(), append, &regions))
        throw std::runtime_error("board runtime rejected memory-map request: " + module_path.string());
    return regions;
}

std::vector<std::uint8_t> decode_board_tiles(const GameProfile& profile,
    std::span<const std::uint8_t> tile_rom) {
    const auto* registration = find_board(board_family(profile));
    if (!registration) throw std::runtime_error("Sega board has no registered runtime");
    const auto module_path = module_directory()
        / ("multiarcaderecomp_" + std::string(registration->module_id) + "_runtime.so");
    void* handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const auto* error = dlerror();
        throw std::runtime_error("could not load board runtime " + module_path.string()
            + ": " + (error ? error : "unknown dynamic loader error"));
    }
    std::shared_ptr<void> owner(handle, close_module);
    const auto decode = reinterpret_cast<DecodeTilesFn>(required_symbol(
        handle, "multiarcaderecomp_runtime_decode_tiles", module_path));
    return decode(tile_rom.data(), tile_rom.size());
}

std::vector<std::uint8_t> decrypt_board_initial_program(const GameProfile& profile,
    const std::filesystem::path& rom_path, std::span<const std::uint8_t> encrypted_program) {
    const auto* registration = find_board(board_family(profile));
    if (!registration) throw std::runtime_error("Sega board has no registered runtime");
    const auto module_path = module_directory()
        / ("multiarcaderecomp_" + std::string(registration->module_id) + "_runtime.so");
    void* handle = dlopen(module_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const auto* error = dlerror();
        throw std::runtime_error("could not load board runtime " + module_path.string()
            + ": " + (error ? error : "unknown dynamic loader error"));
    }
    std::shared_ptr<void> owner(handle, close_module);
    const auto decrypt = reinterpret_cast<DecryptInitialProgramFn>(required_symbol(
        handle, "multiarcaderecomp_runtime_decrypt_initial_program", module_path));
    return decrypt(&profile, rom_path.c_str(), encrypted_program.data(), encrypted_program.size());
}

} // namespace multiarcaderecomp::sega
