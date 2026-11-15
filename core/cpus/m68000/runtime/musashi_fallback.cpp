#include "musashi_fallback.h"

#include "recomp_bus.h"

#include <m68k.h>
#include <algorithm>
#include <cstring>
extern "C" {
#include <musashi_glue.h>
void m68k_set_cmpild_instr_callback(void (*callback)(unsigned int, int));
void m68k_set_rte_instr_callback(void (*callback)(void));
}
#include <neogeorecomp/bus.h>
#include <neogeorecomp/cpu.h>

#include <cstdint>

namespace {

bool s_initialized = false;
MultiArcadeRecompM68kOpcodeRead16Fn s_opcode_reader = nullptr;
void* s_opcode_reader_context = nullptr;
MultiArcadeRecompM68kCmpildFn s_fd1094_cmpild = nullptr;
MultiArcadeRecompM68kRteFn s_fd1094_rte = nullptr;
MultiArcadeRecompM68kIrqAckFn s_fd1094_irq_ack = nullptr;
void* s_fd1094_context = nullptr;
const uint32_t* s_aot_entry_pcs = nullptr;
size_t s_aot_entry_count = 0;
const bool* s_aot_state_valid = nullptr;
int64_t s_fallback_start_cycles = 0;
uint64_t s_fallback_instruction_count = 0;
bool s_batch_mode = false;

uint16_t opcode_read16(unsigned int address) {
    return s_opcode_reader
        ? s_opcode_reader(s_opcode_reader_context, address & 0x00ffffffu)
        : static_cast<uint16_t>(ng_r16(address));
}

void fd1094_cmpild(unsigned int value, int reg) {
    if (s_fd1094_cmpild) s_fd1094_cmpild(s_fd1094_context, value, static_cast<unsigned>(reg));
}
void fd1094_rte() {
    if (s_fd1094_rte) s_fd1094_rte(s_fd1094_context);
}
int fd1094_irq_ack(int level) {
    return s_fd1094_irq_ack
        ? s_fd1094_irq_ack(s_fd1094_context, level)
        : M68K_INT_ACK_AUTOVECTOR;
}

void copy_to_musashi() {
    m68k_set_reg(M68K_REG_SR, ng_get_sr());
    for (int i = 0; i < 8; ++i)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0 + i), CPU.d[i]);
    for (int i = 0; i < 8; ++i)
        m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_A0 + i), CPU.a[i]);
    m68k_set_reg(CPU.s ? M68K_REG_USP : M68K_REG_ISP, CPU.osp);
    m68k_set_reg(M68K_REG_PC, CPU.pc);
}

void copy_from_musashi() {
    const uint16_t sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    CPU.s = (sr >> 13) & 1;
    CPU.t = (sr >> 15) & 1;
    CPU.ipl = (sr >> 8) & 7;
    ng_set_ccr(sr);
    for (int i = 0; i < 8; ++i)
        CPU.d[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
    for (int i = 0; i < 8; ++i)
        CPU.a[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
    CPU.osp = m68k_get_reg(nullptr, CPU.s ? M68K_REG_USP : M68K_REG_ISP);
    CPU.pc = m68k_get_reg(nullptr, M68K_REG_PC) & 0x00ffffffu;
}

bool is_aot_entry(uint32_t pc) {
    size_t low = 0;
    size_t high = s_aot_entry_count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        if (s_aot_entry_pcs[mid] < pc) low = mid + 1;
        else high = mid;
    }
    return low < s_aot_entry_count && s_aot_entry_pcs[low] == pc;
}

void fallback_instruction_hook(unsigned int pc) {
    if (!s_batch_mode) return;
    ng_cycles = s_fallback_start_cycles + m68k_cycles_run();
    const bool invalidated = s_aot_state_valid && !*s_aot_state_valid;
    const bool event_due = ng_cycles >= ng_next_event;
    const bool bus_faulted = multiarcaderecomp_m68k_bus_fault().access != M68K_BUS_ACCESS_NONE;
    if (invalidated || event_due || bus_faulted || is_aot_entry(pc)) {
        m68k_end_timeslice();
        return;
    }
    ++s_fallback_instruction_count;
}

void initialize_musashi() {
    if (s_initialized) return;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_cmpild_instr_callback(fd1094_cmpild);
    m68k_set_rte_instr_callback(fd1094_rte);
    m68k_set_int_ack_callback(fd1094_irq_ack);
    m68k_set_instr_hook_callback(fallback_instruction_hook);
    s_initialized = true;
}

} // namespace

