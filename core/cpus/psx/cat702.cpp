// license:BSD-3-Clause
// CAT702 algorithm adapted from MAME src/devices/machine/cat702.cpp.
#include "cat702.h"

namespace multiarcaderecomp::psx {
namespace {
constexpr std::array<uint8_t, 8> initial_sbox{
    0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x7f
};
}

Cat702::Cat702(const std::array<uint8_t, 8> &transform) : transform_(transform) {}

uint8_t Cat702::compute_sbox_coef(unsigned sel, unsigned bit) const
{
    if (!sel) return transform_[bit];
    uint8_t r = compute_sbox_coef((sel - 1) & 7, (bit - 1) & 7);
    r = uint8_t((r << 1) | (((r >> 7) ^ (r >> 6)) & 1));
    return bit != 7 ? r : uint8_t(r ^ compute_sbox_coef(sel, 0));
}

void Cat702::apply_bit_sbox(unsigned sel)
{
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
        if ((state_ >> i) & 1) result ^= compute_sbox_coef(sel, i);
    state_ = result;
}

void Cat702::apply_sbox(const std::array<uint8_t, 8> &sbox)
{
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
        if ((state_ >> i) & 1) result ^= sbox[i];
    state_ = result;
}

void Cat702::select(bool state)
{
    if (selected_ == state) return;
    if (!state) {
        state_ = 0xfc;
        bit_ = 0;
    } else {
        data_out_ = true;
    }
    selected_ = state;
}

void Cat702::clock(bool state)
{
    if (!state && clock_ && !selected_) {
        if (bit_ == 0) apply_sbox(initial_sbox);
        data_out_ = ((state_ >> bit_) & 1) != 0;
    }
    if (state && !clock_ && !selected_) {
        if (!data_in_) apply_bit_sbox(bit_);
        bit_ = (bit_ + 1) & 7;
    }
    clock_ = state;
}

} // namespace multiarcaderecomp::psx
