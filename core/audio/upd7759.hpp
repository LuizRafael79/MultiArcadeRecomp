#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

namespace mamerecomp::audio {

// Clocked uPD7759 sample player. Supports the standalone ROM mode used by
// Konami TMNT and the FIFO/slave mode used by System 16B sound boards.
class Upd7759 {
public:
    enum class Mode : std::uint8_t { Rom, Fifo };

    explicit Upd7759(Mode mode = Mode::Fifo) : mode_(mode) {}
    void set_rom(std::span<const std::uint8_t> rom) { rom_ = rom; }
    void set_request_callback(std::function<void()> callback) { request_callback_ = std::move(callback); }
    void reset();
    void start();
    void write_port(std::uint8_t value) { fifo_in_ = value; }
    void advance(std::uint32_t clocks);
    bool idle() const { return state_ == State::Idle; }
    std::int32_t pcm_sample() const { return std::int32_t(sample_) * 128; }
    std::uint8_t fifo_value() const { return fifo_in_; }

private:
    enum class State : std::uint8_t {
        Idle, DropDrq, Start, FirstRequest, LastSample, Dummy1,
        AddressMsb, AddressLsb, Dummy2, Header, Count, NibbleMsb, NibbleLsb
    };
    void advance_one();
    void request_byte();
    std::uint8_t read_rom(std::uint32_t address) const;
    void decode_nibble(std::uint8_t nibble);

    Mode mode_;
    std::span<const std::uint8_t> rom_{};
    std::function<void()> request_callback_;
    State state_ = State::Idle, post_state_ = State::Idle;
    std::uint8_t fifo_in_ = 0xff, request_sample_ = 0x10, last_sample_ = 0x10;
    std::uint8_t header_ = 0, sample_rate_ = 1, adpcm_data_ = 0, repeat_count_ = 0;
    std::uint8_t adpcm_state_ = 0;
    std::uint16_t nibbles_left_ = 0;
    std::uint32_t clocks_left_ = 0, post_clocks_ = 0, offset_ = 0, repeat_offset_ = 0;
    std::int32_t sample_ = 0;
    bool drq_ = false, first_valid_header_ = false;
};

} // namespace mamerecomp::audio
