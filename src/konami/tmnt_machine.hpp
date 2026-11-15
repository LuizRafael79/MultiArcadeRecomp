#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <array>
#include <memory>
#include <functional>

#include "upd7759.hpp"
#include "ymfm_opm.h"
#include <neogeorecomp/neogeo.h>

namespace multiarcaderecomp::z80 { class Z80; class Z80Bus; }

namespace multiarcaderecomp::konami {

struct TmntRomSet {
    std::unordered_map<std::string, std::vector<uint8_t>> regions;
    const std::vector<uint8_t>& at(std::string_view region) const;
};

struct TmntFrame {
    unsigned width = 320;
    unsigned height = 224;
    std::vector<uint16_t> palette_indices;
    std::vector<uint8_t> shadow_pixels;
};

// Loads and verifies the original 1989 TMNT parent set using MAME's ROM_LOAD
// geometry. ROM data is consumed from the ZIP and is never extracted.
TmntRomSet load_tmnt_romset(const std::filesystem::path& zip);

// Original 1989 TMNT board bus. The video ASIC state and main bus are kept
// separate from the later TMNT2/K053245 board implementation.
class TmntBoard : private ymfm::ymfm_interface {
public:
    explicit TmntBoard(TmntRomSet roms);
    uint16_t main_read(uint32_t address, uint16_t mem_mask = 0xffff) const;
    void main_write(uint32_t address, uint16_t value, uint16_t mem_mask = 0xffff);
    void set_sound_sync_callback(std::function<void(uint64_t)> callback);
    void sync_sound_to_main_cycles(uint64_t main_cpu_cycles);
    void set_input(unsigned port, uint16_t value);
    void video_vblank();
    bool irq5_pending() const { return irq5_pending_; }
    bool sound_irq_pending() const { return sound_irq_pending_; }
    uint8_t sound_latch() const { return sound_latch_; }
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void acknowledge_sound_irq() { sound_irq_pending_ = false; }
    void advance_sound_to_cycles(uint64_t cycles);
    std::vector<int16_t> take_audio_samples();
    uint32_t audio_sample_rate() const { return ym2151_.sample_rate(3'579'545); }
    uint8_t tile_pixel(unsigned layer, unsigned tile, unsigned x, unsigned y) const;
    TmntFrame render_frame() const;
    std::vector<uint8_t> frame_rgb(const TmntFrame& frame) const;
    const TmntRomSet& roms() const { return roms_; }

private:
    void ymfm_set_timer(uint32_t timer, int32_t duration) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class, uint32_t, uint8_t) override;
    static uint16_t read_word(const std::vector<uint8_t>& bytes, size_t offset);
    static void write_word(std::vector<uint8_t>& bytes, size_t offset,
                           uint16_t value, uint16_t mem_mask);
    TmntRomSet roms_;
    std::vector<uint8_t> work_ram_ = std::vector<uint8_t>(0x4000);
    std::vector<uint8_t> palette_ram_ = std::vector<uint8_t>(0x1000);
    std::vector<uint8_t> tile_ram_ = std::vector<uint8_t>(0x6000);
    std::vector<uint8_t> sprite_ram_ = std::vector<uint8_t>(0x400);
    std::vector<uint8_t> sprite_buffer_ = std::vector<uint8_t>(0x400);
    std::vector<uint8_t> sound_ram_ = std::vector<uint8_t>(0x800);
    audio::Upd7759 upd7759_{audio::Upd7759::Mode::Rom};
    std::array<uint8_t, 8> sprite_regs_{};
    std::array<uint8_t, 2> ym_registers_{};
    std::array<uint8_t, 0x10> k007232_registers_{};
    struct K007232Voice {
        uint32_t address = 0;
        uint32_t start = 0;
        uint16_t step = 0;
        int counter = 0x1000;
        std::array<uint8_t, 2> volume{255, 0};
        bool playing = false;
    };
    std::array<K007232Voice, 2> k007232_voices_{};
    int k007232_left_ = 0;
    int k007232_right_ = 0;
    ymfm::ym2151 ym2151_;
    uint8_t ym_address_ = 0;
    uint64_t sound_cycles_ = 0;
    uint64_t generated_sound_samples_ = 0;
    uint64_t title_sample_position_ = 0;
    uint64_t title_sample_phase_ = 0;
    uint64_t upd_clock_fraction_ = 0;
    uint64_t ym_busy_until_ = 0;
    std::array<uint64_t, 2> ym_timer_deadlines_{};
    std::array<bool, 2> ym_timer_active_{};
    std::vector<int16_t> audio_queue_;
    std::array<uint16_t, 8> inputs_{};
    uint8_t last_control_ = 0;
    uint8_t sres_control_ = 0;
    bool upd_reset_line_ = false;
    bool upd_start_line_ = true;
    uint8_t sound_latch_ = 0;
    uint8_t priority_ = 0;
    std::array<uint8_t, 4> char_banks_{};
    uint8_t addrmap_ = 0;
    uint8_t rom_subbank_ = 0;
    uint8_t scroll_control_ = 0;
    uint8_t tile_flip_ = 0;
    bool irq5_enabled_ = false;
    bool irq5_pending_ = false;
    bool sound_irq_pending_ = false;
    bool ym_irq_ = false;
    bool title_sample_playing_ = false;
    bool char_rom_read_ = false;
    std::function<void(uint64_t)> sound_sync_callback_;
};

class TmntMainCpu {
public:
    TmntMainCpu(TmntBoard& board, const ng_func_entry_t* entries, size_t entry_count);
    ~TmntMainCpu();
    TmntMainCpu(const TmntMainCpu&) = delete;
    TmntMainCpu& operator=(const TmntMainCpu&) = delete;
    void reset();
    int execute_until(uint64_t cycle_deadline);
    uint32_t pc() const;
    uint64_t cycles() const;
    uint64_t aot_instructions() const { return aot_instructions_; }
    uint64_t fallback_instructions() const { return fallback_instructions_; }
private:
    TmntBoard& board_;
    const ng_func_entry_t* entries_ = nullptr;
    size_t entry_count_ = 0;
    uint64_t aot_instructions_ = 0;
    uint64_t fallback_instructions_ = 0;
};

class TmntSoundCpu {
public:
    explicit TmntSoundCpu(TmntBoard& board);
    ~TmntSoundCpu();
    TmntSoundCpu(const TmntSoundCpu&) = delete;
    TmntSoundCpu& operator=(const TmntSoundCpu&) = delete;
    void reset();
    void run(uint64_t cycles);
    void run_until(uint64_t cycle_timestamp);
    uint16_t pc() const;
    uint16_t sp() const;
    uint64_t cycles() const;
    uint64_t aot_instructions() const;
    uint64_t fallback_instructions() const;
private:
    TmntBoard& board_;
    std::unique_ptr<multiarcaderecomp::z80::Z80Bus> bus_;
    std::unique_ptr<multiarcaderecomp::z80::Z80> cpu_;
};

} // namespace multiarcaderecomp::konami
