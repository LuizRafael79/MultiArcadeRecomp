/* emit.c - C emission for the Z80 -> pacrecomp static recompiler. See emit.h.
 *
 * The translator below lowers each discovered basic block to one readable C
 * function. Every instruction is annotated with its address + disassembly and
 * turned into direct pac_cpu state updates plus pac_* ALU helper calls from the
 * runtime (cpu.h). Memory goes through pac_mem_read/write, I/O through
 * pac_port_in/out, and the Z80 stack through pac_push16/pac_pop16. Every transfer
 * of control (branch / JP / CALL / RET / RST / fall-through into another
 * leader) sets pac_cpu.pc.w and returns to the host driver, which dispatches the
 * next block - so a never-returning Z80 loop ping-pongs through the driver
 * instead of recursing in C.
 *
 * Coverage: the full main table, the CB plane (rotates/shifts/BIT/RES/SET),
 * the common ED ops (NEG, IN/OUT (C), SBC/ADC HL, LD rr,(nn), LD I/R, RRD/RLD,
 * IM, the LDx/CPx/INx/OUTx block ops), and DD/FD (IX/IY, (IX+d)/(IY+d), IXH/IXL)
 * including the DDCB/FDCB bit ops. Genuinely undefined encodings become a marked
 * no-op; z80_emit reports any it could not translate. */
