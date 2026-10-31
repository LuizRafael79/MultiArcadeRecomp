/*
 * Naomi M1 ROM-board stream decoder.
 * Algorithm follows MAME src/mame/sega/naomim1.cpp and
 * src/mame/sega/naomim1.h (BSD-3-Clause, Olivier Galibert).
 */
#include "naomi_m1crypt.hpp"

#include <algorithm>
#include <stdexcept>

namespace mamerecomp::naomi {

void M1CryptStream::start_dma(std::span<const std::uint8_t> encrypted_region,
                              std::uint32_t key, std::uint32_t dma_address,
                              bool is_dma) {
    rom_ = encrypted_region;
    // MAME byte-swaps the board key when it reads the ROM-board parameter.
    key_ = ((key & 0x000000ffu) << 24) | ((key & 0x0000ff00u) << 8) |
           ((key & 0x00ff0000u) >> 8) | ((key & 0xff000000u) >> 24);
    rom_address_ = dma_address & 0x1fffffffu;
    encrypted_ = ((dma_address & 0x20000000u) == 0) && is_dma;
    buffer_size_ = 0;
    has_history_ = false;
    stream_ended_ = false;
    available_value_ = 0;
    available_bits_ = 0;
    if (encrypted_) {
        reset_stream();
        fill_buffer();
    }
}

std::uint32_t M1CryptStream::read_cipher_word() {
    std::uint8_t a = 0xff, b = 0xff, c = 0xff, d = 0xff;
    if (rom_address_ < rom_.size()) a = rom_[rom_address_];
    if (rom_address_ + 1 < rom_.size()) b = rom_[rom_address_ + 1];
    if (rom_address_ + 2 < rom_.size()) c = rom_[rom_address_ + 2];
    if (rom_address_ + 3 < rom_.size()) d = rom_[rom_address_ + 3];
    rom_address_ += 4;
    return key_ ^ ((std::uint32_t(b ^ d) << 24) |
                   (std::uint32_t(a ^ c) << 16) |
                   (std::uint32_t(b) << 8) | a);
}

std::uint32_t M1CryptStream::look_bits(unsigned count) {
    if (count > available_bits_) {
        available_value_ = (available_value_ << 32) | read_cipher_word();
        available_bits_ += 32;
    }
    return static_cast<std::uint32_t>((available_value_ >> (available_bits_ - count)) &
                                       ((std::uint32_t(1) << count) - 1));
}

std::uint32_t M1CryptStream::get_bits(unsigned count) {
    const auto value = look_bits(count);
    skip_bits(count);
    return value;
}

void M1CryptStream::skip_bits(unsigned count) { available_bits_ -= count; }

void M1CryptStream::reset_stream() {
    available_value_ = 0;
    available_bits_ = 0;
    stream_ended_ = false;
    has_history_ = false;
    buffer_size_ = 0;
    for (auto& value : dictionary_) value = static_cast<std::uint8_t>(get_bits(8));
}

void M1CryptStream::emit_byte(std::uint8_t value) {
    if (dictionary_[0] & 0x40) {
        if (buffer_size_ < 2)
            buffer_[buffer_size_] = has_history_
                ? static_cast<std::uint8_t>(history_[buffer_size_] - value) : value;
        else
            buffer_[buffer_size_] = static_cast<std::uint8_t>(buffer_[buffer_size_ - 2] - value);
    } else {
        buffer_[buffer_size_] = value;
    }
    ++buffer_size_;
}

void M1CryptStream::fill_buffer() {
    while (buffer_size_ < sizeof(buffer_) && !stream_ended_) {
        switch (look_bits(3)) {
        case 0: case 1: {
            skip_bits(2);
            const auto address = get_bits(2);
            emit_byte(address ? dictionary_[address] : static_cast<std::uint8_t>(get_bits(8)));
            break;
        }
        case 2:
            skip_bits(3);
            emit_byte(dictionary_[get_bits(2) + 4]);
            break;
        case 3:
            skip_bits(3);
            emit_byte(dictionary_[get_bits(3) + 8]);
            break;
        case 4: case 5:
            skip_bits(2);
            emit_byte(dictionary_[get_bits(5) + 16]);
            break;
        case 6: case 7: {
            skip_bits(2);
            const auto address = get_bits(6) + 48;
            if (address == 111) stream_ended_ = true;
            else emit_byte(dictionary_[address]);
            break;
        }
        }
    }
    while (buffer_size_ < sizeof(buffer_)) buffer_[buffer_size_++] = 0;
}

void M1CryptStream::advance(std::size_t size) {
    if (size < buffer_size_) {
        std::move(buffer_ + size, buffer_ + buffer_size_, buffer_);
        buffer_size_ -= size;
    } else {
        history_[0] = buffer_[buffer_size_ - 2];
        history_[1] = buffer_[buffer_size_ - 1];
        has_history_ = true;
        buffer_size_ = 0;
    }
    fill_buffer();
}

std::vector<std::uint8_t> M1CryptStream::read(std::size_t size) {
    std::vector<std::uint8_t> result(size);
    if (!encrypted_) {
        for (std::size_t i = 0; i < size; ++i)
            result[i] = rom_address_ + i < rom_.size() ? rom_[rom_address_ + i] : 0xff;
        rom_address_ += static_cast<std::uint32_t>(size);
        return result;
    }
    std::size_t written = 0;
    while (written < size) {
        if (buffer_size_ == 0) fill_buffer();
        const auto count = std::min(size - written, buffer_size_);
        std::copy_n(buffer_, count, result.begin() + written);
        written += count;
        advance(count);
    }
    return result;
}

} // namespace mamerecomp::naomi
