#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <neogeorecomp/neogeo.h>
#include "ymfm_opl.h"
#include "chips/oki6295.hpp"

namespace multiarcaderecomp::seibu {

struct TokiRomSet {
    std::unordered_map<std::string, std::vector<uint8_t>> regions;
    const std::vector<uint8_t>& at(std::string_view region) const;
};

TokiRomSet load_toki_romset(const std::filesystem::path& zip);
uint8_t toki_sei80bu_data(uint16_t address, uint8_t source);
uint8_t toki_sei80bu_opcode(uint16_t address, uint8_t source);

struct TokiFrame {
    static constexpr unsigned width = 256;
    static constexpr unsigned height = 224;
    std::vector<uint8_t> rgb;
};

class TokiMainCpu {
public:
    TokiMainCpu(class TokiBoard& board, const ::ng_func_entry_t* entries, size_t entry_count);
    ~TokiMainCpu();
    TokiMainCpu(const TokiMainCpu&) = delete;
    TokiMainCpu& operator=(const TokiMainCpu&) = delete;
    void reset();
    int execute_until(uint64_t cycle_deadline);
    uint32_t pc() const;
    uint64_t cycles() const;
    uint64_t aot_instructions() const;
    uint64_t fallback_instructions() const;
private:
    class Impl;
    Impl* impl_;
};

class TokiSoundCpu {
public:
    explicit TokiSoundCpu(class TokiBoard& board);
    ~TokiSoundCpu();
    TokiSoundCpu(const TokiSoundCpu&) = delete;
    TokiSoundCpu& operator=(const TokiSoundCpu&) = delete;
    void reset();
    void run_until(uint64_t cycle_timestamp);
    uint16_t pc() const;
    uint64_t cycles() const;
    uint64_t instructions() const;
    uint64_t aot_instructions() const;
    uint64_t fallback_instructions() const;
private:
    class Impl;
    Impl* impl_;
};

// Runtime for the original TAD Toki board (MAME seibu/toki.cpp, non-bootleg).
// Address decoding and graphics wiring deliberately remain board-local.
class TokiBoard : private ymfm::ymfm_interface {
    class Ym3812 final : public ymfm::ym3812 {
    public:
        using ymfm::ym3812::ym3812;
        void timer_expired(uint32_t timer) { m_fm.engine_timer_expired(timer); }
    };
public:
    explicit TokiBoard(TokiRomSet roms);
    uint8_t main_read8(uint32_t address);
    uint16_t main_read16(uint32_t address);
    void main_write8(uint32_t address, uint8_t value);
    void main_write16(uint32_t address, uint16_t value);
    void video_vblank();
    void set_inputs(uint16_t controls, uint8_t system, uint8_t coins) {
        input_controls_ = controls;
        input_system_ = static_cast<uint16_t>((input_system_ & 0xff00) | system);
        coin_inputs_ = coins;
    }
    bool main_irq_pending() const { return main_irq_pending_; }
    void acknowledge_main_irq() { main_irq_pending_ = false; }
    TokiFrame render_frame() const;
    std::array<size_t, 5> nonzero_video_bytes() const;
    std::array<size_t, 2> visible_tile_pixels() const;
    std::array<size_t, 32> text_tiles_per_row() const;
    size_t active_sprite_entries() const;
    uint16_t scroll_control() const { return scroll_ram_[0x28]; }
    uint16_t palette_word(unsigned index) const { return read_be16(palette_ram_, (index & 0x3ff) * 2); }
    uint16_t tile_word(unsigned layer, unsigned index) const {
        return read_be16(tile_ram_[layer & 1], (index & 0x3ff) * 2);
    }
    const TokiRomSet& roms() const { return roms_; }
    uint8_t sound_read(uint16_t address, bool opcode_fetch = false);
    void sound_write(uint16_t address, uint8_t value);
    void advance_sound_to_cycles(uint64_t cycles);
    std::vector<int16_t> take_audio_samples();
    uint64_t ym3812_samples() const { return ym_generated_samples_; }
    uint16_t ym3812_peak() const { return ym_peak_; }
    uint64_t ym3812_writes() const { return ym_write_count_; }
    uint64_t ym3812_key_ons() const { return ym_key_on_count_; }
    uint64_t oki_writes() const { return oki_write_count_; }
    unsigned oki_active_voices() const { return oki_.active_voices(); }
    uint8_t ym3812_last_register() const { return ym_address_; }
    uint8_t ym3812_last_value() const { return ym_last_value_; }
    uint64_t sound_command_writes() const { return sound_command_writes_; }
    uint64_t sound_irq_assertions() const { return sound_irq_assertions_; }
    uint64_t sound_irq18_acks() const { return sound_irq18_acks_; }
    uint64_t sound_irq10_acks() const { return sound_irq10_acks_; }
    uint64_t sound_latch_reads() const { return sound_latch_reads_; }
    uint8_t last_sound_latch_value() const { return last_sound_latch_value_; }
    uint64_t sound_pending_acks() const { return sound_pending_acks_; }
    uint16_t last_sound_bus_word() const { return last_sound_bus_word_; }
    uint64_t sound_low_lane_nonzero() const { return sound_low_lane_nonzero_; }
    uint64_t sound_high_lane_nonzero() const { return sound_high_lane_nonzero_; }
    uint32_t audio_sample_rate() const { return ym3812_.sample_rate(3'579'545); }
    uint8_t sound_irq_vector();
    bool sound_irq_pending() const { return (rst10_irq_ && !rst10_service_) || (rst18_irq_ && !rst18_service_); }
    void sound_irq_acknowledge();
    uint64_t main_irq_count() const { return main_irq_count_; }
private:
    void ymfm_set_timer(uint32_t timer, int32_t duration_in_clocks) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class type, uint32_t address, uint8_t data) override;
    uint16_t read_be16(const std::vector<uint8_t>& bytes, size_t offset) const;
    void write_be16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value,
                    uint16_t mask = 0xffff);
    uint8_t graphics_pixel(std::string_view region, bool chars, unsigned code,
                           unsigned x, unsigned y) const;
    void draw_tile_layer(TokiFrame& frame, unsigned layer, bool opaque) const;
    void draw_sprites(TokiFrame& frame) const;
    void put_pixel(TokiFrame& frame, int x, int y, uint16_t palette_index,
                   bool transparent) const;
    uint32_t palette_rgb(unsigned index) const;

    TokiRomSet roms_;
    std::vector<uint8_t> work_ram_ = std::vector<uint8_t>(0xd800);
    std::vector<uint8_t> sprite_ram_ = std::vector<uint8_t>(0x800);
    std::vector<uint8_t> sprite_buffer_ = std::vector<uint8_t>(0x800);
    std::vector<uint8_t> palette_ram_ = std::vector<uint8_t>(0x800);
    std::array<std::vector<uint8_t>, 2> tile_ram_ {
        std::vector<uint8_t>(0x800), std::vector<uint8_t>(0x800)};
    std::vector<uint8_t> text_ram_ = std::vector<uint8_t>(0x800);
    std::array<uint16_t, 0x30> scroll_ram_{};
    std::array<uint16_t, 2> main_to_sound_{};
    std::array<uint16_t, 2> sound_to_main_{};
    std::vector<uint8_t> sound_ram_ = std::vector<uint8_t>(0x800);
    std::array<uint8_t, 0x10000> sound_bank_{};
    uint8_t sound_bank_select_ = 0;
    bool main_to_sound_pending_ = false;
    bool sound_to_main_pending_ = false;
    bool rst10_irq_ = false;
    bool rst18_irq_ = false;
    bool rst10_service_ = false;
    bool rst18_service_ = false;
    uint64_t main_irq_count_ = 0;
    bool main_irq_pending_ = false;
    uint16_t input_dsw_ = 0xffdf;
    uint16_t input_controls_ = 0xffff;
    uint16_t input_system_ = 0x001f;
    uint8_t coin_inputs_ = 0;
    Ym3812 ym3812_;
    multiarcaderecomp::chips::Oki6295 oki_;
    uint64_t ym_clock_ = 0;
    uint64_t ym_generated_samples_ = 0;
    uint64_t ym_busy_until_ = 0;
    std::array<uint64_t, 2> ym_timer_deadlines_{};
    std::array<bool, 2> ym_timer_active_{};
    std::vector<int16_t> audio_samples_;
    uint16_t ym_peak_ = 0;
    uint64_t ym_write_count_ = 0;
    uint64_t ym_key_on_count_ = 0;
    uint64_t oki_write_count_ = 0;
    uint64_t oki_clock_fraction_ = 0;
    uint8_t ym_address_ = 0;
    uint8_t ym_last_value_ = 0;
    uint64_t sound_command_writes_ = 0;
    uint64_t sound_irq_assertions_ = 0;
    uint64_t sound_irq18_acks_ = 0;
    uint64_t sound_irq10_acks_ = 0;
    uint64_t sound_latch_reads_ = 0;
    uint8_t last_sound_latch_value_ = 0xff;
    uint64_t sound_pending_acks_ = 0;
    uint16_t last_sound_bus_word_ = 0xffff;
    uint64_t sound_low_lane_nonzero_ = 0;
    uint64_t sound_high_lane_nonzero_ = 0;
};

} // namespace multiarcaderecomp::seibu
