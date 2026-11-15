/* analyze.c - recursive-descent block discovery. See analyze.h. */
#include "analyze.h"
#include "decode.h"

#include <stdlib.h>
#include <string.h>

static int in_range(const z80_prog_t *p, uint16_t a)
{
    return a >= p->base && (uint32_t)(a - p->base) < (uint32_t)p->len;
}

static void add_block(z80_prog_t *p, uint16_t a)
{
    for (int i = 0; i < p->nblocks; i++)
        if (p->blocks[i] == a) return;
    if (p->nblocks == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->blocks = (uint16_t *)realloc(p->blocks, (size_t)p->cap * sizeof(uint16_t));
    }
    p->blocks[p->nblocks++] = a;
}

/* A tiny address worklist used as the descent frontier. */
typedef struct { uint16_t *a; int n, cap; } worklist_t;

static void wl_push(worklist_t *w, uint16_t a)
{
    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 64;
        w->a = (uint16_t *)realloc(w->a, (size_t)w->cap * sizeof(uint16_t));
    }
    w->a[w->n++] = a;
}

static int cmp_u16(const void *x, const void *y)
{
    uint16_t a = *(const uint16_t *)x, b = *(const uint16_t *)y;
    return (a > b) - (a < b);
}

int z80_analyze(const uint8_t *code, int len, uint16_t base,
                const uint16_t *seeds, int nseeds, z80_prog_t *out)
{
    memset(out, 0, sizeof *out);
    out->base = base;
    out->code = code;
    out->len  = len;
    out->seen = (uint8_t *)calloc((size_t)len, 1);
    out->smc  = (uint8_t *)calloc((size_t)len, 1);
    if (!out->seen || !out->smc) return -1;

    worklist_t wl = {0};
    for (int i = 0; i < nseeds; i++)
        if (in_range(out, seeds[i])) { wl_push(&wl, seeds[i]); add_block(out, seeds[i]); }

    while (wl.n > 0) {
        uint16_t pc = wl.a[--wl.n];

        /* Walk a straight-line run from pc until a block terminator. */
        for (;;) {
            if (!in_range(out, pc)) break;
            int off = pc - base;
            if (out->seen[off]) break;          /* already decoded from here */

            z80_insn_t in;
            int n = z80_decode(code + off, len - off, pc, &in);
            if (n <= 0) break;
            out->seen[off] = 1;
            out->ninsns++;

            /* Self-modifying-code detection: a store to a constant address that
             * lands inside the image patches those bytes at runtime. Mark them
             * so the emitter reads any operand overlapping them live. */
            {
                uint16_t waddr = 0; int wlen = 0;
                if (in.prefix == ZP_NONE) {
                    if (in.opcode == 0x32) { waddr = in.imm; wlen = 1; }      /* LD (nn),A  */
                    else if (in.opcode == 0x22) { waddr = in.imm; wlen = 2; } /* LD (nn),HL */
                } else if (in.prefix == ZP_ED &&
                           (in.opcode == 0x43 || in.opcode == 0x53 ||
                            in.opcode == 0x63 || in.opcode == 0x73)) {
                    waddr = in.imm; wlen = 2;                                 /* LD (nn),rr */
                } else if ((in.prefix == ZP_DD || in.prefix == ZP_FD) && in.opcode == 0x22) {
                    waddr = in.imm; wlen = 2;                                 /* LD (nn),IX/IY */
                }
                for (int k = 0; k < wlen; k++) {
                    uint16_t a = (uint16_t)(waddr + k);
                    if (in_range(out, a)) out->smc[a - base] = 1;
                }
            }

            /* A taken branch/call/jump target starts a new block. */
            if (in.cflow == CF_BRANCH || in.cflow == CF_JMP || in.cflow == CF_CALL) {
                if (in.target) {
                    if (in_range(out, in.target)) { add_block(out, in.target); wl_push(&wl, in.target); }
                    else out->next_ext++;
                }
            }

            /* In the iterative dispatch model every control-flow instruction
             * ends a block. For the ones that can resume in line - a conditional
             * branch's not-taken path, the address a CALL/RST returns to, and the
             * instruction after a HALT (on the TI, HALT waits for an interrupt
             * then continues) - that resume address is itself a new block leader. */
            uint16_t next = (uint16_t)(pc + n);
            if (in.cflow == CF_BRANCH || in.cflow == CF_CALL ||
                in.cflow == CF_RST    || in.cflow == CF_STOP) {
                if (in_range(out, next)) { add_block(out, next); wl_push(&wl, next); }
            }
            if (in.cflow != CF_NORMAL) break;   /* terminator: end this run */
            pc = next;                          /* fall through to next insn */
        }
    }

    free(wl.a);
    if (out->nblocks > 1)
        qsort(out->blocks, (size_t)out->nblocks, sizeof(uint16_t), cmp_u16);
    return 0;
}

void z80_prog_free(z80_prog_t *p)
{
    free(p->blocks);
    free(p->seen);
    free(p->smc);
    p->blocks = NULL;
    p->seen = NULL;
    p->smc = NULL;
    p->nblocks = 0;
}
