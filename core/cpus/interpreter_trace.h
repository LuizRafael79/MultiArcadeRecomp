#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MultiArcadeRecompFallbackReason {
    MULTIARCADERECOMP_UNSUPPORTED_OPCODE = 0,
    MULTIARCADERECOMP_UNSUPPORTED_VARIANT,
    MULTIARCADERECOMP_UNCOVERED_ENTRY,
    MULTIARCADERECOMP_DYNAMIC_CODE,
    MULTIARCADERECOMP_MEMORY_ACCESS,
    MULTIARCADERECOMP_EXCEPTION_BOUNDARY,
    MULTIARCADERECOMP_NATIVE_DIVERGENCE,
    MULTIARCADERECOMP_FALLBACK_REASON_COUNT
} MultiArcadeRecompFallbackReason;

const char *multiarcaderecomp_fallback_reason_name(MultiArcadeRecompFallbackReason reason);

/* Report one instruction executed by an interpreter/fallback backend.
 * `reason` is the precise reason control entered the interpreter. The CPU,
 * backend and instruction names should point to static strings.
 * Unique sites are counted in memory and summarized at process exit; detailed
 * per-step file output is optional. */
void multiarcaderecomp_interpreter_trace_step(const char *cpu, const char *backend,
    MultiArcadeRecompFallbackReason reason, uint32_t pc, uint64_t opcode, unsigned opcode_bits,
    uint64_t cycles_before, uint64_t cycles_after, uint32_t next_pc,
    const char *instruction);

/* Record a dispatch event with no instruction execution, such as an interrupt
 * or exception boundary. */
void multiarcaderecomp_interpreter_trace_event(const char *cpu, const char *backend,
    MultiArcadeRecompFallbackReason reason, uint32_t pc, uint32_t next_pc,
    uint64_t cycles_before, uint64_t cycles_after);

/* Optional detailed per-instruction trace path; set to NULL to disable. */
void multiarcaderecomp_interpreter_trace_set_step_log(const char *path);

/* Optional summary path; profiling is required for any summary to be produced. */
void multiarcaderecomp_interpreter_trace_set_summary_log(const char *path);

/* Enable/disable per-instruction categorized profiling. Disabled by default. */
void multiarcaderecomp_interpreter_trace_set_profile_enabled(int enabled);

/* True when a detailed trace file is active. Backends can avoid formatting a
 * disassembly string on the normal fast path. */
int multiarcaderecomp_interpreter_trace_steps_enabled(void);

/* Flush the categorized profile. Registered automatically with atexit. */
void multiarcaderecomp_interpreter_trace_flush(void);

#ifdef __cplusplus
}
#endif
