#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace multiarcaderecomp::z80 { struct Z80Code; }

namespace multiarcaderecomp::cps1::detail {

bool initialize_sound(std::vector<uint8_t> fixed_rom, std::vector<uint8_t> banked_rom,
    std::vector<uint8_t> oki_samples, const multiarcaderecomp::z80::Z80Code* code, bool audio_output);
void sync_sound_to_main_clock();
void write_sound_command(uint8_t value);
void write_sound_timer_command(uint8_t value);
uint8_t sound_command();
uint64_t sound_ym_register_count();
void print_sound_summary(std::ostream& out);
bool save_sound_artifacts(const std::string& entry_path, const std::string& fallback_path);
void shutdown_sound();

} // namespace multiarcaderecomp::cps1::detail
