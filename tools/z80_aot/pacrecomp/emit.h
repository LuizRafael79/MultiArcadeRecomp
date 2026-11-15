/* emit.h - C emission for the Z80 -> pacrecomp static recompiler.
 *
 * z80_emit() turns the discovered basic blocks of a TI-83/84+ Z80 program into
 * native C: one `static void pac_func_<addr>(void)` per block leader, plus a
 * registration function that wires every leader into the dispatch table. The
 * generated code runs on the pacrecomp runtime (recomp_rt.h / cpu.h / mem.h /
 * ports.h): each instruction is lowered to direct CPU-state manipulation and
 * pac_* ALU helpers, and every inter-block transfer sets pac_cpu.pc and returns
 * to the host driver, so Z80 control flow never recurses on the C stack.
 */
#ifndef Z80_EMIT_H
#define Z80_EMIT_H

#include <stdint.h>

/* Emit recomp_funcs.c + recomp_funcs.h into outdir. `blocks` is the sorted list
 * of basic-block leader addresses (from z80_analyze). `base` is the load address
 * of code[0]. `regname` is the game name used for the registration function
 * (e.g. "blockdude" -> void blockdude_register_funcs(void)). Returns the number
 * of functions emitted, or negative on error. */
/* `smc` (optional, may be NULL) is a per-byte map from z80_analyze marking
 * self-modified bytes; operands overlapping them are emitted as live memory
 * reads so runtime patches take effect. */
int z80_emit(const uint8_t *code, int len, uint16_t base,
             const uint16_t *blocks, int nblocks,
             const char *outdir, const char *regname,
             const uint8_t *smc);

#endif /* Z80_EMIT_H */
