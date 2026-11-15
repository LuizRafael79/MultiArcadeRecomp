#pragma once

#include <cstdint>
#include <array>

namespace multiarcaderecomp::cps1::detail {

std::uint16_t read_player_inputs();
std::uint8_t read_system_dip(unsigned offset);
std::uint8_t read_extra_buttons();
std::uint8_t read_input2();
std::uint8_t read_input3();
void configure_inputs(std::uint16_t players, const std::array<std::uint8_t, 4>& dips,
    std::uint8_t extra_buttons = 0xff, std::uint8_t input2 = 0xff,
    std::uint8_t input3 = 0xff);
void set_host_inputs(std::uint16_t players_pressed, std::uint8_t system_pressed,
    std::uint8_t extra_pressed, std::uint8_t input2_pressed = 0);
void cps1_coin_counter_write(unsigned slot, bool level);
void cps1_coin_lockout_write(unsigned slot, bool locked);
void pulse_coin(unsigned slot);

} // namespace multiarcaderecomp::cps1::detail
