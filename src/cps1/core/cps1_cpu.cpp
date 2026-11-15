#include "cps1_cpu.hpp"
#include "cps1_bus.hpp"
#include "cps1_state.hpp"
#include "cps1_gfxram.hpp"

#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>
#include "m68k.h"
#include "native_registry.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
uint32_t g_current_guest_pc = 0;
}

namespace multiarcaderecomp::cps1::detail {
namespace {
constexpr uint32_t kAddressMask = 0x00ffffff;
struct NativeEntry { ng_func_t fn; std::uint16_t instruction_size; };
std::unordered_map<uint32_t, NativeEntry> g_native;
const std::uint8_t* g_gfxram_snapshot = nullptr;
std::size_t g_gfxram_snapshot_size = 0;
uint64_t g_fallback_instructions = 0;
uint64_t g_native_blocks = 0;
uint32_t g_last_fallback_pc = 0;
std::unordered_set<uint32_t> g_dynamic_entries;
std::unordered_set<uint32_t> g_executable_ram_entries;
bool g_in_fallback_sequence = false;
bool g_in_ram_sequence = false;
uint32_t g_expected_ram_pc = 0;
std::array<uint64_t, 0x8000> g_pc_profile{};
bool g_profile_pc = false;
bool g_trace_trap = false;
uint64_t g_trap_count = 0;

void profile_guest_pc(uint32_t pc) {
    g_current_guest_pc = pc;
    if (g_profile_pc && pc < 0x10000)
        ++g_pc_profile[pc >> 1];
    if (g_trace_trap && pc == 0x0005c4u) {
        std::cerr << "[cps1] TRAP14 n=" << ++g_trap_count << " D0-D7=" << std::hex;
        for (unsigned i = 0; i < 8; ++i) std::cerr << (i ? "," : "") << ng_cpu.d[i];
        std::cerr << " A0-A7=";
        for (unsigned i = 0; i < 8; ++i) std::cerr << (i ? "," : "") << ng_cpu.a[i];
        std::cerr << std::dec << '\n';
    }
    if (g_trace_trap && (pc == 0x000aa6u || pc == 0x000ab2u || pc == 0x0000c0u || pc == 0x000232u)) {
        const uint32_t sp = ng_cpu.a[7];
        std::cerr << "[cps1] boot-flow PC=$" << std::hex << pc << " SR=$" << ng_get_sr()
                  << " SP=$" << sp << " stack=$" << ng_r16(sp) << ",$" << ng_r32(sp + 2)
                  << " D0=$" << ng_cpu.d[0] << " D2=$" << ng_cpu.d[2]
                  << " D3=$" << ng_cpu.d[3] << " D4=$" << ng_cpu.d[4]
                  << " A4=$" << ng_cpu.a[4] << " A5=$" << ng_cpu.a[5]
                  << std::dec << '\n';
    }
}


void copy_to_musashi() {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; ++i) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0 + i), ng_cpu.d[i]);
    for (int i = 0; i < 7; ++i) m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_A0 + i), ng_cpu.a[i]);
    m68k_set_reg(M68K_REG_A7, ng_cpu.a[7]);
    m68k_set_reg(ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP, ng_cpu.osp);
    m68k_set_reg(M68K_REG_PC, ng_cpu.pc);
}

