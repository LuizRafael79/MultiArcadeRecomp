#pragma once

#include <cstdint>
#include <functional>

namespace mamerecomp::naomi {

// 315-5881 M2 cartridge crypto/decompression stream interface. The board
// supplies ciphered ROM words through the callback; protection registers feed
// the key/address/subkey state separately.
void m2crypt_set_ciphered_reader(std::function<std::uint16_t(std::uint32_t)> reader);
void m2crypt_reset();
void m2crypt_set_key(std::uint32_t key);
void m2crypt_set_low_address(std::uint16_t address);
void m2crypt_set_high_address(std::uint16_t address);
void m2crypt_set_subkey(std::uint16_t subkey);
std::uint16_t m2crypt_read_word();

} // namespace mamerecomp::naomi