extern "C" {

unsigned int m68k_read_memory_8(unsigned int address) { return ng_r8(address); }
unsigned int m68k_read_memory_16(unsigned int address) { return ng_r16(address); }
unsigned int m68k_read_memory_32(unsigned int address) { return ng_r32(address); }
unsigned int m68k_read_immediate_16(unsigned int address) { return opcode_read16(address); }
unsigned int m68k_read_immediate_32(unsigned int address) {
    return (static_cast<uint32_t>(opcode_read16(address)) << 16) | opcode_read16(address + 2);
}
unsigned int m68k_read_pcrelative_8(unsigned int address) {
    const uint16_t word = opcode_read16(address & ~1u);
    return (address & 1u) ? (word & 0xffu) : (word >> 8);
}
unsigned int m68k_read_pcrelative_16(unsigned int address) { return opcode_read16(address); }
unsigned int m68k_read_pcrelative_32(unsigned int address) {
    return (static_cast<uint32_t>(opcode_read16(address)) << 16) | opcode_read16(address + 2);
}
unsigned int m68k_read_disassembler_8(unsigned int address) { return ng_r8(address); }
unsigned int m68k_read_disassembler_16(unsigned int address) { return opcode_read16(address); }
unsigned int m68k_read_disassembler_32(unsigned int address) {
    return (static_cast<uint32_t>(opcode_read16(address)) << 16) | opcode_read16(address + 2);
}
void m68k_write_memory_8(unsigned int address, unsigned int value) {
    ng_w8(address, static_cast<uint8_t>(value));
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
    ng_w16(address, static_cast<uint16_t>(value));
}
void m68k_write_memory_32(unsigned int address, unsigned int value) { ng_w32(address, value); }
void m68k_write_memory_32_pd(unsigned int address, unsigned int value) {
    ng_w16(address + 2, static_cast<uint16_t>(value));
    ng_w16(address, static_cast<uint16_t>(value >> 16));
}

__attribute__((visibility("default"))) void multiarcaderecomp_m68k_musashi_set_opcode_reader(
    MultiArcadeRecompM68kOpcodeRead16Fn reader, void* context) {
    s_opcode_reader = reader;
    s_opcode_reader_context = context;
}

__attribute__((visibility("default"))) void multiarcaderecomp_m68k_musashi_set_extension_callbacks(
    MultiArcadeRecompM68kCmpildFn cmpild, MultiArcadeRecompM68kRteFn rte,
    MultiArcadeRecompM68kIrqAckFn irq_ack, void* context) {
    s_fd1094_cmpild = cmpild;
    s_fd1094_rte = rte;
    s_fd1094_irq_ack = irq_ack;
    s_fd1094_context = context;
}

__attribute__((visibility("default"))) int multiarcaderecomp_m68k_musashi_step(void) {
    initialize_musashi();
    if (!CPU.stopped) mus_clear_stopped();
    copy_to_musashi();
    const int consumed_cycles = m68k_execute(1);
    copy_from_musashi();
    CPU.stopped = mus_stopped() != 0;
    return consumed_cycles;
}

__attribute__((visibility("default"))) int multiarcaderecomp_m68k_musashi_run_until(
    const uint32_t* aot_entry_pcs, size_t aot_entry_count,
    const bool* aot_state_valid, uint64_t* instruction_count) {
    initialize_musashi();
    s_aot_entry_pcs = aot_entry_pcs;
    s_aot_entry_count = aot_entry_pcs ? aot_entry_count : 0;
    s_aot_state_valid = aot_state_valid;
    s_fallback_start_cycles = ng_cycles;
    s_fallback_instruction_count = 0;
    multiarcaderecomp_m68k_bus_clear_fault();
    if (!CPU.stopped) mus_clear_stopped();
    copy_to_musashi();
    const int64_t cycles_to_event = std::max<int64_t>(1, ng_next_event - ng_cycles);
    const int budget = static_cast<int>(std::min<int64_t>(cycles_to_event, INT32_MAX));
    s_batch_mode = true;
    const int consumed_cycles = m68k_execute(budget);
    s_batch_mode = false;
    copy_from_musashi();
    CPU.stopped = mus_stopped() != 0;
    if (instruction_count) *instruction_count = s_fallback_instruction_count;
    s_aot_entry_pcs = nullptr;
    s_aot_entry_count = 0;
    s_aot_state_valid = nullptr;
    return consumed_cycles;
}

__attribute__((visibility("default"))) void multiarcaderecomp_m68k_musashi_describe(
    uint32_t pc, uint16_t* opcode, char* disassembly, size_t capacity) {
    if (opcode) *opcode = static_cast<uint16_t>(ng_r16(pc));
    if (!disassembly || capacity == 0) return;
    disassembly[0] = '\0';
    char text[128]{};
    m68k_disassemble(text, pc, M68K_CPU_TYPE_68000);
    std::strncpy(disassembly, text, capacity - 1);
    disassembly[capacity - 1] = '\0';
}

} // extern "C"
