#pragma once

#include <array>
#include <cstdint>

namespace multiarcaderecomp::psx {

// CAT702 serial security IC used by Sony ZN boards. Algorithm and pin behavior
// follow MAME's BSD-3-Clause src/devices/machine/cat702.cpp.
class Cat702 {
public:
    explicit Cat702(const std::array<uint8_t, 8> &transform =
                    std::array<uint8_t, 8>{0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff});
    void select(bool state);
    void clock(bool state);
    void data_in(bool state) { data_in_ = state; }
    bool data_out() const { return data_out_; }

private:
    uint8_t compute_sbox_coef(unsigned select, unsigned bit) const;
    void apply_bit_sbox(unsigned select);
    void apply_sbox(const std::array<uint8_t, 8> &sbox);

    std::array<uint8_t, 8> transform_{};
    bool selected_ = true;
    bool clock_ = true;
    bool data_in_ = true;
    bool data_out_ = true;
    uint8_t state_ = 0;
    uint8_t bit_ = 0;
};

} // namespace multiarcaderecomp::psx
