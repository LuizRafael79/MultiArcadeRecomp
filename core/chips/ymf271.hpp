// license:BSD-3-Clause
// copyright-holders:superctr
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Standalone sound-core port of the MAME YMF271 OPX implementation. Board
// clocks, external ROM, scheduling and host audio delivery remain caller-owned.
class Ymf271 {
public:
    explicit Ymf271(uint32_t master_clock, std::span<const uint8_t> sample_rom);
    void reset();
    uint8_t read(uint32_t offset);
    void write(uint32_t offset, uint8_t data);
    void advance_master_clocks(uint64_t clocks);
    std::vector<int16_t> take_audio();
    uint32_t sample_rate() const { return m_master_clock / 384; }
    uint64_t generated_samples() const { return m_generated_samples; }
    uint32_t peak() const { return m_peak; }
    unsigned active_slots() const;

private:
    static constexpr int NUM_SLOTS = 48;
    static constexpr int NUM_GROUPS = 12;
    struct opx_alg { uint8_t mods[4]; uint8_t car; uint8_t fbsrc; };
    struct opx_slot {
        uint8_t kon, ext_en, ext_out, lfo_freq;
        uint8_t ams, pms, lfo_wave, dt, mul, tl, ks, ar, d1r, d2r;
        uint8_t d1l, rr;
        uint16_t fnum;
        uint8_t block, fnum_latch, accon, fb, wave, alg, ch_level[4];
        uint32_t pcm_start, pcm_end, pcm_loop;
        uint8_t pcm_altloop, pcm_fs, pcm_12bit, pcm_srcnote, pcm_srcb;
        int8_t block_s;
        uint8_t keycode, eg_state;
        int32_t eg_att;
        uint32_t phase;
        int32_t out, acc, fb_hist[2];
        uint32_t lfo_cnt;
        uint8_t lfo_pos;
        uint32_t pcm_pos, pcm_frac;
        uint8_t pcm_ended;
        uint8_t c_nmod, c_mod[3], c_fbhead;
        int8_t c_fbtarget;
        uint8_t c_carrier;
    };
    struct opx_group { uint8_t sync, pfm, dirty; };
    struct alg_tables;

    void init_tables();
    bool is_keyon_slot(int bank, int group) const;
    int voice_slots(int bank, int group, int *slots) const;
    static void update_keycode(opx_slot &s, int slotnum);
    void slot_keyon(int slotnum);
    void slot_keyoff(int slotnum);
    void write_slot_reg(int slotnum, int reg, uint8_t data);
    void write_fm(int bank, uint8_t address, uint8_t data);
    void write_pcm(uint8_t address, uint8_t data);
    void write_util(uint8_t address, uint8_t data);
    void update_irq();
    void connect(const opx_alg &alg, const int *slots, int n);
    void rebuild_group(int g);
    void eg_tick(opx_slot &s);
    static int eg_rate(int rate2, int rks);
    static uint32_t lfo_period(uint8_t n);
    static void lfo_tick(opx_slot &s);
    static int32_t lfo_pm(const opx_slot &s);
    static int32_t lfo_am(const opx_slot &s);
    static uint32_t phase_inc(const opx_slot &s, int32_t lfo_pm);
    static uint32_t pcm_step(const opx_slot &s, int32_t lfo_pm);
    int32_t pcm_word(const opx_slot &s, uint32_t pos);
    int32_t pcm_sample(opx_slot &s, int slotnum, int32_t lfo_pm);
    int32_t env_mul(int32_t v, uint32_t env) const;
    int32_t op(uint32_t phase, int wave, uint32_t env) const;
    static int32_t pan(int32_t v, uint8_t level);
    void render_one();
    uint8_t read_sample_rom(uint32_t address) const;

    uint16_t m_logsin[256]{};
    uint16_t m_exp[256]{};
    opx_slot m_slots[NUM_SLOTS]{};
    opx_group m_groups[NUM_GROUPS]{};
    uint8_t m_regs_main[0x10]{};
    uint16_t m_timerA = 0;
    uint8_t m_timerB = 0, m_timer_ctrl = 0, m_status = 0;
    uint16_t m_end_status = 0;
    uint8_t m_irqstate = 0;
    uint32_t m_ext_address = 0;
    uint8_t m_ext_rw = 0, m_ext_readlatch = 0;
    uint32_t m_eg_cnt = 0;
    uint8_t m_eg_phase = 0;
    uint32_t m_master_clock = 0;
    uint64_t m_master_remainder = 0;
    uint64_t m_timerA_clocks = 0, m_timerB_clocks = 0;
    uint64_t m_generated_samples = 0;
    uint32_t m_peak = 0;
    std::span<const uint8_t> m_sample_rom;
    std::vector<int16_t> m_audio;
};
