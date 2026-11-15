#include "sha1.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace mamerecomp::roms {
namespace {

constexpr std::uint32_t rotate_left(std::uint32_t value, unsigned count) noexcept {
    return std::rotl(value, static_cast<int>(count));
}

void process_block(const std::uint8_t* block, std::array<std::uint32_t, 5>& state) noexcept {
    std::array<std::uint32_t, 80> words{};
    for (unsigned i = 0; i < 16; ++i) {
        const auto offset = i * 4;
        words[i] = (std::uint32_t(block[offset]) << 24)
            | (std::uint32_t(block[offset + 1]) << 16)
            | (std::uint32_t(block[offset + 2]) << 8)
            | std::uint32_t(block[offset + 3]);
    }
    for (unsigned i = 16; i < words.size(); ++i)
        words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

    auto a = state[0];
    auto b = state[1];
    auto c = state[2];
    auto d = state[3];
    auto e = state[4];
    for (unsigned i = 0; i < words.size(); ++i) {
        std::uint32_t function;
        std::uint32_t constant;
        if (i < 20) {
            function = (b & c) | (~b & d);
            constant = 0x5a827999;
        } else if (i < 40) {
            function = b ^ c ^ d;
            constant = 0x6ed9eba1;
        } else if (i < 60) {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8f1bbcdc;
        } else {
            function = b ^ c ^ d;
            constant = 0xca62c1d6;
        }
        const auto temp = rotate_left(a, 5) + function + e + constant + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

} // namespace

std::string sha1_hex(std::span<const std::uint8_t> data) {
    std::array<std::uint32_t, 5> state{
        0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0,
    };
    std::size_t offset = 0;
    for (; offset + 64 <= data.size(); offset += 64)
        process_block(data.data() + offset, state);

    std::array<std::uint8_t, 128> tail{};
    const auto remaining = data.size() - offset;
    for (std::size_t i = 0; i < remaining; ++i) tail[i] = data[offset + i];
    tail[remaining] = 0x80;
    const std::size_t padded_size = remaining < 56 ? 64 : 128;
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
    for (unsigned i = 0; i < 8; ++i)
        tail[padded_size - 1 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
    process_block(tail.data(), state);
    if (padded_size == 128) process_block(tail.data() + 64, state);

    constexpr char hex[] = "0123456789abcdef";
    std::string result(40, '0');
    for (std::size_t i = 0; i < state.size(); ++i) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            const auto value = static_cast<std::uint8_t>(state[i] >> (24 - byte * 8));
            result[(i * 4 + byte) * 2] = hex[value >> 4];
            result[(i * 4 + byte) * 2 + 1] = hex[value & 0x0f];
        }
    }
    return result;
}

} // namespace mamerecomp::roms
