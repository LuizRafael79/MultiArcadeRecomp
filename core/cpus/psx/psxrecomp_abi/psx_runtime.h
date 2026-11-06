// Minimal compile-time ABI for PSXRecomp-generated C.
//
// This deliberately declares only the upstream CPU/runtime contract that the
// generated translation units include. Runtime helper definitions and the
// bridge to PsxCpuState are implemented separately; a successful compile
// against this header does not imply executable integration.
#pragma once

#include <stdint.h>

typedef struct CPUState {
    uint32_t gpr[32];
    uint32_t pc;
    uint32_t hi;
    uint32_t lo;
    uint32_t (*read_word)(uint32_t address);
    void (*write_word)(uint32_t address, uint32_t value);
    uint16_t (*read_half)(uint32_t address);
    void (*write_half)(uint32_t address, uint16_t value);
    uint8_t (*read_byte)(uint32_t address);
    void (*write_byte)(uint32_t address, uint8_t value);
    uint32_t cop0[32];
    uint32_t gte_data[32];
    uint32_t gte_ctrl[32];
    uint32_t current_pc;
    uint64_t instruction_count;
} CPUState;

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t g_debug_last_store_pc;
uint32_t psx_cyc_load_byte(CPUState*, uint32_t, uint32_t, uint32_t);
uint32_t psx_cyc_load_half(CPUState*, uint32_t, uint32_t, uint32_t);
uint32_t psx_cyc_load_word(CPUState*, uint32_t, uint32_t, uint32_t);
uint32_t psx_cyc_lwc2_read(CPUState*, uint32_t);
void psx_store_cycle_barrier(void);
void psx_check_interrupts_at(CPUState*, uint32_t resume_pc);
void debug_server_log_call_entry(uint32_t function_pc);
void psx_restore_state_escape(void);
void psx_rfe_mark_escape(void);
void psx_syscall(CPUState*, uint32_t pc, uint32_t instruction);
void psx_unknown_dispatch(CPUState*, uint32_t pc, uint32_t instruction);
void gte_execute(CPUState*, uint32_t instruction);
uint32_t gte_read_data(CPUState*, uint32_t reg);
void gte_write_data(CPUState*, uint32_t reg, uint32_t value);
uint32_t gte_read_ctrl(CPUState*, uint32_t reg);
void gte_write_ctrl(CPUState*, uint32_t reg, uint32_t value);
void gte_precision_store_word(uint32_t address, uint32_t value, uint32_t reg);
int gte_nclip_exact_sign(uint32_t pc, uint32_t instruction, int32_t mac0);
void psx_recomp_set_pc(CPUState*, uint32_t pc);

#ifdef __cplusplus
}
#endif

#define PGXP_ALU(...) ((void)0)
#define PGXP_LOAD(...) ((void)0)
#define PGXP_STORE(...) ((void)0)
#define PGXP_COP2(...) ((void)0)