void copy_from_musashi() {
    const uint16_t sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    ng_cpu.s = (sr >> 13) & 1;
    ng_cpu.t = (sr >> 15) & 1;
    ng_cpu.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; ++i) ng_cpu.d[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
    for (int i = 0; i < 8; ++i) ng_cpu.a[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
    ng_cpu.osp = m68k_get_reg(nullptr, ng_cpu.s ? M68K_REG_USP : M68K_REG_ISP);
    ng_cpu.pc = m68k_get_reg(nullptr, M68K_REG_PC) & kAddressMask;
}

void run_fallback() {
    g_last_fallback_pc = ng_cpu.pc & kAddressMask;
    if (g_trace_trap && (g_last_fallback_pc == 0x0005c4u || g_last_fallback_pc == 0x000aa6u || g_last_fallback_pc == 0x0000c0u)) {
        std::cerr << "[cps1] fallback PC=$" << std::hex << g_last_fallback_pc
                  << " SR=$" << ng_get_sr() << " SP=$" << ng_cpu.a[7]
                  << " D0=$" << ng_cpu.d[0] << " D2=$" << ng_cpu.d[2]
                  << " D3=$" << ng_cpu.d[3] << " D4=$" << ng_cpu.d[4]
                  << " A4=$" << ng_cpu.a[4] << " A5=$" << ng_cpu.a[5]
                  << std::dec << '\n';
    }
    if (!g_in_fallback_sequence && g_last_fallback_pc < g_program_size) {
        const auto [_, inserted] = g_dynamic_entries.insert(g_last_fallback_pc);
        if (inserted && g_dynamic_entries.size() <= 32)
            std::cerr << "[cps1] uncovered ROM entry PC=$" << std::hex << g_last_fallback_pc << std::dec << '\n';
    }
    char disassembly[128]{};
    const unsigned length = std::max(2U, m68k_disassemble(disassembly, g_last_fallback_pc, M68K_CPU_TYPE_68000));
    copy_to_musashi();
    const int consumed = m68k_execute(1);
    copy_from_musashi();
    ng_cycles += std::max(consumed, 1);
    ++g_fallback_instructions;
    const uint32_t next_pc = ng_cpu.pc & kAddressMask;
    g_in_fallback_sequence = next_pc == ((g_last_fallback_pc + length) & kAddressMask)
        && g_native.find(next_pc) == g_native.end();
    g_in_ram_sequence = next_pc >= 0x900000 && next_pc <= 0x92ffff
        && next_pc == ((g_last_fallback_pc + length) & kAddressMask);
    g_expected_ram_pc = next_pc;
}

}

