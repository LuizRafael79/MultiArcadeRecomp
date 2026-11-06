#include "upd7759.hpp"

#include <algorithm>

namespace mamerecomp::audio {
namespace {
constexpr std::int16_t step[16][16] = {
    {0,0,1,2,3,5,7,10,0,0,-1,-2,-3,-5,-7,-10},
    {0,1,2,3,4,6,8,13,0,-1,-2,-3,-4,-6,-8,-13},
    {0,1,2,4,5,7,10,15,0,-1,-2,-4,-5,-7,-10,-15},
    {0,1,3,4,6,9,13,19,0,-1,-3,-4,-6,-9,-13,-19},
    {0,2,3,5,8,11,15,23,0,-2,-3,-5,-8,-11,-15,-23},
    {0,2,4,7,10,14,19,29,0,-2,-4,-7,-10,-14,-19,-29},
    {0,3,5,8,12,16,22,33,0,-3,-5,-8,-12,-16,-22,-33},
    {1,4,7,10,15,20,29,43,-1,-4,-7,-10,-15,-20,-29,-43},
    {1,4,8,13,18,25,35,53,-1,-4,-8,-13,-18,-25,-35,-53},
    {1,6,10,16,22,31,43,64,-1,-6,-10,-16,-22,-31,-43,-64},
    {2,7,12,19,27,37,51,76,-2,-7,-12,-19,-27,-37,-51,-76},
    {2,9,16,24,34,46,64,96,-2,-9,-16,-24,-34,-46,-64,-96},
    {3,11,19,29,41,57,79,117,-3,-11,-19,-29,-41,-57,-79,-117},
    {4,13,24,36,50,69,96,143,-4,-13,-24,-36,-50,-69,-96,-143},
    {4,16,29,44,62,85,118,175,-4,-16,-29,-44,-62,-85,-118,-175},
    {6,20,36,54,76,104,144,214,-6,-20,-36,-54,-76,-104,-144,-214}
};
constexpr std::int8_t state_delta[16] = {-1,-1,0,0,1,2,2,3,-1,-1,0,0,1,2,2,3};
}

void Upd7759::reset()
{
    state_ = post_state_ = State::Idle;
    clocks_left_ = post_clocks_ = offset_ = repeat_offset_ = 0;
    nibbles_left_ = 0;
    repeat_count_ = adpcm_state_ = 0;
    request_sample_ = mode_ == Mode::Rom ? fifo_in_ : 0x10;
    last_sample_ = 0x10;
    header_ = adpcm_data_ = 0;
    sample_rate_ = 1;
    sample_ = 0;
    drq_ = first_valid_header_ = false;
}

void Upd7759::start()
{
    if (state_ != State::Idle) return;
    state_ = State::Start;
    clocks_left_ = 0;
}

std::uint8_t Upd7759::read_rom(std::uint32_t address) const
{
    return address < rom_.size() ? rom_[address] : 0xff;
}

void Upd7759::decode_nibble(std::uint8_t nibble)
{
    nibble &= 15;
    sample_ += step[adpcm_state_][nibble];
    adpcm_state_ = static_cast<std::uint8_t>(std::clamp(
        int(adpcm_state_) + state_delta[nibble], 0, 15));
}

void Upd7759::request_byte()
{
    drq_ = true;
    if (mode_ == Mode::Fifo && request_callback_) request_callback_();
}

void Upd7759::advance(std::uint32_t clocks)
{
    while (clocks--) advance_one();
}

void Upd7759::advance_one()
{
    if (clocks_left_) {
        --clocks_left_;
        if (clocks_left_) return;
    }
    auto byte = [&](std::uint32_t address) {
        return mode_ == Mode::Rom ? read_rom(address) : fifo_in_;
    };
    switch (state_) {
    case State::Idle:
        clocks_left_ = 4;
        return;
    case State::DropDrq:
        drq_ = false;
        clocks_left_ = post_clocks_;
        state_ = post_state_;
        break;
    case State::Start:
        request_sample_ = mode_ == Mode::Rom ? fifo_in_ : 0x10;
        clocks_left_ = 70;
        state_ = State::FirstRequest;
        break;
    case State::FirstRequest:
        clocks_left_ = 44; state_ = State::LastSample; request_byte(); break;
    case State::LastSample:
        last_sample_ = byte(0);
        clocks_left_ = 28;
        state_ = request_sample_ > last_sample_ ? State::Idle : State::Dummy1;
        request_byte();
        break;
    case State::Dummy1:
        clocks_left_ = 32; state_ = State::AddressMsb; request_byte(); break;
    case State::AddressMsb:
        offset_ = std::uint32_t(byte(request_sample_ * 2 + 5)) << 9;
        clocks_left_ = 44; state_ = State::AddressLsb; request_byte(); break;
    case State::AddressLsb:
        offset_ |= std::uint32_t(byte(request_sample_ * 2 + 6)) << 1;
        clocks_left_ = 36; state_ = State::Dummy2; request_byte(); break;
    case State::Dummy2:
        ++offset_; first_valid_header_ = false;
        clocks_left_ = 36; state_ = State::Header; request_byte(); break;
    case State::Header:
        if (repeat_count_) { --repeat_count_; offset_ = repeat_offset_; }
        header_ = byte(offset_++);
        switch (header_ & 0xc0) {
        case 0x00:
            sample_ = 0; adpcm_state_ = 0;
            clocks_left_ = 1024U * ((header_ & 0x3f) + 1);
            state_ = (header_ == 0 && first_valid_header_) ? State::Idle : State::Header;
            break;
        case 0x40:
            sample_rate_ = (header_ & 0x3f) + 1; nibbles_left_ = 256;
            clocks_left_ = 36; state_ = State::NibbleMsb;
            break;
        case 0x80:
            sample_rate_ = (header_ & 0x3f) + 1;
            clocks_left_ = 36; state_ = State::Count;
            break;
        default:
            repeat_count_ = (header_ & 7) + 1; repeat_offset_ = offset_;
            clocks_left_ = 36; state_ = State::Header;
            break;
        }
        if (header_) first_valid_header_ = true;
        request_byte();
        break;
    case State::Count:
        nibbles_left_ = std::uint16_t(byte(offset_++)) + 1;
        clocks_left_ = 36; state_ = State::NibbleMsb; request_byte(); break;
    case State::NibbleMsb:
        adpcm_data_ = byte(offset_++); decode_nibble(adpcm_data_ >> 4);
        clocks_left_ = sample_rate_ * 4;
        state_ = (--nibbles_left_ == 0) ? State::Header : State::NibbleLsb;
        request_byte();
        break;
    case State::NibbleLsb:
        decode_nibble(adpcm_data_ & 15);
        clocks_left_ = sample_rate_ * 4;
        state_ = (--nibbles_left_ == 0) ? State::Header : State::NibbleMsb;
        break;
    }
    if (drq_) {
        post_state_ = state_;
        post_clocks_ = clocks_left_ > 21 ? clocks_left_ - 21 : 0;
        state_ = State::DropDrq;
        clocks_left_ = 21;
    }
}

} // namespace mamerecomp::audio
