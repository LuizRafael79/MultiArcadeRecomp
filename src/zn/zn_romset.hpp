#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>

#include "psx_aot_abi.h"
#include "cat702.h"
#include "zn_mcu.hpp"
#include "devices/at28c16.hpp"
#include "../../core/chips/c352.hpp"

namespace mamerecomp::zn {

struct RomRegions {
    std::unordered_map<std::string, std::vector<uint8_t>> regions;
    const std::vector<uint8_t> &at(std::string_view name) const;
};

// Loads the MAME-defined ROM regions directly from a ZIP. No extracted ROM
// files are written. Initial profiles cover the two ZN games in this batch.
RomRegions load_romset(std::string_view set, const std::filesystem::path &zip);
// Reads the MAME qsound_hle.zip firmware without extracting it. The returned
// words are the 0x1000-word DL-1425 program/table region used by LLE and HLE.
std::vector<std::uint16_t> load_qsound_firmware(const std::filesystem::path &zip);

// Capcom ZN-2 board address decoder used with the shared PSX memory map,
// including active-low player/system inputs and QSound/Z80 board wiring.
class CapcomZn2Board {
public:
    using SoundCommandCallback = void (*)(void *, uint64_t);
    explicit CapcomZn2Board(const RomRegions &roms);
    PsxBus make_bus();
    uint8_t bank() const { return bank_; }
    void set_input_ports(uint8_t p1, uint8_t p2, uint8_t service,
                         uint8_t system, uint8_t p3, uint8_t p4,
                         uint8_t kick1, uint8_t kick2) {
        input_ports_ = {p1, p2, service, system, p3, p4, kick1, kick2};
    }
    uint8_t sound_latch() const { return sound_latch_; }
    uint8_t sound_bank() const { return sound_bank_; }
    void set_sound_bank(uint8_t bank) { sound_bank_ = bank & 0x0f; }
    bool sound_pending() const { return sound_pending_; }
    void acknowledge_sound() { sound_pending_ = false; }
    void set_sound_command_callback(void *opaque, SoundCommandCallback callback) {
        sound_command_opaque_ = opaque;
        sound_command_callback_ = callback;
    }
    uint8_t sound_program_read(uint16_t address) const;
    void sound_program_write(uint16_t address, uint8_t value);
    uint8_t sound_port_read(uint8_t port) const;
    void sound_port_write(uint8_t port, uint8_t value);
    uint8_t qsound_response() const { return qsound_response_; }
    void set_qsound_response(uint8_t value) { qsound_response_ = value; }
    const std::array<uint8_t, 3> &qsound_registers() const { return qsound_registers_; }
    const std::vector<uint8_t> &qsound_samples() const { return roms_->at("qsound"); }
    bool load_nvram(std::span<const uint8_t> image) { return eeprom_.load(image); }
    std::span<const uint8_t> nvram() const { return eeprom_.image(); }
private:
    static bool read_cb(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool write_cb(void *, uint32_t, unsigned, uint32_t, uint32_t);
    static void serial_clock_cb(void *, int);
    static void serial_data_cb(void *, int);
    static int serial_read_cb(void *);
    static int serial_dsr_cb(void *);
    static void advance_serial_cb(void *, uint64_t);
    static uint64_t serial_next_event_cycle_cb(void *);
    static void cpu_cycles_cb(void *, uint64_t);
    bool read(uint32_t, unsigned, uint32_t *);
    bool write(uint32_t, unsigned, uint32_t);
    const RomRegions *roms_;
    uint8_t bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t sound_bank_ = 0;
    uint8_t qsound_response_ = 0;
    std::vector<uint8_t> sound_ram_ = std::vector<uint8_t>(0x1000);
    std::array<uint8_t, 3> qsound_registers_{};
    std::array<uint8_t, 8> input_ports_{0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff};
    uint8_t coin_ = 0;
    uint16_t spu_hack_ = 0;
    uint64_t cpu_cycles_ = 0;
    uint8_t security_select_ = 0;
    bool sound_pending_ = false;
    void *sound_command_opaque_ = nullptr;
    SoundCommandCallback sound_command_callback_ = nullptr;
    mamerecomp::psx::Cat702 security_a_;
    mamerecomp::psx::Cat702 security_b_;
    // ZnMcu::advance receives the main CPU's cycle counter, so use the
    // ZN-2 R3000A clock (the MCU itself is undumped and timer driven in MAME).
    ZnMcu mcu_{100'000'000};
    mamerecomp::devices::At28c16 eeprom_{100'000'000};
};

class RaizingZn1Board {
public:
    explicit RaizingZn1Board(const RomRegions &roms);
    PsxBus make_bus();
    void set_input_ports(uint8_t p1, uint8_t p2, uint8_t service,
                         uint8_t system, uint8_t p3, uint8_t p4) {
        input_ports_ = {p1, p2, service, system, p3, p4};
    }
    uint8_t bank() const { return bank_; }
    uint8_t sound_latch() const { return sound_latch_; }
    bool load_nvram(std::span<const uint8_t> image) { return eeprom_.load(image); }
    std::span<const uint8_t> nvram() const { return eeprom_.image(); }
    bool sound_irq_pending() const { return sound_irq_pending_; }
    void acknowledge_sound_irq() { sound_irq_pending_ = false; }
private:
    static bool read_cb(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool write_cb(void *, uint32_t, unsigned, uint32_t, uint32_t);
    static void serial_clock_cb(void *, int);
    static void serial_data_cb(void *, int);
    static int serial_read_cb(void *);
    static int serial_dsr_cb(void *);
    static void advance_serial_cb(void *, uint64_t);
    static uint64_t serial_next_event_cycle_cb(void *);
    static void cpu_cycles_cb(void *, uint64_t);
    bool read(uint32_t, unsigned, uint32_t *);
    bool write(uint32_t, unsigned, uint32_t);
    const RomRegions *roms_;
    uint8_t bank_ = 0;
    uint8_t sound_latch_ = 0;
    uint8_t security_select_ = 0;
    uint64_t cpu_cycles_ = 0;
    bool sound_irq_pending_ = false;
    std::array<uint8_t, 6> input_ports_{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    mamerecomp::psx::Cat702 security_a_;
    mamerecomp::psx::Cat702 security_b_;
    ZnMcu mcu_{16'934'400};
    mamerecomp::devices::At28c16 eeprom_{67'737'600};
};

// Namco System 12 COH700 main-board mappings from MAME's namcos12.cpp.
// The PSX SoC is shared with ZN, while ROM banking, shared RAM and DMA ROM
// source selection remain board-specific.
class NamcoSystem12Board {
public:
    explicit NamcoSystem12Board(const RomRegions &roms);
    PsxBus make_bus();
    std::vector<uint32_t> dma_rom_read(size_t words);
    bool sub_read(uint32_t address, unsigned size, uint32_t *value);
    bool sub_write(uint32_t address, unsigned size, uint32_t value);
    uint16_t c352_read(uint16_t word_offset) const { return c352_.read(word_offset); }
    void c352_write(uint16_t word_offset, uint16_t data, uint16_t mem_mask = 0xffff) {
        c352_.write(word_offset, data, mem_mask);
    }
    std::vector<int16_t> render_audio(size_t frames) { return c352_.render(frames); }
    uint32_t audio_sample_rate() const { return c352_.sample_rate(); }
    unsigned active_audio_voices() const { return c352_.active_voices(); }
    uint64_t c352_writes() const { return c352_writes_; }
    uint16_t bank() const { return bank_; }
private:
    uint8_t h8_read8(uint32_t address);
    void h8_write8(uint32_t address, uint8_t value);
    uint8_t h8_port_read(unsigned port) const;
    uint8_t h8_port_input(unsigned port) const;
    static bool read_cb(void *, uint32_t, unsigned, uint32_t, uint32_t *);
    static bool write_cb(void *, uint32_t, unsigned, uint32_t, uint32_t);
    bool read(uint32_t, unsigned, uint32_t *);
    bool write(uint32_t, unsigned, uint32_t);
    const RomRegions *roms_;
    mamerecomp::chips::C352 c352_;
    uint16_t bank_ = 0;
    uint32_t dma_offset_ = 0;
    std::vector<uint8_t> shared_ram_ = std::vector<uint8_t>(0x10000);
    std::array<uint8_t, 0x200> h8_internal_ram_{};
    std::array<uint8_t, 16> h8_port_ddr_ = [] {
        std::array<uint8_t, 16> value{};
        value[6] = 0x80; // H8/3002 port 6 reset DDR from MAME configuration.
        value[8] = 0xf0; // Port 8 reset DDR from MAME configuration.
        return value;
    }();
    std::array<uint8_t, 16> h8_port_dr_{};
    uint8_t h8_port8_latch_ = 0;
    uint8_t h8_porta_latch_ = 0;
    uint8_t h8_portb_latch_ = 0x50;
    uint8_t h8_port4_pcr_ = 0;
    uint8_t h8_syscr_ = 0x09;
    uint64_t c352_writes_ = 0;
};

} // namespace mamerecomp::zn
