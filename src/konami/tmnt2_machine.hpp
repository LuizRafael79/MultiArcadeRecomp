#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <deque>
#include <memory>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ymfm_opm.h"
#include "devices/serial_eeprom.hpp"
#include <neogeorecomp/neogeo.h>

namespace mamerecomp::z80 { class Z80; class Z80Bus; }

namespace mamerecomp::konami {

struct Tmnt2RomSet {
    std::unordered_map<std::string, std::vector<uint8_t>> regions;
    const std::vector<uint8_t>& at(std::string_view region) const;
};

struct Tmnt2Frame {
    unsigned width = 304;
    unsigned height = 224;
    std::vector<uint16_t> palette_indices;
    std::vector<uint8_t> priorities;
    std::vector<uint8_t> shadow_pixels;
};

// MAME ROM_START/ROM_LOAD geometry and CRCs for the supported parent sets.
// Files are read directly from the ZIP; no extracted ROM images are written.
Tmnt2RomSet load_tmnt2_romset(std::string_view game, const std::filesystem::path& zip);

// Shared Konami TMNT2-board address decoder. Video and sound ICs are held as
// board registers/RAM here and can be replaced by full device models without
// changing either game's 68000/Z80 bus contract.
class Tmnt2Board : private ymfm::ymfm_interface {
public:
    Tmnt2Board(std::string_view game, Tmnt2RomSet roms);

    uint16_t main_read(uint32_t address, uint16_t mem_mask = 0xffff);
    void main_write(uint32_t address, uint16_t value,
                    uint16_t mem_mask = 0xffff);
    uint8_t sound_read(uint16_t address);
    void sound_write(uint16_t address, uint8_t value);
    void video_vblank();
    void set_main_cycles(uint64_t cycles) { main_cycles_ = cycles; }
    bool main_irq4_pending() const { return main_irq4_pending_; }
    std::vector<int16_t> generate_k053260_audio(size_t sample_count);
    void advance_sound_to_cycles(uint64_t z80_cycles);
    std::vector<int16_t> take_audio_samples();
    uint32_t audio_sample_rate() const { return 3'579'545 / 64; }

    void set_input(unsigned port, uint16_t value);
    // Decode one K052109 tile pixel using the live tile RAM, character banks,
    // callback wiring and graphics ROM layout from MAME's 052109 device.
    uint8_t tile_pixel(unsigned layer, unsigned tile, unsigned x, unsigned y) const;
    uint8_t priority(unsigned ci) const;
    uint8_t palette_base(unsigned ci) const;
    Tmnt2Frame render_tile_layers() const;
    void draw_sprites(Tmnt2Frame& frame) const;
    Tmnt2Frame render_frame() const;
    std::vector<uint8_t> frame_rgb(const Tmnt2Frame& frame) const;
    const Tmnt2RomSet& roms() const { return roms_; }
    bool sound_irq_pending() const { return sound_irq_pending_ || ym_irq_; }
    void acknowledge_sound_irq() { sound_irq_pending_ = false; }
    bool sound_nmi_pending() const { return sound_nmi_pending_; }
    void acknowledge_sound_nmi() { sound_nmi_pending_ = false; }
    uint8_t eeprom_control() const { return eeprom_control_; }

private:
    void write_word(std::vector<uint8_t>& bytes, size_t offset,
                    uint16_t value, uint16_t mem_mask);
    uint16_t read_word(const std::vector<uint8_t>& bytes, size_t offset) const;
    uint16_t tmnt2_protection_read_word(uint32_t word_address) const;
    void tmnt2_protection_write_word(uint32_t word_address, uint16_t value);
    void run_tmnt2_protection_blit();
    uint16_t protection_read() const;
    uint8_t tile_rom_read(size_t offset) const;
    uint8_t sprite_device_read(unsigned reg);
    void sprite_device_write(unsigned reg, uint8_t value);
    std::array<int16_t, 2> generate_k053260_sample();
    void synchronize_sound(uint64_t target_cycles);
    void ymfm_set_timer(uint32_t timer, int32_t duration) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    void ymfm_external_write(ymfm::access_class, uint32_t, uint8_t) override;

