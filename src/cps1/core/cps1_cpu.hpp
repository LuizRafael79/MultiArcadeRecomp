#pragma once

#include <cstdint>
#include <cstddef>
#include <iosfwd>
#include <string>

extern "C" {
#include <neogeorecomp/neogeo.h>
}

namespace mamerecomp::cps1::detail {

struct Cps1CpuStats {
    uint64_t native_blocks = 0;
    uint64_t native_instructions = 0;
    uint64_t interpreted_instructions = 0;
    size_t uncovered_entries = 0;
};

void initialize_m68k(const ng_func_entry_t* entries, size_t count,
    const std::uint8_t* gfxram_snapshot, std::size_t snapshot_size);
void dispatch_m68k_instruction(bool force_trap_fallback, bool force_all_fallback,
    int64_t next_event_cycle);
Cps1CpuStats m68k_stats();
void configure_m68k_diagnostics(bool profile_pc, bool trace_traps);
void write_m68k_profile(std::ostream& out);
bool save_m68k_entries(const std::string& path);
bool save_m68k_ram_artifacts(const std::string& entries_path, const std::string& snapshot_path);

} // namespace mamerecomp::cps1::detail
