#include "cps1_inputs.hpp"

namespace mamerecomp::cps1::detail {
namespace {
std::uint16_t player_port = 0xffff;
std::array<std::uint8_t, 4> dip_ports{0xff, 0xff, 0xfc, 0x9c};
std::uint8_t extra_buttons_port = 0xff;
std::uint8_t input2_port = 0xff;
std::uint8_t input3_port = 0xff;
std::uint8_t system_port = 0xff;
std::uint16_t players_pressed = 0;
std::uint8_t extra_pressed = 0;
std::uint64_t input_frame = 0;
std::array<std::uint64_t, 2> coin_pulse_until{};
std::array<bool, 4> coin_counter_level{};
std::array<bool, 4> coin_locked{};
std::array<std::uint64_t, 4> coin_counts{};
}

std::uint16_t read_player_inputs() { return player_port; }
std::uint8_t read_system_dip(unsigned offset) {
    if (offset == 0) return system_port;
    return offset < dip_ports.size() ? dip_ports[offset] : 0xff;
}
std::uint8_t read_extra_buttons() { return extra_buttons_port; }
std::uint8_t read_input2() { return input2_port; }
std::uint8_t read_input3() { return input3_port; }
void configure_inputs(std::uint16_t players, const std::array<std::uint8_t, 4>& dips,
    std::uint8_t extra_buttons, std::uint8_t input2, std::uint8_t input3) {
    player_port = players;
    dip_ports = dips;
    extra_buttons_port = extra_buttons;
    input2_port = input2;
    input3_port = input3;
    system_port = 0xff;
    players_pressed = 0;
    extra_pressed = 0;
    input_frame = 0;
    coin_pulse_until = {};
    coin_counter_level = {};
    coin_locked = {};
    coin_counts = {};
}

void set_host_inputs(std::uint16_t pressed_players, std::uint8_t pressed_system,
    std::uint8_t pressed_extra, std::uint8_t pressed_input2) {
    ++input_frame;
    players_pressed = pressed_players;
    extra_pressed = pressed_extra;
    std::uint8_t system = pressed_system;
    for (unsigned slot = 0; slot < coin_pulse_until.size(); ++slot) {
        if (coin_pulse_until[slot] >= input_frame)
            system |= static_cast<std::uint8_t>(1U << slot);
    }
    player_port = static_cast<std::uint16_t>(~players_pressed);
    extra_buttons_port = static_cast<std::uint8_t>(~extra_pressed);
    input2_port = static_cast<std::uint8_t>(~pressed_input2);
    system_port = static_cast<std::uint8_t>(~system);
}

void pulse_coin(unsigned slot) {
    if (slot < coin_pulse_until.size() && !coin_locked[slot])
        coin_pulse_until[slot] = input_frame + 1;
}

void cps1_coin_counter_write(unsigned slot, bool level) {
    if (slot >= coin_counter_level.size()) return;
    if (level && !coin_counter_level[slot]) ++coin_counts[slot];
    coin_counter_level[slot] = level;
}

void cps1_coin_lockout_write(unsigned slot, bool locked) {
    if (slot < coin_locked.size()) coin_locked[slot] = locked;
}

} // namespace mamerecomp::cps1::detail
