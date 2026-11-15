/* analyze.h - recursive-descent function/block discovery over a Z80 image.
 *
 * Given a program's raw Z80 machine code and the address it loads at, walk it
 * the way the CPU would: start at the entry seeds, decode forward, and follow
 * every CALL/JP/JR/DJNZ/RST whose target lands inside the image, recording the
 * set of block leaders (addresses that begin a basic block). The emitter then
 * turns each block into a `pac_func_<addr>`. Targets outside the image (TI-OS
 * bcalls, RAM addresses) are noted but not descended into. */
#ifndef Z80_ANALYZE_H
#define Z80_ANALYZE_H

#include <stdint.h>

typedef struct {
    uint16_t       base;      /* load address of code[0]                       */
    const uint8_t *code;
    int            len;
    uint16_t      *blocks;    /* sorted, unique block-leader addresses         */
    int            nblocks;
    int            cap;
    uint8_t       *seen;      /* len bytes: 1 = decoded as an instruction start */
    uint8_t       *smc;       /* len bytes: 1 = byte is written by the code at a
                               * constant address (self-modified) - the emitter
                               * reads such operands live instead of baking them */
    int            ninsns;    /* instructions decoded                          */
    int            next_ext;  /* count of branch/call targets outside the image */
} z80_prog_t;

/* Discover blocks reachable from `seeds` (e.g. the load address). Returns 0 on
 * success. Caller frees with z80_prog_free. */
int  z80_analyze(const uint8_t *code, int len, uint16_t base,
                 const uint16_t *seeds, int nseeds, z80_prog_t *out);
void z80_prog_free(z80_prog_t *p);

#endif /* Z80_ANALYZE_H */