    bool tmnt2_;
    unsigned screen_width_;
    unsigned visible_x_;
    Tmnt2RomSet roms_;
    mamerecomp::devices::SerialEeprom eeprom_;
    std::vector<uint8_t> work_ram_ = std::vector<uint8_t>(0x4000);
    std::vector<uint8_t> palette_ram_ = std::vector<uint8_t>(0x1000);
    std::vector<uint8_t> sprite_ram_ = std::vector<uint8_t>(0x4000);
    std::vector<uint8_t> sprite_device_ram_ = std::vector<uint8_t>(0x800);
    std::vector<uint8_t> sprite_buffer_ = std::vector<uint8_t>(0x800);
    std::vector<uint8_t> tile_ram_ = std::vector<uint8_t>(0x6000);
    std::vector<uint8_t> protection_ram_ = std::vector<uint8_t>(0x80);
    std::vector<uint8_t> audio_ram_ = std::vector<uint8_t>(0x800);
    std::array<uint16_t, 4> player_inputs_{0xffff, 0xffff, 0xffff, 0xffff};
    uint16_t coin_inputs_ = 0xffff;
    std::array<uint8_t, 0x10> sprite_regs_{};
    std::array<uint8_t, 0x20> priority_regs_{};
    std::array<uint8_t, 0x30> sample_regs_{};
    std::array<uint8_t, 4> sample_ports_{};
    std::array<bool, 4> sample_voice_keyon_{};
    struct SampleVoice {
        uint32_t position = 0;
        uint32_t start = 0;
        uint16_t length = 0;
        uint16_t pitch = 0;
        uint16_t counter = 0;
        int8_t output = 0;
        uint8_t volume = 0;
        uint8_t pan = 0;
        bool playing = false;
        bool loop = false;
        bool kadpcm = false;
        bool reverse = false;
    };
    std::array<SampleVoice, 4> sample_voices_{};
    uint8_t sample_keyon_ = 0;
    uint8_t sample_mode_ = 0;
    std::array<uint8_t, 2> ym2151_regs_{};
    ymfm::ym2151 ym2151_;
    uint64_t sound_cycles_ = 0;
    uint64_t main_cycles_ = 0;
    uint64_t sound_cpu_cycles_ = 0;
    uint64_t next_k053260_tick_ = 16;
    uint64_t sound_nmi_blocked_until_ = 0;
    bool sound_nmi_armed_ = false;
    bool sound_nmi_line_asserted_ = true;
    uint64_t generated_sound_samples_ = 0;
    uint64_t ym_busy_until_ = 0;
    std::array<uint64_t, 2> ym_timer_deadlines_{};
    std::array<bool, 2> ym_timer_active_{};
    std::vector<int16_t> audio_queue_;
    uint8_t ym_address_ = 0;
    uint8_t k053260_timer_state_ = 0;
    bool ym_irq_ = false;
    bool sound_irq_pending_ = false;
    bool sound_nmi_pending_ = false;
    bool main_irq4_pending_ = false;
    uint8_t eeprom_control_ = 0xff;
    uint8_t graphics_control_ = 0xff;
    uint8_t dim_control_ = 0;
    uint8_t sprite_rom_bank_ = 0;
    std::array<uint8_t, 4> tile_char_banks_{};
    std::array<uint8_t, 4> tile_char_banks2_{};
    uint8_t tile_addrmap_ = 0;
    uint8_t tile_rom_subbank_ = 0;
    uint8_t tile_flip_enable_ = 0;
    uint8_t tile_scroll_control_ = 0;
    uint8_t tile_irq_control_ = 0;
    bool tile_rom_read_enabled_ = false;
    bool watchdog_touched_ = false;
};

// Shared 68000 AOT dispatcher with a Musashi instruction fallback.
class Tmnt2MainCpu {
public:
    Tmnt2MainCpu(Tmnt2Board& board, const ng_func_entry_t* entries, size_t entry_count);
    ~Tmnt2MainCpu();
    Tmnt2MainCpu(const Tmnt2MainCpu&) = delete;
    Tmnt2MainCpu& operator=(const Tmnt2MainCpu&) = delete;

    void reset();
    int execute_until(uint64_t cycle_deadline);
    int execute_frame();
    uint32_t pc() const;
    std::string current_instruction() const;
    std::string disassembly_before_pc() const;
    uint32_t data_register(unsigned index) const;
    uint32_t address_register(unsigned index) const;
    uint64_t cycles() const;
    uint64_t aot_instructions() const { return aot_instructions_; }
    uint64_t fallback_instructions() const { return fallback_instructions_; }

private:
    struct RecentInstruction {
        uint32_t pc;
        uint16_t opcode;
        uint32_t sp;
        uint32_t stack_top;
        uint32_t a4;
        uint32_t a5;
        uint32_t a6;
    };
    Tmnt2Board& board_;
    const ng_func_entry_t* entries_ = nullptr;
    size_t entry_count_ = 0;
    uint64_t aot_instructions_ = 0;
    uint64_t fallback_instructions_ = 0;
    std::deque<RecentInstruction> recent_pc_;
};

// Shared Z80 interpreter for the sound program. Its bus stays board-owned;
// this wrapper advances the existing reusable interpreter in cycle budgets.
class Tmnt2SoundCpu {
public:
    explicit Tmnt2SoundCpu(Tmnt2Board& board);
    ~Tmnt2SoundCpu();
    Tmnt2SoundCpu(const Tmnt2SoundCpu&) = delete;
    Tmnt2SoundCpu& operator=(const Tmnt2SoundCpu&) = delete;

    void reset();
    void run(uint64_t cycles);
    void run_until(uint64_t cycle_timestamp);
    uint16_t pc() const;
    uint16_t sp() const;
    uint64_t cycles() const;
    uint64_t nmi_count() const;
    uint64_t aot_instructions() const;
    uint64_t fallback_instructions() const;

private:
    Tmnt2Board& board_;
    std::unique_ptr<mamerecomp::z80::Z80Bus> bus_;
    std::unique_ptr<mamerecomp::z80::Z80> cpu_;
};

} // namespace mamerecomp::konami
