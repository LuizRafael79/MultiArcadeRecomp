// license:BSD-3-Clause
// Adapted from MAME src/devices/cpu/psx/mdec.h (copyright 2003-2011 smf).
#pragma once

#include <cstdint>

class psxmdec_device {
public:
    psxmdec_device();
    void device_start();
    void device_reset();
    void device_post_load();
    void write(unsigned offset, uint32_t data);
    uint32_t read(unsigned offset);
    void dma_write(uint32_t *ram, uint32_t address, int32_t words);
    void dma_read(uint32_t *ram, uint32_t address, int32_t words);
private:
    static constexpr unsigned DCTSIZE = 8;
    static constexpr unsigned DCTSIZE2 = 64;
    static constexpr unsigned MDEC_COS_PRECALC_BITS = 21;
    static const uint32_t m_p_n_mdec_zigzag[DCTSIZE2];
    void mdec_cos_precalc();
    void mdec_idct(int32_t *src, int32_t *dst);
    uint32_t mdec_unpack(uint32_t *ram, uint32_t address);
    uint16_t mdec_clamp_r5(int32_t value) const;
    uint16_t mdec_clamp_g5(int32_t value) const;
    uint16_t mdec_clamp_b5(int32_t value) const;
    uint16_t mdec_clamp8(int32_t value) const;
    void mdec_yuv2_to_rgb15();
    void mdec_yuv2_to_rgb24();
    void mdec_makergb15(uint32_t address, int32_t r, int32_t g, int32_t b, int32_t *y, uint16_t stp);
    void mdec_makergb24(uint32_t address, int32_t r, int32_t g, int32_t b, int32_t *y, uint32_t stp);
    uint32_t n_decoded = 0, n_offset = 0;
    uint16_t p_n_output[24 * 16]{};
    int32_t p_n_quantize_y[DCTSIZE2]{};
    int32_t p_n_quantize_uv[DCTSIZE2]{};
    int32_t p_n_cos[DCTSIZE2]{};
    int32_t p_n_cos_precalc[DCTSIZE2 * DCTSIZE2]{};
    uint32_t n_0_command = 0, n_0_address = 0, n_0_size = 0;
    uint32_t n_1_command = 0, n_1_status = 0;
    uint16_t p_n_clamp8[256 * 3]{};
    uint16_t p_n_r5[256 * 3]{};
    uint16_t p_n_g5[256 * 3]{};
    uint16_t p_n_b5[256 * 3]{};
    int32_t m_p_n_unpacked[DCTSIZE2 * 6 * 2]{};
};
