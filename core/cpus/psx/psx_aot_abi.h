// PlayStation-derived R3000A AOT contract.
//
// This is deliberately separate from the project's MIPS III ABI. The PSX
// processor is a 32-bit R3000A derivative with a delayed-load pipeline and
// PSX-specific COP0/GTE behavior. The interpreter and machine bus implement
// the helper functions declared here; generated code only relies on this ABI.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsxCpuState {
    uint32_t r[32];
    uint32_t hi;
    uint32_t lo;
    uint32_t pc;

    // PSX R3000 pipeline state. A pending branch transition is applied after
    // its delay-slot instruction. A memory/COP delayed load commits at the
    // next ordinary advance unless a same-register load supersedes it.
    uint32_t delayed_load_value;
    uint32_t branch_target;
    uint8_t delayed_load_reg;
    uint8_t delayed_load_pending;
    uint8_t branch_pending;
    uint8_t branch_taken;

    uint8_t multiplier_operation;
    uint8_t reserved_pipeline[3];
    uint32_t multiplier_operand1;
    uint32_t multiplier_operand2;

    // COP0 and the integer-visible GTE register files. Devices may extend
    // these through board-owned state, but keep this prefix stable for AOT.
    uint32_t cp0[16];
    uint32_t gte_data[32];
    uint32_t gte_control[32];

    uint64_t cycles;
    uint64_t instructions;
    uint32_t irq_lines;
    uint32_t reserved_status;
    // Absolute CPU cycle of the next timed device event; appended to preserve
    // the existing state prefix used by generated code.
    uint64_t next_event_cycle;
} PsxCpuState;

typedef enum PsxAotExit {
    PSX_AOT_DISPATCH = 0,
    PSX_AOT_INTERPRETER = 1,
    PSX_AOT_EXCEPTION = 2,
    PSX_AOT_HALTED = 3
} PsxAotExit;

typedef enum PsxMulDivOperation {
    PSX_MULDIV_IDLE = 0,
    PSX_MULDIV_SIGNED = 1,
    PSX_MULDIV_UNSIGNED = 2,
    PSX_DIV_SIGNED = 3,
    PSX_DIV_UNSIGNED = 4
} PsxMulDivOperation;

typedef struct PsxBus PsxBus;
typedef PsxAotExit (*PsxAotBlock)(PsxCpuState *state, const PsxBus *bus,
                                  uint64_t cycle_limit, uint8_t entry_offset);

typedef struct PsxAotEntry {
    uint32_t pc;
    PsxAotBlock block;
    uint32_t source_opcode;
    uint8_t source_opcode_valid;
    uint8_t native;
    uint8_t entry_offset;
} PsxAotEntry;

typedef struct PsxAotSourceWord {
    uint32_t pc;
    uint32_t opcode;
} PsxAotSourceWord;

typedef struct PsxBus {
    void *opaque;
    bool (*read)(void *opaque, uint32_t address, unsigned size,
                 uint32_t pc, uint32_t *value);
    bool (*write)(void *opaque, uint32_t address, unsigned size,
                  uint32_t value, uint32_t pc);
    bool (*cop2_command)(void *opaque, PsxCpuState *state,
                         uint32_t pc, uint32_t opcode);
    bool (*cop2_read)(void *opaque, PsxCpuState *state, uint32_t pc,
                      bool control, unsigned reg, uint32_t *value);
    bool (*cop2_write)(void *opaque, PsxCpuState *state, uint32_t pc,
                       bool control, unsigned reg, uint32_t value);
    void (*check_interrupts)(void *opaque, PsxCpuState *state,
                             uint32_t resume_pc);
    // Optional serial pins used by PSX SIO0 peripherals such as ZN CAT702s.
    void (*serial_clock)(void *opaque, int state);
    void (*serial_data)(void *opaque, int state);
    int (*serial_read)(void *opaque);
    int (*serial_dsr)(void *opaque);
    void (*advance_serial)(void *opaque, uint64_t cpu_cycles);
    // True while a serial peripheral has a timed state transition pending.
    // This lets the CPU scheduler avoid polling an idle peripheral per opcode.
    uint64_t (*serial_next_event_cycle)(void *opaque);
    // Cache-isolation accesses bypass the physical board bus in MAME's PSX core.
    bool (*cache_read)(void *opaque, uint32_t address, unsigned size,
                       uint32_t *value);
    bool (*cache_write)(void *opaque, uint32_t address, unsigned size,
                        uint32_t value);
    // Direct program-space view for the hot RAM opcode-fetch path. MAME's
    // address_space returns a direct pointer for mapped RAM/ROM regions.
    const uint8_t *direct_ram;
    const size_t *direct_ram_visible_bytes;
    size_t direct_ram_size;
    size_t direct_ram_mask;
    // Lets board devices advance on the owning R3000A clock before an
    // address-space access reaches their decoder.
    void (*set_cpu_cycles)(void *opaque, uint64_t cycles);
    // Writable view of the mapped main RAM. Normal RAM loads/stores can use
    // this without invoking the device-bus decoder; cache-isolated and MMIO
    // accesses continue to use their dedicated callbacks.
    uint8_t *direct_ram_write;
} PsxBus;