#include "emit.h"
#include "decode.h"
#include "analyze.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Self-modified-byte map (from z80_analyze) + image bounds, set in z80_emit.
 * An operand overlapping a self-modified byte is emitted as a live memory read
 * so a runtime patch (e.g. Phoenix's `ld (smc+1),sp` / `ld sp,nn` trick) works. */
static const uint8_t *g_smc;
static uint16_t       g_smc_base;
static int            g_smc_len;

static int smc_hit(uint16_t addr, int nbytes) {
    if (!g_smc) return 0;
    for (int k = 0; k < nbytes; k++) {
        int o = (int)(uint16_t)(addr + k) - (int)g_smc_base;
        if (o >= 0 && o < g_smc_len && g_smc[o]) return 1;
    }
    return 0;
}
/* Emit a 16-bit immediate at operand address `oa`: a literal, or a live read. */
static void imm16(FILE *f, uint16_t oa, uint16_t val) {
    if (smc_hit(oa, 2)) fprintf(f, "pac_mem_read16(0x%04X)", oa);
    else                fprintf(f, "0x%04X", val);
}
/* Emit an 8-bit immediate at operand address `oa`. */
static void imm8(FILE *f, uint16_t oa, uint8_t val) {
    if (smc_hit(oa, 1)) fprintf(f, "pac_mem_read(0x%04X)", oa);
    else                fprintf(f, "0x%02X", val);
}

/* --- decode context: which plane / index register a main opcode runs under --
 * pfx is the active prefix; idx is 1 when this opcode references (IX+d)/(IY+d)
 * (decoder set operand_kind == Z_IDX); disp is the signed displacement. */
typedef struct {
    z80_prefix_t pfx;
    int          idx;
    int          disp;
} ctx_t;

/* --- bookkeeping for unimplemented opcodes (reported to the caller) --------- */
static int  g_unimpl_total;
static int  g_unimpl_kinds;
static char g_unimpl_seen[7][256];   /* [prefix-bucket][opcode] = reported     */

static int prefix_bucket(z80_prefix_t p) {
    switch (p) {
        case ZP_CB:   return 1;
        case ZP_ED:   return 2;
        case ZP_DD:   return 3;
        case ZP_FD:   return 4;
        case ZP_DDCB: return 5;
        case ZP_FDCB: return 6;
        default:      return 0;
    }
}

static void note_unimpl(const z80_insn_t *in) {
    int b = prefix_bucket(in->prefix);
    g_unimpl_total++;
    if (!g_unimpl_seen[b][in->opcode]) {
        g_unimpl_seen[b][in->opcode] = 1;
        g_unimpl_kinds++;
        char dis[48];
        z80_format(in, dis, sizeof dis);
        fprintf(stderr, "  emit: unimplemented opcode %s%02X  (%s)\n",
                in->prefix == ZP_NONE ? "" :
                in->prefix == ZP_CB ? "CB " :
                in->prefix == ZP_ED ? "ED " :
                in->prefix == ZP_DD ? "DD " :
                in->prefix == ZP_FD ? "FD " :
                in->prefix == ZP_DDCB ? "DDCB " : "FDCB ",
                in->opcode, dis);
    }
}

/* --- small string helpers --------------------------------------------------- */

/* 8-bit register name for index r in {0,1,2,3,4,5,7}. r==6 is memory and must
 * be handled by the caller. Under DD/FD *without* an index displacement, H and
 * L become IXH/IXL; with a displacement the register operand keeps real H/L. */
static const char *r8name(int r, const ctx_t *c) {
    switch (r) {
        case 0: return "pac_cpu.bc.b.hi";   /* B */
        case 1: return "pac_cpu.bc.b.lo";   /* C */
        case 2: return "pac_cpu.de.b.hi";   /* D */
        case 3: return "pac_cpu.de.b.lo";   /* E */
        case 4:                            /* H / IXH / IYH */
            if (!c->idx && c->pfx == ZP_DD) return "pac_cpu.ix.b.hi";
            if (!c->idx && c->pfx == ZP_FD) return "pac_cpu.iy.b.hi";
            return "pac_cpu.hl.b.hi";
        case 5:                            /* L / IXL / IYL */
            if (!c->idx && c->pfx == ZP_DD) return "pac_cpu.ix.b.lo";
            if (!c->idx && c->pfx == ZP_FD) return "pac_cpu.iy.b.lo";
            return "pac_cpu.hl.b.lo";
        case 7: return "pac_cpu.af.b.hi";   /* A */
        default: return "pac_cpu.af.b.hi";
    }
}

/* 16-bit pair name for rp in {0,1,2,3}; index 2 follows the active prefix
 * (HL/IX/IY), index 3 = SP. */
static const char *rpname(int rp, const ctx_t *c) {
    switch (rp) {
        case 0: return "pac_cpu.bc.w";
        case 1: return "pac_cpu.de.w";
        case 2: return c->pfx == ZP_DD ? "pac_cpu.ix.w"
                     : c->pfx == ZP_FD ? "pac_cpu.iy.w" : "pac_cpu.hl.w";
        default: return "pac_cpu.sp.w";
    }
}

/* As rpname but index 3 = AF (PUSH/POP). */
static const char *rpname_af(int rp, const ctx_t *c) {
    if (rp == 3) return "pac_cpu.af.w";
    return rpname(rp, c);
}

/* The HL/IX/IY pair for the active prefix. */
static const char *hlpair(const ctx_t *c) {
    return c->pfx == ZP_DD ? "pac_cpu.ix.w"
         : c->pfx == ZP_FD ? "pac_cpu.iy.w" : "pac_cpu.hl.w";
}

/* Address expression for (HL)/(IX+d)/(IY+d) memory access. */
static void memaddr(const ctx_t *c, char *out, int n) {
    if (c->idx && c->pfx == ZP_DD)
        snprintf(out, n, "(uint16_t)(pac_cpu.ix.w + (%d))", c->disp);
    else if (c->idx && c->pfx == ZP_FD)
        snprintf(out, n, "(uint16_t)(pac_cpu.iy.w + (%d))", c->disp);
    else
        snprintf(out, n, "pac_cpu.hl.w");
}

/* Read-value expression for register index r (memory or register). */
static void rval(int r, const ctx_t *c, char *out, int n) {
    if (r == 6) {
        char a[64];
        memaddr(c, a, sizeof a);
        snprintf(out, n, "pac_mem_read(%s)", a);
    } else {
        snprintf(out, n, "%s", r8name(r, c));
    }
}

/* Condition expression for cond_code 0..7. */
static const char *condstr(uint8_t cc) {
    switch (cc) {
        case 0: return "!(pac_f & PAC_FLAG_Z)";   /* NZ */
        case 1: return "(pac_f & PAC_FLAG_Z)";    /* Z  */
        case 2: return "!(pac_f & PAC_FLAG_C)";   /* NC */
        case 3: return "(pac_f & PAC_FLAG_C)";    /* C  */
        case 4: return "!(pac_f & PAC_FLAG_PV)";  /* PO */
        case 5: return "(pac_f & PAC_FLAG_PV)";   /* PE */
        case 6: return "!(pac_f & PAC_FLAG_S)";   /* P  */
        default: return "(pac_f & PAC_FLAG_S)";   /* M  */
    }
}

static const char *alu_helper(int a) {
    switch (a) {
        case 0: return "pac_add8";
        case 1: return "pac_adc8";
        case 2: return "pac_sub8";
        case 3: return "pac_sbc8";
        case 4: return "pac_and8";
        case 5: return "pac_xor8";
        case 6: return "pac_or8";
        default: return NULL;   /* 7 = CP: special (no store) */
    }
}

static const char *rot_helper(int y) {
    static const char *r[8] =
        { "pac_rlc", "pac_rrc", "pac_rl", "pac_rr",
          "pac_sla", "pac_sra", "pac_sll", "pac_srl" };
    return r[y & 7];
}

/* --- control-flow emission -------------------------------------------------- */

static void emit_control(FILE *f, const z80_insn_t *in) {
    uint16_t next = (uint16_t)(in->pc + in->len);
    uint16_t tgt  = in->target;
    int op = in->opcode;

    switch (in->cflow) {
        case CF_JMP:
            fprintf(f, "pac_cpu.pc.w = 0x%04X; return;", tgt);
            return;
        case CF_JPHL:
            fprintf(f, "pac_cpu.pc.w = %s; return;", hlpair(&(ctx_t){in->prefix,0,0}));
            return;
        case CF_CALL:
            fprintf(f, "pac_push16(0x%04X); pac_cpu.pc.w = 0x%04X; return;", next, tgt);
            return;
        case CF_RST:
            fprintf(f, "pac_push16(0x%04X); pac_cpu.pc.w = 0x%04X; return;", next, tgt);
            return;
        case CF_RET:
            fprintf(f, "pac_cpu.pc.w = pac_pop16(); return;");
            return;
        case CF_STOP:
            fprintf(f, "pac_cpu.halted = 1; pac_cpu.pc.w = 0x%04X; return;", next);
            return;
        case CF_BRANCH:
            if (op == 0x10) {   /* DJNZ e */
                fprintf(f,
                    "if (--pac_cpu.bc.b.hi != 0) { pac_cpu.pc.w = 0x%04X; } "
                    "else { pac_cpu.pc.w = 0x%04X; } return;", tgt, next);
                return;
            }
            {
                const char *cond = condstr(in->cond_code);
                if ((op & 0xC7) == 0xC0) {        /* RET cc */
                    fprintf(f,
                        "if (%s) { pac_cpu.pc.w = pac_pop16(); } "
                        "else { pac_cpu.pc.w = 0x%04X; } return;", cond, next);
                } else if ((op & 0xC7) == 0xC4) { /* CALL cc,nn */
                    fprintf(f,
                        "if (%s) { pac_push16(0x%04X); pac_cpu.pc.w = 0x%04X; } "
                        "else { pac_cpu.pc.w = 0x%04X; } return;",
                        cond, next, tgt, next);
                } else {                          /* JP cc,nn / JR cc,e */
                    fprintf(f,
                        "if (%s) { pac_cpu.pc.w = 0x%04X; } "
                        "else { pac_cpu.pc.w = 0x%04X; } return;", cond, tgt, next);
                }
            }
            return;
        default:
            fprintf(f, "pac_cpu.pc.w = 0x%04X; return; /* ?cflow */", next);
            return;
    }
}

/* --- CB plane (plain CB, or DDCB/FDCB on (IX+d)/(IY+d)) --------------------- */

static void emit_cb(FILE *f, const z80_insn_t *in) {
    int op = in->opcode;
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    int mem;
    char addr[64];

    if (in->prefix == ZP_DDCB) {
        mem = 1;
        snprintf(addr, sizeof addr, "(uint16_t)(pac_cpu.ix.w + (%d))", in->disp);
    } else if (in->prefix == ZP_FDCB) {
        mem = 1;
        snprintf(addr, sizeof addr, "(uint16_t)(pac_cpu.iy.w + (%d))", in->disp);
    } else if (z == 6) {
        mem = 1;
        snprintf(addr, sizeof addr, "pac_cpu.hl.w");
    } else {
        mem = 0;
    }
    ctx_t c = { ZP_NONE, 0, 0 };   /* CB regs are always the real B..A set */
    const char *reg = mem ? NULL : r8name(z, &c);

    switch (x) {
        case 0: { /* rotate / shift */
            const char *h = rot_helper(y);
            if (mem)
                fprintf(f, "pac_mem_write(%s, %s(pac_mem_read(%s)));", addr, h, addr);
            else
                fprintf(f, "%s = %s(%s);", reg, h, reg);
            return;
        }
        case 1: /* BIT b,r */
            if (mem) fprintf(f, "pac_bit(%d, pac_mem_read(%s));", y, addr);
            else     fprintf(f, "pac_bit(%d, %s);", y, reg);
            return;
        case 2: /* RES b,r */
            if (mem)
                fprintf(f, "pac_mem_write(%s, (uint8_t)(pac_mem_read(%s) & ~(1u << %d)));",
                        addr, addr, y);
            else
                fprintf(f, "%s &= (uint8_t)~(1u << %d);", reg, y);
            return;
        default: /* 3: SET b,r */
            if (mem)
                fprintf(f, "pac_mem_write(%s, (uint8_t)(pac_mem_read(%s) | (1u << %d)));",
                        addr, addr, y);
            else
                fprintf(f, "%s |= (uint8_t)(1u << %d);", reg, y);
            return;
    }
}

/* --- ED plane -------------------------------------------------------------- */

static void emit_ed(FILE *f, const z80_insn_t *in) {
    int op = in->opcode;
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    int rp = (op >> 4) & 3;

    /* Block transfer / search / I/O ops (0xA0-0xBB). */
    switch (op) {
        case 0xA0: /* LDI */
            fprintf(f, "pac_mem_write(pac_cpu.de.w, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.de.w++; pac_cpu.bc.w--; "
                       "pac_f &= ~(PAC_FLAG_H|PAC_FLAG_N|PAC_FLAG_PV); "
                       "if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xA8: /* LDD */
            fprintf(f, "pac_mem_write(pac_cpu.de.w, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.de.w--; pac_cpu.bc.w--; "
                       "pac_f &= ~(PAC_FLAG_H|PAC_FLAG_N|PAC_FLAG_PV); "
                       "if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xB0: /* LDIR */
            fprintf(f, "do { pac_mem_write(pac_cpu.de.w, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.de.w++; pac_cpu.bc.w--; } "
                       "while (pac_cpu.bc.w); pac_f &= ~(PAC_FLAG_H|PAC_FLAG_N|PAC_FLAG_PV);");
            return;
        case 0xB8: /* LDDR */
            fprintf(f, "do { pac_mem_write(pac_cpu.de.w, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.de.w--; pac_cpu.bc.w--; } "
                       "while (pac_cpu.bc.w); pac_f &= ~(PAC_FLAG_H|PAC_FLAG_N|PAC_FLAG_PV);");
            return;
        case 0xA1: /* CPI */
            fprintf(f, "pac_cp8(pac_a, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.w--; "
                       "pac_f &= ~PAC_FLAG_PV; if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xA9: /* CPD */
            fprintf(f, "pac_cp8(pac_a, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.w--; "
                       "pac_f &= ~PAC_FLAG_PV; if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xB1: /* CPIR */
            fprintf(f, "do { pac_cp8(pac_a, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.w--; } "
                       "while (pac_cpu.bc.w && !(pac_f & PAC_FLAG_Z)); "
                       "pac_f &= ~PAC_FLAG_PV; if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xB9: /* CPDR */
            fprintf(f, "do { pac_cp8(pac_a, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.w--; } "
                       "while (pac_cpu.bc.w && !(pac_f & PAC_FLAG_Z)); "
                       "pac_f &= ~PAC_FLAG_PV; if (pac_cpu.bc.w) pac_f |= PAC_FLAG_PV;");
            return;
        case 0xA2: /* INI */
            fprintf(f, "pac_mem_write(pac_cpu.hl.w, pac_port_in(pac_cpu.bc.b.lo)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.b.hi = pac_dec8(pac_cpu.bc.b.hi);");
            return;
        case 0xAA: /* IND */
            fprintf(f, "pac_mem_write(pac_cpu.hl.w, pac_port_in(pac_cpu.bc.b.lo)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.b.hi = pac_dec8(pac_cpu.bc.b.hi);");
            return;
        case 0xB2: /* INIR */
            fprintf(f, "do { pac_mem_write(pac_cpu.hl.w, pac_port_in(pac_cpu.bc.b.lo)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.b.hi--; } while (pac_cpu.bc.b.hi); "
                       "pac_f |= PAC_FLAG_Z | PAC_FLAG_N;");
            return;
        case 0xBA: /* INDR */
            fprintf(f, "do { pac_mem_write(pac_cpu.hl.w, pac_port_in(pac_cpu.bc.b.lo)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.b.hi--; } while (pac_cpu.bc.b.hi); "
                       "pac_f |= PAC_FLAG_Z | PAC_FLAG_N;");
            return;
        case 0xA3: /* OUTI */
            fprintf(f, "pac_port_out(pac_cpu.bc.b.lo, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.b.hi = pac_dec8(pac_cpu.bc.b.hi);");
            return;
        case 0xAB: /* OUTD */
            fprintf(f, "pac_port_out(pac_cpu.bc.b.lo, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.b.hi = pac_dec8(pac_cpu.bc.b.hi);");
            return;
        case 0xB3: /* OTIR */
            fprintf(f, "do { pac_port_out(pac_cpu.bc.b.lo, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w++; pac_cpu.bc.b.hi--; } while (pac_cpu.bc.b.hi); "
                       "pac_f |= PAC_FLAG_Z | PAC_FLAG_N;");
            return;
        case 0xBB: /* OTDR */
            fprintf(f, "do { pac_port_out(pac_cpu.bc.b.lo, pac_mem_read(pac_cpu.hl.w)); "
                       "pac_cpu.hl.w--; pac_cpu.bc.b.hi--; } while (pac_cpu.bc.b.hi); "
                       "pac_f |= PAC_FLAG_Z | PAC_FLAG_N;");
            return;
        default: break;
    }

    /* The 0x40-0x7F extended block. */
    if (x == 1) {
        switch (z) {
            case 0: /* IN r,(C)  (y==6: IN (C), flags only) */
                if (y == 6) fprintf(f, "pac_port_in(pac_cpu.bc.b.lo);");
                else        fprintf(f, "%s = pac_port_in(pac_cpu.bc.b.lo);",
                                    r8name(y, &(ctx_t){ZP_NONE,0,0}));
                return;
            case 1: /* OUT (C),r  (y==6: OUT (C),0) */
                if (y == 6) fprintf(f, "pac_port_out(pac_cpu.bc.b.lo, 0);");
                else        fprintf(f, "pac_port_out(pac_cpu.bc.b.lo, %s);",
                                    r8name(y, &(ctx_t){ZP_NONE,0,0}));
                return;
            case 2: /* SBC/ADC HL,rp */
                if (op & 0x08)
                    fprintf(f, "pac_cpu.hl.w = pac_adc16(pac_cpu.hl.w, %s);",
                            rpname(rp, &(ctx_t){ZP_NONE,0,0}));
                else
                    fprintf(f, "pac_cpu.hl.w = pac_sbc16(pac_cpu.hl.w, %s);",
                            rpname(rp, &(ctx_t){ZP_NONE,0,0}));
                return;
            case 3: /* LD (nn),rp / LD rp,(nn) */
                if (op & 0x08)
                    fprintf(f, "%s = pac_mem_read16(0x%04X);",
                            rpname(rp, &(ctx_t){ZP_NONE,0,0}), in->imm);
                else
                    fprintf(f, "pac_mem_write16(0x%04X, %s);",
                            in->imm, rpname(rp, &(ctx_t){ZP_NONE,0,0}));
                return;
            case 4: /* NEG */
                fprintf(f, "pac_a = pac_sub8(0, pac_a);");
                return;
            case 6: /* IM n */
                fprintf(f, "pac_cpu.im = %d;", (y == 2) ? 1 : (y == 3) ? 2 : 0);
                return;
            case 7:
                switch (op) {
                    case 0x47: fprintf(f, "pac_cpu.i = pac_a;"); return;
                    case 0x4F: fprintf(f, "pac_cpu.r = pac_a;"); return;
                    case 0x57: fprintf(f, "pac_a = pac_cpu.i;"); return; /* flags approx */
                    case 0x5F: fprintf(f, "pac_a = pac_cpu.r;"); return; /* flags approx */
                    case 0x67: /* RRD */
                        fprintf(f, "{ uint8_t _m = pac_mem_read(pac_cpu.hl.w); "
                                   "uint8_t _a = pac_a; "
                                   "pac_mem_write(pac_cpu.hl.w, (uint8_t)((_a << 4) | (_m >> 4))); "
                                   "pac_a = (uint8_t)((_a & 0xF0) | (_m & 0x0F)); }");
                        return;
                    case 0x6F: /* RLD */
                        fprintf(f, "{ uint8_t _m = pac_mem_read(pac_cpu.hl.w); "
                                   "uint8_t _a = pac_a; "
                                   "pac_mem_write(pac_cpu.hl.w, (uint8_t)((_m << 4) | (_a & 0x0F))); "
                                   "pac_a = (uint8_t)((_a & 0xF0) | (_m >> 4)); }");
                        return;
                    default: fprintf(f, "; /* ED %02X nop */", op); return;
                }
            default: break;
        }
    }

    /* Any other ED opcode is an undefined 2-byte NOP. */
    fprintf(f, "; /* ED %02X nop */", op);
}

/* --- main table (also drives DD/FD via ctx) -------------------------------- */

static void emit_main(FILE *f, const z80_insn_t *in, const ctx_t *c) {
    int op = in->opcode;
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    char a[64], v[96], v2[96];

    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (op == 0x00) { fprintf(f, "; /* nop */"); return; }
            if (op == 0x08) { /* EX AF,AF' */
                fprintf(f, "{ pac_reg16_t _t = pac_cpu.af; pac_cpu.af = pac_cpu.af_; "
                           "pac_cpu.af_ = _t; }");
                return;
            }
            note_unimpl(in); fprintf(f, "; /* TODO unimpl */"); return;
        case 1:
            if (op & 0x08) {  /* ADD HL,rp */
                fprintf(f, "%s = pac_add16(%s, %s);",
                        hlpair(c), hlpair(c), rpname((op >> 4) & 3, c));
            } else {          /* LD rp,nn */
                fprintf(f, "%s = ", rpname((op >> 4) & 3, c));
                imm16(f, (uint16_t)(in->pc + in->len - 2), in->imm);
                fprintf(f, ";");
            }
            return;
        case 2:
            switch (y) {
                case 0: fprintf(f, "pac_mem_write(pac_cpu.bc.w, pac_a);"); return;
                case 1: fprintf(f, "pac_a = pac_mem_read(pac_cpu.bc.w);"); return;
                case 2: fprintf(f, "pac_mem_write(pac_cpu.de.w, pac_a);"); return;
                case 3: fprintf(f, "pac_a = pac_mem_read(pac_cpu.de.w);"); return;
                case 4: fprintf(f, "pac_mem_write16(0x%04X, %s);", in->imm, hlpair(c)); return;
                case 5: fprintf(f, "%s = pac_mem_read16(0x%04X);", hlpair(c), in->imm); return;
                case 6: fprintf(f, "pac_mem_write(0x%04X, pac_a);", in->imm); return;
                default: fprintf(f, "pac_a = pac_mem_read(0x%04X);", in->imm); return;
            }
        case 3: /* INC/DEC rp */
            fprintf(f, "%s%s;", rpname((op >> 4) & 3, c), (op & 0x08) ? "--" : "++");
            return;
        case 4: /* INC r */
            if (y == 6) {
                memaddr(c, a, sizeof a);
                fprintf(f, "pac_mem_write(%s, pac_inc8(pac_mem_read(%s)));", a, a);
            } else {
                fprintf(f, "%s = pac_inc8(%s);", r8name(y, c), r8name(y, c));
            }
            return;
        case 5: /* DEC r */
            if (y == 6) {
                memaddr(c, a, sizeof a);
                fprintf(f, "pac_mem_write(%s, pac_dec8(pac_mem_read(%s)));", a, a);
            } else {
                fprintf(f, "%s = pac_dec8(%s);", r8name(y, c), r8name(y, c));
            }
            return;
        case 6: /* LD r,n */
            if (y == 6) {
                memaddr(c, a, sizeof a);
                fprintf(f, "pac_mem_write(%s, ", a);
                imm8(f, (uint16_t)(in->pc + in->len - 1), (uint8_t)in->imm);
                fprintf(f, ");");
            } else {
                fprintf(f, "%s = ", r8name(y, c));
                imm8(f, (uint16_t)(in->pc + in->len - 1), (uint8_t)in->imm);
                fprintf(f, ";");
            }
            return;
        default: /* z==7: accumulator / flag ops */
            switch (op) {
                case 0x07: fprintf(f, "pac_a = pac_rlca(pac_a);"); return;
                case 0x0F: fprintf(f, "pac_a = pac_rrca(pac_a);"); return;
                case 0x17: fprintf(f, "pac_a = pac_rla(pac_a);");  return;
                case 0x1F: fprintf(f, "pac_a = pac_rra(pac_a);");  return;
                case 0x27: fprintf(f, "pac_a = pac_daa(pac_a);");  return;
                case 0x2F: fprintf(f, "pac_a = (uint8_t)~pac_a; pac_f |= PAC_FLAG_H | PAC_FLAG_N;");
                           return;
                case 0x37: fprintf(f, "pac_f |= PAC_FLAG_C; pac_f &= (uint8_t)~(PAC_FLAG_H | PAC_FLAG_N);");
                           return;
                default:   /* 0x3F CCF */
                    fprintf(f, "{ uint8_t _c = pac_f & PAC_FLAG_C; "
                               "pac_f &= (uint8_t)~(PAC_FLAG_N | PAC_FLAG_H); "
                               "if (_c) pac_f |= PAC_FLAG_H; pac_f ^= PAC_FLAG_C; }");
                    return;
            }
        }
    case 1: /* LD r,r' (0x76 HALT handled as control flow) */
        if (z == 6) {                 /* LD r,(HL)/(IX+d) */
            memaddr(c, a, sizeof a);
            fprintf(f, "%s = pac_mem_read(%s);", r8name(y, c), a);
        } else if (y == 6) {          /* LD (HL)/(IX+d),r */
            memaddr(c, a, sizeof a);
            fprintf(f, "pac_mem_write(%s, %s);", a, r8name(z, c));
        } else {
            fprintf(f, "%s = %s;", r8name(y, c), r8name(z, c));
        }
        return;
    case 2: { /* ALU A,r */
        rval(z, c, v, sizeof v);
        if (y == 7) fprintf(f, "pac_cp8(pac_a, %s);", v);
        else        fprintf(f, "pac_a = %s(pac_a, %s);", alu_helper(y), v);
        return;
    }
    default: /* x==3 */
        switch (z) {
        case 1:
            if (op & 0x08) {
                if (op == 0xD9) { /* EXX */
                    fprintf(f, "{ pac_reg16_t _t; "
                               "_t = pac_cpu.bc; pac_cpu.bc = pac_cpu.bc_; pac_cpu.bc_ = _t; "
                               "_t = pac_cpu.de; pac_cpu.de = pac_cpu.de_; pac_cpu.de_ = _t; "
                               "_t = pac_cpu.hl; pac_cpu.hl = pac_cpu.hl_; pac_cpu.hl_ = _t; }");
                    return;
                }
                if (op == 0xF9) { /* LD SP,HL/IX/IY */
                    fprintf(f, "pac_cpu.sp.w = %s;", hlpair(c));
                    return;
                }
                /* 0xC9 RET / 0xE9 JP(HL) are control flow - not reached here. */
            } else {              /* POP rp */
                fprintf(f, "%s = pac_pop16();", rpname_af((op >> 4) & 3, c));
                return;
            }
            note_unimpl(in); fprintf(f, "; /* TODO unimpl */"); return;
        case 3:
            switch (op) {
                case 0xD3: fprintf(f, "pac_port_out(0x%02X, pac_a);", in->imm); return;
                case 0xDB: fprintf(f, "pac_a = pac_port_in(0x%02X);", in->imm); return;
                case 0xE3: /* EX (SP),HL/IX/IY */
                    fprintf(f, "{ uint16_t _t = pac_mem_read16(pac_cpu.sp.w); "
                               "pac_mem_write16(pac_cpu.sp.w, %s); %s = _t; }",
                            hlpair(c), hlpair(c));
                    return;
                case 0xEB: /* EX DE,HL */
                    fprintf(f, "{ uint16_t _t = pac_cpu.de.w; "
                               "pac_cpu.de.w = pac_cpu.hl.w; pac_cpu.hl.w = _t; }");
                    return;
                case 0xF3: fprintf(f, "pac_cpu.iff1 = pac_cpu.iff2 = 0;"); return;
                case 0xFB: fprintf(f, "pac_cpu.iff1 = pac_cpu.iff2 = 1;"); return;
                /* 0xC3 JP nn is control flow - not reached here. */
                default: note_unimpl(in); fprintf(f, "; /* TODO unimpl */"); return;
            }
        case 5:
            if (!(op & 0x08)) { /* PUSH rp */
                fprintf(f, "pac_push16(%s);", rpname_af((op >> 4) & 3, c));
                return;
            }
            /* 0xCD CALL / 0xDD/0xED/0xFD prefixes are handled elsewhere. */
            note_unimpl(in); fprintf(f, "; /* TODO unimpl */"); return;
        case 6: { /* ALU A,n */
            if (y == 7) fprintf(f, "pac_cp8(pac_a, 0x%02X);", in->imm);
            else        fprintf(f, "pac_a = %s(pac_a, 0x%02X);", alu_helper(y), in->imm);
            (void)v2;
            return;
        }
        default:
            /* z in {0,2,4,7}: RET cc / JP cc / CALL cc / RST - all control flow. */
            note_unimpl(in); fprintf(f, "; /* TODO unimpl */"); return;
        }
    }
}

/* Lower one decoded instruction (no leading comment). */
static void emit_insn(FILE *f, const z80_insn_t *in) {
    if (in->cflow != CF_NORMAL) { emit_control(f, in); return; }

    switch (in->prefix) {
        case ZP_CB:
        case ZP_DDCB:
        case ZP_FDCB:
            emit_cb(f, in);
            return;
        case ZP_ED:
            emit_ed(f, in);
            return;
        case ZP_DD:
        case ZP_FD: {
            ctx_t c = { in->prefix, in->operand_kind == Z_IDX, in->disp };
            emit_main(f, in, &c);
            return;
        }
        default: {
            ctx_t c = { ZP_NONE, 0, 0 };
            emit_main(f, in, &c);
            return;
        }
    }
}

/* --- driver ---------------------------------------------------------------- */

int z80_emit(const uint8_t *code, int len, uint16_t base,
             const uint16_t *blocks, int nblocks,
             const char *outdir, const char *regname,
             const uint8_t *smc)
{
    char path[1024];
    int i;

    if (!code || !blocks || !outdir || !regname || len <= 0) return -1;

    g_smc = smc; g_smc_base = base; g_smc_len = len;

    g_unimpl_total = 0;
    g_unimpl_kinds = 0;
    memset(g_unimpl_seen, 0, sizeof g_unimpl_seen);

    /* Fast "is this address a block leader" lookup over the image. */
    uint8_t *is_leader = (uint8_t *)calloc((size_t)len, 1);
    if (!is_leader) return -1;
    for (i = 0; i < nblocks; i++) {
        int off = (int)(blocks[i] - base);
        if (off >= 0 && off < len) is_leader[off] = 1;
    }

    /* recomp_funcs.h */
    snprintf(path, sizeof path, "%s/recomp_funcs.h", outdir);
    FILE *h = fopen(path, "wb");
    if (!h) { free(is_leader); return -1; }
    fprintf(h,
        "/* recomp_funcs.h - generated by z80recomp. Do not edit by hand.\n"
        " * %d recompiled basic blocks. */\n"
        "#ifndef RECOMP_FUNCS_H\n#define RECOMP_FUNCS_H\n"
        "#include <stdint.h>\n\n"
        "void %s_register_funcs(void);\n\n"
        "#endif /* RECOMP_FUNCS_H */\n",
        nblocks, regname);
    fclose(h);

    /* recomp_funcs.c */
    snprintf(path, sizeof path, "%s/recomp_funcs.c", outdir);
    FILE *f = fopen(path, "wb");
    if (!f) { free(is_leader); return -1; }

    fprintf(f,
        "/* recomp_funcs.c - generated by z80recomp.\n"
        " *\n"
        " * STATIC RECOMPILATION: every reached Z80 basic block became one C\n"
        " * function `pac_func_XXXX`, where XXXX is its start address. Each line is\n"
        " *     [comment: AAAA: DISASSEMBLY]  C statement\n"
        " * CPU state lives in `pac_cpu` (cpu.h); the pac_* helpers perform an op\n"
        " * and set the Z80 flags exactly. Memory goes through pac_mem_read/write,\n"
        " * I/O through pac_port_in/out, the stack through pac_push16/pac_pop16.\n"
        " *\n"
        " * Control flow never recurses in C: each transfer (branch/JP/CALL/RET/\n"
        " * RST/fall-through) sets pac_cpu.pc.w and returns to the host driver,\n"
        " * which dispatches the next block.\n"
        " *\n"
        " * Do not edit by hand - regenerate with z80recomp. */\n"
        "#include \"pacrecomp/recomp_rt.h\"\n"
        "#include \"pacrecomp/io.h\"\n"
        "#include \"recomp_funcs.h\"\n\n");

    int emitted = 0;
    for (i = 0; i < nblocks; i++) {
        uint16_t start = blocks[i];
        int off = (int)(start - base);
        if (off < 0 || off >= len) continue;   /* leader outside image */

        /* Decode the block to find its end (for the header comment). */
        uint16_t pc = start;
        char dis[48];
        z80_insn_t in;

        fprintf(f, "/* pac_func_%04X: block at $%04X */\n", start, start);
        fprintf(f, "static void pac_func_%04X(void) {\n", start);

        for (;;) {
            int coff = (int)(pc - base);
            if (coff < 0 || coff >= len) {
                fprintf(f, "    pac_cpu.pc.w = 0x%04X; return;\n", pc);
                break;
            }
            int n = z80_decode(code + coff, len - coff, pc, &in);
            if (n <= 0) {
                fprintf(f, "    pac_cpu.pc.w = 0x%04X; return;\n", pc);
                break;
            }
            z80_format(&in, dis, sizeof dis);
            fprintf(f, "    /* %04X: %-20s */ ", pc, dis);
            emit_insn(f, &in);
            fprintf(f, "\n");

            uint16_t next = (uint16_t)(pc + n);

            if (in.cflow != CF_NORMAL)
                break;   /* emit_insn already produced the transfer + return */

            int noff = (int)(next - base);
            if (noff < 0 || noff >= len || is_leader[noff]) {
                /* Fall through into the next block (or off the image edge). */
                fprintf(f, "    pac_cpu.pc.w = 0x%04X; return;\n", next);
                break;
            }
            pc = next;
        }

        fprintf(f, "}\n\n");
        emitted++;
    }

    /* Registration function. */
    fprintf(f, "void %s_register_funcs(void) {\n", regname);
    for (i = 0; i < nblocks; i++) {
        int off = (int)(blocks[i] - base);
        if (off < 0 || off >= len) continue;
        fprintf(f, "    pac_register_func(0x%04X, pac_func_%04X);\n",
                blocks[i], blocks[i]);
    }
    fprintf(f, "}\n");

    fclose(f);
    free(is_leader);

    if (g_unimpl_total)
        fprintf(stderr, "emit: %d unimplemented opcode site(s), %d distinct\n",
                g_unimpl_total, g_unimpl_kinds);

    return emitted;
}
