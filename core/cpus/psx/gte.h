// license:BSD-3-Clause
// Adapted from MAME src/devices/cpu/psx/gte.h (copyright 2003-2013 smf).
#pragma once

#include <cstdint>

namespace mamerecomp::psx {

union GtePair {
    struct { uint8_t l, h, h2, h3; } b;
    struct { int8_t l, h, h2, h3; } sb;
    struct { uint16_t l, h; } w;
    struct { int16_t l, h; } sw;
    uint32_t d;
    int32_t sd;
};

#define GTE_SF(op) (((op) >> 19) & 1)
#define GTE_MX(op) (((op) >> 17) & 3)
#define GTE_V(op) (((op) >> 15) & 3)
#define GTE_CV(op) (((op) >> 13) & 3)
#define GTE_LM(op) (((op) >> 10) & 1)
#define GTE_FUNCT(op) ((op) & 63)

class Gte {
public:
    GtePair m_cp2cr[32]{};
    GtePair m_cp2dr[32]{};
    uint32_t getcp2dr(uint32_t pc, int reg);
    void setcp2dr(uint32_t pc, int reg, uint32_t value);
    uint32_t getcp2cr(uint32_t pc, int reg);
    void setcp2cr(uint32_t pc, int reg, uint32_t value);
    int docop2(uint32_t pc, int gteop);

protected:
    class int44 {
    public:
        int44(int64_t value) : m_value(value),
            m_positive_overflow(value > 0x7ffffffffffll),
            m_negative_overflow(value < -0x80000000000ll) {}
        int44(int64_t value, bool positive, bool negative)
            : m_value(value), m_positive_overflow(positive), m_negative_overflow(negative) {}
        int44 operator+(int64_t add);
        bool positive_overflow() { return m_positive_overflow; }
        bool negative_overflow() { return m_negative_overflow; }
        int64_t value() { return m_value; }
    private:
        int64_t m_value;
        bool m_positive_overflow, m_negative_overflow;
    };
    int32_t LIM(int32_t, int32_t, int32_t, uint32_t);
    int32_t BOUNDS(int44, int, int);
    int32_t A1(int44); int32_t A2(int44); int32_t A3(int44);
    int32_t Lm_B1(int32_t, int); int32_t Lm_B2(int32_t, int); int32_t Lm_B3(int32_t, int);
    int32_t Lm_B3_sf(int64_t, int, int);
    int32_t Lm_C1(int32_t); int32_t Lm_C2(int32_t); int32_t Lm_C3(int32_t);
    int32_t Lm_D(int64_t, int); uint32_t Lm_E(uint32_t); int64_t F(int64_t);
    int32_t Lm_G1(int64_t); int32_t Lm_G2(int64_t); int32_t Lm_H(int64_t, int);
    int m_sf = 0;
    int64_t m_mac0 = 0, m_mac1 = 0, m_mac2 = 0, m_mac3 = 0;
};

} // namespace mamerecomp::psx