// CPU-runtime helpers shared by AOT and the interpreter. Their definitions
// live with the R3000A execution core, not in generated game modules.
void psx_aot_reset(PsxCpuState *state);
// advance follows MAME advance_pc(). Branch instructions advance first and
// then call schedule_branch(). Pass the encoded branch target and whether it
// was taken so delay-slot exceptions preserve both BD and BT state.
void psx_aot_advance(PsxCpuState *state);
// Generated modules are built without LTO. These equivalent inline forms
// avoid a PLT call for every retired AOT instruction while preserving the
// same branch and delayed-load pipeline transitions as the runtime helpers.
#if defined(__GNUC__)
#define PSX_AOT_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define PSX_AOT_ALWAYS_INLINE inline
#endif
static PSX_AOT_ALWAYS_INLINE void psx_aot_advance_inline(PsxCpuState *state)
{
    if (state->branch_pending) {
        if (state->branch_taken)
            state->pc = state->branch_target;
        else
            state->pc += 4;
        state->branch_pending = 0;
        state->branch_taken = 0;
        return;
    }
    if (state->delayed_load_pending) {
        const unsigned reg = state->delayed_load_reg;
        if (reg != 0 && reg < 32)
            state->r[reg] = state->delayed_load_value;
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }
    state->pc += 4;
}
// The static PSX module executes generated operations in the same shared
// runtime ABI. Keep the simple delayed-load and branch state transitions
// inline there so a guest instruction does not cross the DSO boundary for a
// two- or three-field update. The out-of-line functions remain the canonical
// ABI for the interpreter and other callers.
#if defined(MAMERECOMP_PSX_AOT_INLINE_PIPELINE)
static PSX_AOT_ALWAYS_INLINE void psx_aot_schedule_load_inline(
    PsxCpuState *state, unsigned reg, uint32_t value)
{
    if (state->delayed_load_pending && state->delayed_load_reg == reg) {
        state->delayed_load_reg = 0;
        state->delayed_load_pending = 0;
        state->delayed_load_value = 0;
    }
    psx_aot_advance_inline(state);
    if (reg != 0 && reg < 32) {
        state->delayed_load_reg = (uint8_t)reg;
        state->delayed_load_pending = 1;
        state->delayed_load_value = value;
    }
}

static PSX_AOT_ALWAYS_INLINE void psx_aot_schedule_branch_inline(
    PsxCpuState *state, uint32_t target, bool taken)
{
    state->branch_target = taken ? target : state->pc + 4;
    state->branch_pending = 1;
    state->branch_taken = taken ? 1 : 0;
}
#define psx_aot_schedule_load psx_aot_schedule_load_inline
#define psx_aot_schedule_branch psx_aot_schedule_branch_inline
#endif
// Matches MAME delayed_load(): cancel a pending same-register load, advance,
// then schedule the new result. Generated memory-load ops do not advance first.
void psx_aot_schedule_load(PsxCpuState *state, unsigned reg, uint32_t value);
void psx_aot_schedule_branch(PsxCpuState *state, uint32_t target, bool taken);
void psx_aot_muldiv_start(PsxCpuState *state, PsxMulDivOperation operation,
                          uint32_t lhs, uint32_t rhs);
uint32_t psx_aot_muldiv_result(PsxCpuState *state, bool high);
void psx_aot_write_hilo(PsxCpuState *state, bool high, uint32_t value);
void psx_aot_retire(PsxCpuState *state, uint32_t opcode, uint32_t pc);
static PSX_AOT_ALWAYS_INLINE void psx_aot_retire_inline(PsxCpuState *state,
                                                         uint32_t, uint32_t)
{
    // r0 is initialized/reset to zero and every GPR write path rejects index
    // zero. The dispatcher batches the diagnostic instruction count after a
    // generated block returns, so the hot path only updates the CPU cycle.
    ++state->cycles;
}
#undef PSX_AOT_ALWAYS_INLINE
PsxAotExit psx_aot_interpret_one(PsxCpuState *state, const PsxBus *bus,
                                  uint32_t opcode, uint32_t pc);
PsxAotExit psx_aot_raise_exception(PsxCpuState *state, unsigned exception,
                                    uint32_t pc, uint32_t opcode);
bool psx_aot_memory_read(PsxCpuState *state, const PsxBus *bus,
                         uint32_t address, unsigned size, uint32_t pc,
                         uint32_t opcode, uint32_t *value);
bool psx_aot_memory_write(PsxCpuState *state, const PsxBus *bus,
                          uint32_t address, unsigned size, uint32_t value,
                          uint32_t pc, uint32_t opcode);