void initialize_m68k(const ng_func_entry_t* entries, size_t count,
    const std::uint8_t* gfxram_snapshot, std::size_t snapshot_size) {
    g_native.clear();
    g_gfxram_snapshot = gfxram_snapshot;
    g_gfxram_snapshot_size = snapshot_size;
    for (size_t i = 0; entries != nullptr && i < count; ++i)
        g_native.emplace(entries[i].addr, NativeEntry{entries[i].fn, entries[i].instruction_size});
    ng_cpu.a[7] = ng_r32(0);
    ng_cpu.osp = ng_cpu.a[7];
    ng_cpu.pc = ng_r32(4) & kAddressMask;
    ng_cpu.s = 1;
    ng_set_sr(0x2700);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

void dispatch_m68k_instruction(bool force_trap_fallback, bool force_all_fallback,
    int64_t next_event_cycle) {
    const uint32_t pc = ng_cpu.pc & kAddressMask;
    const bool executable_gfxram = g_gfxram_is_executable && pc >= 0x900000 && pc <= 0x92ffff;
    if (executable_gfxram) {
        if (!g_in_ram_sequence || pc != g_expected_ram_pc)
            g_executable_ram_entries.insert(pc);
    } else {
        g_in_ram_sequence = false;
    }
    if (force_all_fallback || (force_trap_fallback
        && (ng_cpu.pc == 0x0005c4u || (ng_cpu.pc >= 0x000aa6u && ng_cpu.pc <= 0x000ab2u)))) {
        run_fallback();
        return;
    }
    const auto found = g_native.find(ng_cpu.pc & 0x00fffffe);
    if (found == g_native.end()) {
        run_fallback();
        return;
    }
    if (executable_gfxram) {
        const std::size_t offset = pc - 0x900000;
        const std::size_t length = found->second.instruction_size;
        if (!g_gfxram_snapshot || g_gfxram_snapshot_size != 0x30000 || length < 2
            || offset + length > g_gfxram_snapshot_size
            || !std::equal(g_gfxram.begin() + offset, g_gfxram.begin() + offset + length,
                g_gfxram_snapshot + offset)) {
            run_fallback();
            return;
        }
    }
    g_in_fallback_sequence = false;
    ng_next_event = next_event_cycle;
    ++g_native_blocks;
    found->second.fn();
    const uint32_t next_pc = ng_cpu.pc & kAddressMask;
    g_in_ram_sequence = executable_gfxram && next_pc >= 0x900000 && next_pc <= 0x92ffff
        && found->second.instruction_size >= 2
        && next_pc == ((pc + found->second.instruction_size) & kAddressMask);
    g_expected_ram_pc = next_pc;
}

Cps1CpuStats m68k_stats() {
    return {g_native_blocks, ng_icount, g_fallback_instructions, g_dynamic_entries.size()};
}

void configure_m68k_diagnostics(bool profile_pc, bool trace_traps) {
    g_profile_pc = profile_pc;
    g_trace_trap = trace_traps;
    if (g_profile_pc || g_trace_trap) ng_debug_instruction_hook = profile_guest_pc;
}

void write_m68k_profile(std::ostream& out) {
    if (!g_profile_pc) return;
    std::vector<std::pair<uint64_t, uint32_t>> hot;
    uint64_t profiled = 0;
    for (uint32_t i = 0; i < g_pc_profile.size(); ++i)
        if (g_pc_profile[i]) { hot.emplace_back(g_pc_profile[i], i * 2); profiled += g_pc_profile[i]; }
    std::sort(hot.begin(), hot.end(), std::greater<>());
    out << "[cps1] PC profile total=" << profiled << " fail-loop $71c=" << g_pc_profile[0x71c >> 1]
        << " RAM test $6f8=" << g_pc_profile[0x6f8 >> 1] << " reset $c0=" << g_pc_profile[0xc0 >> 1]
        << " trap14 $5c4=" << g_pc_profile[0x5c4 >> 1] << " trap-handler $aa6=" << g_pc_profile[0xaa6 >> 1]
        << " hottest native PCs:";
    for (size_t i = 0; i < std::min<size_t>(hot.size(), 16); ++i)
        out << " $" << std::hex << hot[i].second << '=' << std::dec << hot[i].first;
    out << '\n';
}

bool save_m68k_entries(const std::string& path) {
    std::vector<uint32_t> entries(g_dynamic_entries.begin(), g_dynamic_entries.end());
    std::sort(entries.begin(), entries.end());
    std::ofstream out(path);
    for (uint32_t pc : entries)
        out << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << pc << '\n';
    return static_cast<bool>(out);
}

bool save_m68k_ram_artifacts(const std::string& entries_path, const std::string& snapshot_path) {
    std::vector<uint32_t> entries(g_executable_ram_entries.begin(), g_executable_ram_entries.end());
    std::sort(entries.begin(), entries.end());
    std::ofstream entry_file(entries_path);
    for (uint32_t pc : entries)
        entry_file << std::hex << std::uppercase << std::setw(6) << std::setfill('0') << pc << '\n';
    if (!entry_file) return false;
    std::ofstream snapshot(snapshot_path, std::ios::binary);
    snapshot.write(reinterpret_cast<const char*>(g_gfxram.data()), 0x30000);
    if (!snapshot) return false;
    std::cerr << "[cps1] executable gfx RAM: " << entries.size()
              << " observed entry PCs; captured mapped bytes [0x900000,0x930000) to "
              << snapshot_path << " (RAM AOT entries are guarded by live-byte checks)\n";
    return true;
}

} // namespace multiarcaderecomp::cps1::detail

using namespace multiarcaderecomp::cps1::detail;
extern "C" {
void ng_bad_dispatch(uint32_t pc) {
    std::cerr << "[cps1] generated routine received mismatched PC=$" << std::hex << pc << std::dec << '\n';
    std::abort();
}
unsigned int m68k_read_memory_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return ng_r32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { ng_w8(a, static_cast<uint8_t>(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { ng_w16(a, static_cast<uint16_t>(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { ng_w32(a, v); }
unsigned int m68k_read_immediate_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_immediate_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_pcrelative_8(unsigned int a) { return ng_r8(a); }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return ng_r32(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return ng_r16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return ng_r32(a); }
}
