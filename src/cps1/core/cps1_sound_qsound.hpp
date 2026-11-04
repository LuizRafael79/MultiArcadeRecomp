#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace mamerecomp::z80 { struct Z80Code; }

namespace mamerecomp::cps1::detail {

// QSound has a separate Z80/shared-RAM/device runtime from the YM2151/OKI
// sound board. The QSound HLE/device path is experimental and game parity is
// still being validated.
bool initialize_qsound(std::vector<std::uint8_t> program_rom,
                       std::vector<std::uint8_t> sample_rom,
                       const mamerecomp::z80::Z80Code* code, bool audio_output);
bool qsound_is_active();
void qsound_sync_to_main_clock();
void qsound_write_command(std::uint8_t value);
std::uint16_t qsound_main_read(std::uint32_t address);
bool qsound_main_write(std::uint32_t address, std::uint16_t value);
void qsound_print_summary(std::ostream& out);
bool qsound_save_artifacts(const std::string& entry_path, const std::string& fallback_path);
void qsound_shutdown();

} // namespace mamerecomp::cps1::detail
