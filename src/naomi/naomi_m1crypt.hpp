#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mamerecomp::naomi {

// Naomi M1 ROM-board DMA stream. The caller supplies the assembled encrypted
// region and board key; start_dma mirrors the board's DMA-address mode bit.
class M1CryptStream {
public:
    void start_dma(std::span<const std::uint8_t> encrypted_region,
                   std::uint32_t key, std::uint32_t dma_address, bool is_dma);
    std::vector<std::uint8_t> read(std::size_t size);

    bool encryption_enabled() const { return encrypted_; }

private:
    std::uint32_t read_cipher_word();
    std::uint32_t look_bits(unsigned count);
    std::uint32_t get_bits(unsigned count);
    void skip_bits(unsigned count);
    void reset_stream();
    void emit_byte(std::uint8_t value);
    void fill_buffer();
    void advance(std::size_t size);

    std::span<const std::uint8_t> rom_{};
    std::uint32_t key_ = 0;
    std::uint32_t rom_address_ = 0;
    std::uint64_t available_value_ = 0;
    unsigned available_bits_ = 0;
    std::uint8_t dictionary_[111]{};
    std::uint8_t history_[2]{};
    std::uint8_t buffer_[2]{};
    std::size_t buffer_size_ = 0;
    bool stream_ended_ = false;
    bool has_history_ = false;
    bool encrypted_ = false;
};

} // namespace mamerecomp::naomi
