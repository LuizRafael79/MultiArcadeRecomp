#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace multiarcaderecomp::cps1 {

enum class Cps1SoundBoard : std::uint8_t { Ym2151Oki, QSound };
enum class Cps1AddressMap : std::uint8_t { Main, QSound, Custom };
enum class Cps1RomTransform : std::uint8_t { Identity, WordSwap };

struct Cps1MachineProfile {
    std::string_view id;
    std::uint32_t main_clock_hz;
    std::uint32_t sound_clock_hz;
    std::uint32_t pixel_clock_hz;
    std::uint16_t htotal;
    std::uint16_t vtotal;
    std::uint16_t vblank_start;
    Cps1AddressMap address_map;
    Cps1SoundBoard sound_board;
};

struct Cps1InputProfile {
    std::string_view id;
    std::uint16_t player_port_default;
    std::array<std::uint8_t, 4> dip_port_defaults;
    std::uint8_t extra_buttons_port_default = 0xff;
    std::uint8_t input2_port_default = 0xff;
    std::uint8_t input3_port_default = 0xff;
};

struct Cps1CpsBProfile {
    std::string_view id;
    std::string_view device_type;
    std::string_view graphics_mapper;
    std::uint8_t signature_register;
    std::uint16_t board_signature;
    std::uint8_t multiply_factor1_register;
    std::uint8_t multiply_factor2_register;
    std::uint8_t multiply_result_low_register;
    std::uint8_t multiply_result_high_register;
    std::uint8_t layer_control_register;
    std::array<std::uint8_t, 4> priority_registers;
    std::uint8_t palette_control_register;
    std::array<std::uint16_t, 5> layer_enable_masks;
    std::uint8_t input2_register = 0xff;
    std::uint8_t input3_register = 0xff;
};

struct Cps1RomRegion {
    std::string_view name;
    std::uint32_t size;
    std::uint32_t exported_size;
    std::uint8_t fill;
};

// One record describes one physical ROM load segment. A ROM_CONTINUE is a
// second record with the same file and the next source_offset.
struct Cps1RomLoad {
    std::string_view file;
    std::string_view region;
    std::uint32_t file_size;
    std::uint32_t crc32;
    std::uint32_t source_offset;
    std::uint32_t destination;
    std::uint32_t length;
    std::uint8_t group_bytes;
    std::uint8_t stride;
    Cps1RomTransform transform;
};

struct Cps1RomSet {
    std::string_view game;
    std::string_view parent;
    std::span<const Cps1RomRegion> regions;
    std::span<const Cps1RomLoad> loads;
};

struct Cps1GameDriver {
    std::string_view id;
    std::string_view title;
    std::string_view year;
    std::string_view parent;
    std::string_view machine_config;
    std::string_view input_config;
    std::string_view initialization;
    const Cps1MachineProfile* machine;
    const Cps1InputProfile* inputs;
    const Cps1CpsBProfile* cps_b;
    const Cps1RomSet* romset;
    bool graphics_ram_is_executable;
    bool reverse_sprite_order = false;
};

const Cps1GameDriver& sf2ce_driver();
const Cps1GameDriver& dino_driver();
const Cps1GameDriver* find_game_driver(std::string_view id);

} // namespace multiarcaderecomp::cps1