// Generated blocks can inline the common RAM side of these helpers. Keeping
// cache-isolated and MMIO accesses on the shared callbacks preserves the
// machine bus contract while avoiding a shared-library helper call for every
// ordinary load/store in a static block.
#if defined(MAMERECOMP_PSX_AOT_INLINE_MEMORY)
#if defined(__GNUC__)
#define PSX_AOT_MEMORY_INLINE inline __attribute__((always_inline))
#else
#define PSX_AOT_MEMORY_INLINE inline
#endif
static PSX_AOT_MEMORY_INLINE bool psx_aot_memory_read_inline(
    PsxCpuState *state, const PsxBus *bus, uint32_t address, unsigned size,
    uint32_t pc, uint32_t opcode, uint32_t *value)
{
    if ((size == 2 && (address & 1u)) || (size == 4 && (address & 3u))) {
        state->cp0[8] = address;
        (void)psx_aot_raise_exception(state, 4, pc, opcode);
        return false;
    }
    const bool cache_isolated = (state->cp0[12] & (1u << 16)) != 0;
    if (!cache_isolated && bus && bus->direct_ram &&
        bus->direct_ram_visible_bytes && bus->direct_ram_size) {
        const uint32_t physical = address & 0x1fffffffu;
        if (physical < *bus->direct_ram_visible_bytes) {
            const size_t offset = bus->direct_ram_mask != SIZE_MAX
                ? (size_t(physical) & bus->direct_ram_mask)
                : size_t(physical) % bus->direct_ram_size;
            if (offset <= bus->direct_ram_size &&
                size <= bus->direct_ram_size - offset) {
                const uint8_t *bytes = bus->direct_ram + offset;
                uint32_t result = bytes[0];
                if (size > 1) result |= uint32_t(bytes[1]) << 8;
                if (size > 2) result |= uint32_t(bytes[2]) << 16;
                if (size > 3) result |= uint32_t(bytes[3]) << 24;
                *value = result;
                return true;
            }
        }
    }
    const bool ok = cache_isolated && bus && bus->cache_read
        ? bus->cache_read(bus->opaque, address, size, value)
        : bus && bus->read && bus->read(bus->opaque, address, size, pc, value);
    if (!ok) (void)psx_aot_raise_exception(state, 7, pc, opcode);
    return ok;
}

static PSX_AOT_MEMORY_INLINE bool psx_aot_memory_write_inline(
    PsxCpuState *state, const PsxBus *bus, uint32_t address, unsigned size,
    uint32_t value, uint32_t pc, uint32_t opcode)
{
    if ((size == 2 && (address & 1u)) || (size == 4 && (address & 3u))) {
        state->cp0[8] = address;
        (void)psx_aot_raise_exception(state, 5, pc, opcode);
        return false;
    }
    const bool cache_isolated = (state->cp0[12] & (1u << 16)) != 0;
    if (!cache_isolated && bus && bus->direct_ram_write &&
        bus->direct_ram_visible_bytes && bus->direct_ram_size) {
        const uint32_t physical = address & 0x1fffffffu;
        if (physical < *bus->direct_ram_visible_bytes) {
            const size_t offset = bus->direct_ram_mask != SIZE_MAX
                ? (size_t(physical) & bus->direct_ram_mask)
                : size_t(physical) % bus->direct_ram_size;
            if (offset <= bus->direct_ram_size &&
                size <= bus->direct_ram_size - offset) {
                for (unsigned i = 0; i < size; ++i)
                    bus->direct_ram_write[offset + i] =
                        static_cast<uint8_t>(value >> (i * 8));
                return true;
            }
        }
    }
    const bool ok = cache_isolated && bus && bus->cache_write
        ? bus->cache_write(bus->opaque, address, size, value)
        : bus && bus->write && bus->write(bus->opaque, address, size, value, pc);
    if (!ok) (void)psx_aot_raise_exception(state, 7, pc, opcode);
    return ok;
}
#undef PSX_AOT_MEMORY_INLINE
#define psx_aot_memory_read psx_aot_memory_read_inline
#define psx_aot_memory_write psx_aot_memory_write_inline
#endif

extern const PsxAotEntry psx_aot_entries[];
extern const size_t psx_aot_entry_count;
extern const PsxAotSourceWord psx_aot_source_words[];
extern const size_t psx_aot_source_word_count;

static inline PsxAotBlock psx_aot_lookup(uint32_t pc)
{
    size_t lo = 0, hi = psx_aot_entry_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (psx_aot_entries[mid].pc < pc) lo = mid + 1;
        else hi = mid;
    }
    return lo < psx_aot_entry_count && psx_aot_entries[lo].pc == pc
        ? psx_aot_entries[lo].block : NULL;
}

#ifdef __cplusplus
}
#endif
