#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "codegen.h"

#define MATCH(name, val) if (strcasecmp(n, name) == 0) return val
int z80_reg8(const char *n) {
    if (!n) return -1;
    MATCH("B",  0);
    MATCH("C",  1);
    MATCH("D",  2);
    MATCH("E",  3);
    MATCH("H",  4);
    MATCH("L",  5);
    MATCH("A",  7);
    MATCH("HL", 6);  /* (HL) encoded as reg 6 */
    return -1;
}

int z80_reg16(const char *n) {
    if (!n) return -1;
    MATCH("BC", 0);
    MATCH("B",  0);
    MATCH("DE", 1);
    MATCH("D",  1);
    MATCH("HL", 2);
    MATCH("H",  2);
    MATCH("SP", 3);
    return -1;
}

int z80_reg16af(const char *n) {
    if (!n) return -1;
    MATCH("BC", 0);
    MATCH("B", 0);
    MATCH("DE", 1);
    MATCH("D",  1);
    MATCH("HL", 2);
    MATCH("H",  2);
    MATCH("AF", 3);
    return -1;
}

int z80_cond(const char *n) {
    if (!n) return -1;
    MATCH("NZ", 0);
    MATCH("Z",  1);
    MATCH("NC", 2);
    MATCH("C",  3);
    MATCH("PO", 4);
    MATCH("PE", 5);
    MATCH("P",  6);
    MATCH("M",  7);
    return -1;
}

int z80_is_ix(const char *n) {
    return n && (strcasecmp(n, "IX") == 0 || strcasecmp(n, "IXH") == 0 || strcasecmp(n, "IXL") == 0);
}

int z80_is_iy(const char *n) {
    return n && (strcasecmp(n, "IY") == 0 || strcasecmp(n, "IYH") == 0 || strcasecmp(n, "IYL") == 0);
}

static void mk(Instruction *i, int n, ...){
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    memset(i, 0, sizeof(*i));
    i->len = n;
    i->reloc_pos = -1;
    for (int j = 0; j < n; j++)i->bytes[j] = __builtin_va_arg(ap, int) & 0xFF;

    __builtin_va_end(ap);
}

static void mk_rel16(Instruction *i, int nb, const unsigned char *b, Value *v){
    memset(i, 0, sizeof(*i));
    i->reloc_pos = -1;
    for (int j = 0; j < nb; j++)i->bytes[j] = b[j];

    i->bytes[nb] = v->num & 0xFF;
    i->bytes[nb + 1] = (v->num >> 8) & 0xFF;
    i->len = nb + 2;
    if (!val_is_absolute(*v)) {
        i->has_reloc  = 1;
        i->reloc_pos  = nb;
        i->reloc_size = 2;
        i->reloc_val  = *v;
    }
}

static void mk_rel8(Instruction *i, int nb, const unsigned char *b, Value *v){
    memset(i, 0, sizeof(*i));
    i->reloc_pos = -1;
    for (int j = 0; j < nb; j++)i->bytes[j] = b[j];

    int bv = (v->byte_op == 2) ? ((v->num >> 8) & 0xFF) : (v->num & 0xFF);
    i->bytes[nb] = bv;
    i->len = nb + 1;
    if (!val_is_absolute(*v)) {
        i->has_reloc  = 1;
        i->reloc_pos  = nb;
        i->reloc_size = 1;
        i->reloc_val  = *v;
    }
}

//////////////////////////////////////////////////
// NOTE: EMIT* macros include a return 0 statement
//////////////////////////////////////////////////

#define EMIT1(b1)             do { mk(inst, 1, b1); return 0; } while(0)
#define EMIT2(b1, b2)         do { mk(inst, 2, b1, b2); return 0; } while(0)
#define EMIT3(b1, b2, b3)     do { mk(inst, 3, b1, b2, b3); return 0; } while(0)
#define EMIT4(b1, b2, b3, b4) do { mk(inst, 4, b1, b2, b3, b4); return 0; } while(0)
#define EMIT_REL16(v, ...)    do { unsigned char b[] = {__VA_ARGS__}; mk_rel16(inst, sizeof(b), b, v); return 0; } while(0)
#define EMIT_REL8(v, ...)     do { unsigned char b[] = {__VA_ARGS__}; mk_rel8(inst, sizeof(b), b, v); return 0; } while(0)
#define IS_REG8(r) ((r) >= 0 && (r) != 6)

int encode_z80(const char *op, const char *o1, const char *o2,
               Value *v1, Value *v2, int ind1, int ind2,
               Value *ix1, Value *ix2, Instruction *inst) {
    memset(inst, 0, sizeof(*inst));
    inst->reloc_pos = -1;
    int r, s, rr, cc;
    unsigned char pre;

    /* === no-operand === */
    if (!strcasecmp(op, "NOP"))  EMIT1(0x00);
    if (!strcasecmp(op, "HALT")) EMIT1(0x76);
    if (!strcasecmp(op, "DI"))   EMIT1(0xF3);
    if (!strcasecmp(op, "EI"))   EMIT1(0xFB);
    if (!strcasecmp(op, "EXX"))  EMIT1(0xD9);
    if (!strcasecmp(op, "CCF"))  EMIT1(0x3F);
    if (!strcasecmp(op, "SCF"))  EMIT1(0x37);
    if (!strcasecmp(op, "DAA"))  EMIT1(0x27);
    if (!strcasecmp(op, "CPL"))  EMIT1(0x2F);
    if (!strcasecmp(op, "RLA"))  EMIT1(0x17);
    if (!strcasecmp(op, "RLCA")) EMIT1(0x07);
    if (!strcasecmp(op, "RRA"))  EMIT1(0x1F);
    if (!strcasecmp(op, "RRCA")) EMIT1(0x0F);
    if (!strcasecmp(op, "NEG"))  EMIT2(0xED, 0x44);
    if (!strcasecmp(op, "RETI")) EMIT2(0xED, 0x4D);
    if (!strcasecmp(op, "RETN")) EMIT2(0xED, 0x45);
    if (!strcasecmp(op, "RLD"))  EMIT2(0xED, 0x6F);
    if (!strcasecmp(op, "RRD"))  EMIT2(0xED, 0x67);
    /* block instructions */
    if (!strcasecmp(op, "LDI"))  EMIT2(0xED, 0xA0);
    if (!strcasecmp(op, "LDIR")) EMIT2(0xED, 0xB0);
    if (!strcasecmp(op, "LDD"))  EMIT2(0xED, 0xA8);
    if (!strcasecmp(op, "LDDR")) EMIT2(0xED, 0xB8);
    if (!strcasecmp(op, "CPI"))  EMIT2(0xED, 0xA1);
    if (!strcasecmp(op, "CPIR")) EMIT2(0xED, 0xB1);
    if (!strcasecmp(op, "CPD"))  EMIT2(0xED, 0xA9);
    if (!strcasecmp(op, "CPDR")) EMIT2(0xED, 0xB9);
    if (!strcasecmp(op, "INI"))  EMIT2(0xED, 0xA2);
    if (!strcasecmp(op, "INIR")) EMIT2(0xED, 0xB2);
    if (!strcasecmp(op, "IND"))  EMIT2(0xED, 0xAA);
    if (!strcasecmp(op, "INDR")) EMIT2(0xED, 0xBA);
    if (!strcasecmp(op, "OUTI")) EMIT2(0xED, 0xA3);
    if (!strcasecmp(op, "OTIR")) EMIT2(0xED, 0xB3);
    if (!strcasecmp(op, "OUTD")) EMIT2(0xED, 0xAB);
    if (!strcasecmp(op, "OTDR")) EMIT2(0xED, 0xBB);
    /* === LD === */
    if (!strcasecmp(op, "LD")) {
        /* LD r, r' */
        if (o1 && o2 && !ind1 && !ind2) {
            r = z80_reg8(o1);
            s = z80_reg8(o2);
            if (IS_REG8(r) && IS_REG8(s))                               EMIT1(0x40 | (r << 3) | s);
            /* LD r, (HL) */
            if (IS_REG8(r) && strcasecmp(o2, "HL") == 0 && ind2)        EMIT1(0x46 | (r << 3));
            /* LD SP, HL */
            if (strcasecmp(o1, "SP") == 0 && strcasecmp(o2, "HL") == 0) EMIT1(0xF9);
            /* LD SP, IX/IY */
            if (strcasecmp(o1, "SP") == 0 && z80_is_ix(o2))             EMIT2(0xDD, 0xF9);
            if (strcasecmp(o1, "SP") == 0 && z80_is_iy(o2))             EMIT2(0xFD, 0xF9);
            /* LD A, I / LD A, R */
            if (strcasecmp(o1, "A") == 0 && strcasecmp(o2, "I") == 0)   EMIT2(0xED, 0x57);
            if (strcasecmp(o1, "A") == 0 && strcasecmp(o2, "R") == 0)   EMIT2(0xED, 0x5F);
            if (strcasecmp(o1, "I") == 0 && strcasecmp(o2, "A") == 0)   EMIT2(0xED, 0x47);
            if (strcasecmp(o1, "R") == 0 && strcasecmp(o2, "A") == 0)   EMIT2(0xED, 0x4F);
        }
        /* LD r, (HL) */
        if (o1 && o2 && !ind1 && ind2 && strcasecmp(o2, "HL") == 0) {
            r = z80_reg8(o1);
            if (IS_REG8(r)) EMIT1(0x46 | (r << 3));
        }
        /* LD (HL), r */
        if (o1 && ind1 && !ind2 && strcasecmp(o1, "HL") == 0) {
            if (o2) {
                s = z80_reg8(o2);
                if (IS_REG8(s)) EMIT1(0x70 | s);
            }
            /* LD (HL), n */ if (v2) EMIT2(0x36, v2->num & 0xFF);
        }
        /* LD r, n (immediate) */
        if (o1 && !ind1 && v2 && !ind2) {
            r = z80_reg8(o1);
            if (IS_REG8(r)) EMIT_REL8(v2, 0x06 | (r << 3));
        }
        /* LD A, (BC)/(DE) */
        if (o1 && strcasecmp(o1, "A") == 0 && o2 && ind2) {
            if (strcasecmp(o2, "BC") == 0) EMIT1(0x0A);
            if (strcasecmp(o2, "DE") == 0) EMIT1(0x1A);
        }
        /* LD (BC), A / LD (DE), A */
        if (o1 && ind1 && o2 && strcasecmp(o2, "A") == 0) {
            if (strcasecmp(o1, "BC") == 0) EMIT1(0x02);
            if (strcasecmp(o1, "DE") == 0) EMIT1(0x12);
        }
        /* LD A, (nn) */
        if (o1 && strcasecmp(o1, "A") == 0 && ind2 && v2) EMIT_REL16(v2, 0x3A);
        /* LD (nn), A */
        if (ind1 && v1 && o2 && strcasecmp(o2, "A") == 0) EMIT_REL16(v1, 0x32);
        /* LD rr, nn */
        if (o1 && !ind1 && v2 && !ind2) {
            rr = z80_reg16(o1);
            if (rr >= 0)       EMIT_REL16(v2, 0x01 | (rr << 4));
            if (z80_is_ix(o1)) EMIT_REL16(v2, 0xDD, 0x21);
            if (z80_is_iy(o1)) EMIT_REL16(v2, 0xFD, 0x21);
        }
        /* LD HL, (nn) */
        if (o1 && strcasecmp(o1, "HL") == 0 && !ind1 && ind2 && v2) EMIT_REL16(v2, 0x2A);
        /* LD (nn), HL */
        if (ind1 && v1 && o2 && strcasecmp(o2, "HL") == 0) EMIT_REL16(v1, 0x22);
        /* LD rr, (nn) - ED prefix */
        if (o1 && !ind1 && ind2 && v2) {
            rr = z80_reg16(o1);
            if (rr >= 0 && rr != 2) EMIT_REL16(v2, 0xED, 0x4B | (rr << 4));
            if (z80_is_ix(o1))      EMIT_REL16(v2, 0xDD, 0x2A);
            if (z80_is_iy(o1))      EMIT_REL16(v2, 0xFD, 0x2A);
        }
        /* LD (nn), rr - ED prefix */
        if (ind1 && v1 && o2 && !ind2) {
            rr = z80_reg16(o2);
            if (rr >= 0 && rr != 2) EMIT_REL16(v1, 0xED, 0x43 | (rr << 4));
            if (z80_is_ix(o2))      EMIT_REL16(v1, 0xDD, 0x22);
            if (z80_is_iy(o2))      EMIT_REL16(v1, 0xFD, 0x22);
        }
        /* LD r, (IX+d)/(IY+d) */
        if (o1 && !ind1 && ind2 && (z80_is_ix(o2) || z80_is_iy(o2)) && ix2) {
            r = z80_reg8(o1);
            pre = z80_is_ix(o2) ? 0xDD : 0xFD;
            if (IS_REG8(r)) EMIT3(pre, 0x46 | (r << 3), ix2->num & 0xFF);
        }
        /* LD (IX+d), r / (IY+d), r */
        if (ind1 && (z80_is_ix(o1) || z80_is_iy(o1)) && ix1) {
            pre = z80_is_ix(o1) ? 0xDD : 0xFD;
            if (o2 && !ind2) {
                s = z80_reg8(o2);
                if (IS_REG8(s)) EMIT3(pre, 0x70 | s, ix1->num & 0xFF);
            }
            /* LD (IX+d), n */
            if (v2) EMIT_REL8(v2, pre, 0x36, ix1->num & 0xFF);
        }
        return -1;
    }
    /* === PUSH/POP === */
    if (!strcasecmp(op, "PUSH")) {
        if (z80_is_ix(o1))     EMIT2(0xDD, 0xE5);
        if (z80_is_iy(o1))     EMIT2(0xFD, 0xE5);
        rr = z80_reg16af(o1);
        if (rr >= 0)           EMIT1(0xC5 | (rr << 4));
        return -1;
    }
    if (!strcasecmp(op, "POP")) {
        if (z80_is_ix(o1))     EMIT2(0xDD, 0xE1);
        if (z80_is_iy(o1))     EMIT2(0xFD, 0xE1);
        rr = z80_reg16af(o1);
        if (rr >= 0)           EMIT1(0xC1 | (rr << 4));
        return -1;
    }
    /* === ALU: ADD ADC SUB SBC AND OR XOR CP === */
    {
        static const struct {const char*n;
        int g;} alu[] = {
            {"ADD", 0}, {"ADC", 1}, {"SUB", 2}, {"SBC", 3},
            {"AND", 4}, {"XOR", 5}, {"OR", 6}, {"CP", 7}, {NULL, 0}
        };
        for (int i = 0; alu[i].n; i++) {
            if (strcasecmp(op, alu[i].n) != 0) continue;

            int g = alu[i].g;
            /* ADD HL, rr / ADC HL, rr / SBC HL, rr */
            if (o1 && o2 && strcasecmp(o1, "HL") == 0) {
                rr = z80_reg16(o2);
                if (rr >= 0) {
                    if (g == 0) EMIT1(0x09 | (rr << 4));
                    if (g == 1) EMIT2(0xED, 0x4A | (rr << 4));
                    if (g == 3) EMIT2(0xED, 0x42 | (rr << 4));
                }
            }
            /* ADD IX, rr / ADD IY, rr */
            if (o1 && o2 && g == 0 && z80_is_ix(o1)) {
                rr = z80_reg16(o2);
                if (rr >= 0) EMIT2(0xDD, 0x09 | (rr << 4));
            }
            if (o1 && o2 && g == 0 && z80_is_iy(o1)) {
                rr = z80_reg16(o2);
                if (rr >= 0) EMIT2(0xFD, 0x09 | (rr << 4));
            }
            /* ALU A, r or ALU r (implicit A) */
            int has_o2 = (o2 && o2[0]);
            int has_v2 = (v2 != NULL);
            /* two-operand form: ALU A, src */
            if (o1 && o1[0] && (has_o2 || has_v2)) {
                /* second operand is the source */
                const char *src = has_o2 ? o2 : NULL;
                int src_ind = ind2;
                Value *src_ix = ix2;
                Value *src_v  = has_v2 ? v2 : NULL;
                /* ALU A, (IX+d) / (IY+d) */
                if (src && src_ind && (z80_is_ix(src) || z80_is_iy(src)) && src_ix) {
                    pre = z80_is_ix(src) ? 0xDD : 0xFD;
                    EMIT3(pre, 0x86 | (g << 3), src_ix->num & 0xFF);
                }
                /* ALU A, (HL) */
                if (src && src_ind && strcasecmp(src, "HL") == 0) EMIT1(0x86 | (g << 3));
                /* ALU A, r */
                if (src) {
                    s = z80_reg8(src);
                    if (IS_REG8(s) && !src_ind) EMIT1(0x80 | (g << 3) | s);
                }
                /* ALU A, n */
                if (src_v) EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                return -1;
            }
            /* single-operand form: ALU src (implicit A) */
            {
                const char *src = o1;
                int src_ind = ind1;
                Value *src_ix = ix1;
                Value *src_v  = v1;
                if (!src || !src[0]) {
                    if (src_v) EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                    return -1;
                }
                if (src_ind && (z80_is_ix(src) || z80_is_iy(src)) && src_ix) {
                    pre = z80_is_ix(src) ? 0xDD : 0xFD;
                    EMIT3(pre, 0x86 | (g << 3), src_ix->num & 0xFF);
                }
                if (src_ind && strcasecmp(src, "HL") == 0) EMIT1(0x86 | (g << 3));
                s = z80_reg8(src);
                if (IS_REG8(s) && !src_ind) EMIT1(0x80 | (g << 3) | s);
                if (src_v && !src_ind)      EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                return -1;
            }
        }
    }
    /* === INC/DEC === */
    if (!strcasecmp(op, "INC") || !strcasecmp(op, "DEC")) {
        int is_dec = !strcasecmp(op, "DEC");
        /* INC/DEC (IX+d) */
        if (ind1 && (z80_is_ix(o1) || z80_is_iy(o1)) && ix1) {
            pre = z80_is_ix(o1) ? 0xDD : 0xFD;
            EMIT3(pre, is_dec ? 0x35 : 0x34, ix1->num & 0xFF);
        }
        /* INC/DEC r (check before rr since B/D/H match both) */
        r = z80_reg8(o1);
        if (IS_REG8(r) && !ind1) EMIT1((is_dec ? 0x05 : 0x04) | (r << 3));
        /* INC/DEC (HL) */
        if (ind1 && strcasecmp(o1, "HL") == 0) EMIT1(is_dec ? 0x35 : 0x34);
        /* INC/DEC rr */
        rr = z80_reg16(o1);
        if (rr >= 0 && !ind1)       EMIT1((is_dec ? 0x0B : 0x03) | (rr << 4));
        if (z80_is_ix(o1) && !ind1) EMIT2(0xDD, is_dec ? 0x2B : 0x23);
        if (z80_is_iy(o1) && !ind1) EMIT2(0xFD, is_dec ? 0x2B : 0x23);
        return -1;
    }
    /* === JP === */
    if (!strcasecmp(op, "JP")) {
        if (ind1 && !o2) {
            if (strcasecmp(o1, "HL") == 0) EMIT1(0xE9);
            if (z80_is_ix(o1))             EMIT2(0xDD, 0xE9);
            if (z80_is_iy(o1))             EMIT2(0xFD, 0xE9);
            /* JP (nn) - absolute jump */
            if (v1)                        EMIT_REL16(v1, 0xC3);
        }
        /* JP cc, nn */
        if (o1 && v2) {
            cc = z80_cond(o1);
            if (cc >= 0) EMIT_REL16(v2, 0xC2 | (cc << 3));
        }
        /* JP nn */
        if (v1 && !ind1) EMIT_REL16(v1, 0xC3);
        return -1;
    }
    /* === JR === */
    if (!strcasecmp(op, "JR")) {
        /* JR cc, e */
        if (o1 && v2) {
            cc = z80_cond(o1);
            if (cc >= 0 && cc <= 3) EMIT2(0x20 | (cc << 3), v2->num & 0xFF);
        }
        /* JR e */
        if (v1) EMIT2(0x18, v1->num & 0xFF);
        return -1;
    }
    /* === DJNZ === */
    if (!strcasecmp(op, "DJNZ")) {
        if (v1) EMIT2(0x10, v1->num & 0xFF);
        return -1;
    }
    /* === CALL === */
    if (!strcasecmp(op, "CALL")) {
        if (o1 && v2) {
            cc = z80_cond(o1);
            if (cc >= 0) EMIT_REL16(v2, 0xC4 | (cc << 3));
        }
        if (v1) EMIT_REL16(v1, 0xCD);
        return -1;
    }
    /* === RET === */
    if (!strcasecmp(op, "RET")) {
        if (!o1)     EMIT1(0xC9);
        cc = z80_cond(o1);
        if (cc >= 0) EMIT1(0xC0 | (cc << 3));
        return -1;
    }
    /* === RST === */
    if (!strcasecmp(op, "RST") && v1) {
        int n = v1->num;
        if (n <= 0x38 && (n & 7) == 0) EMIT1(0xC7 | n);
        /* also accept 0-7 */
        if (n <= 7) EMIT1(0xC7 | (n << 3));
        return -1;
    }
    /* === EX === */
    if (!strcasecmp(op, "EX")) {
        if (o1 && o2) {
            if (strcasecmp(o1, "DE") == 0 && strcasecmp(o2, "HL") == 0)                                 EMIT1(0xEB);
            if (strcasecmp(o1, "AF") == 0 && (strcasecmp(o2, "AF'") == 0 || strcasecmp(o2, "AF") == 0)) EMIT1(0x08);
            if (ind1 && strcasecmp(o1, "SP") == 0) {
                if (strcasecmp(o2, "HL") == 0) EMIT1(0xE3);
                if (z80_is_ix(o2))             EMIT2(0xDD, 0xE3);
                if (z80_is_iy(o2))             EMIT2(0xFD, 0xE3);
            }
        }
        return -1;
    }
    /* === IM === */
    if (!strcasecmp(op, "IM") && v1) {
        if (v1->num == 0) EMIT2(0xED, 0x46);
        if (v1->num == 1) EMIT2(0xED, 0x56);
        if (v1->num == 2) EMIT2(0xED, 0x5E);
        return -1;
    }
    /* === IN/OUT === */
    if (!strcasecmp(op, "IN")) {
        if (o1 && strcasecmp(o1, "A") == 0 && ind2 && v2) EMIT2(0xDB, v2->num & 0xFF);
        if (o1 && ind2 && strcasecmp(o2, "C") == 0) {
            r = z80_reg8(o1);
            if (r >= 0)                                   EMIT2(0xED, 0x40 | (r << 3));
        }
        return -1;
    }
    if (!strcasecmp(op, "OUT")) {
        if (ind1 && v1 && o2 && strcasecmp(o2, "A") == 0) EMIT2(0xD3, v1->num & 0xFF);
        if (ind1 && strcasecmp(o1, "C") == 0 && o2) {
            r = z80_reg8(o2);
            if (r >= 0)                                   EMIT2(0xED, 0x41 | (r << 3));
        }
        return -1;
    }
    /* === CB prefix: BIT/SET/RES and rotate/shift === */
    if (!strcasecmp(op, "BIT") || !strcasecmp(op, "SET") || !strcasecmp(op, "RES")) {
        int base;
        if (!strcasecmp(op, "BIT"))
            base = 0x40;
        else if (!strcasecmp(op, "RES"))
            base = 0x80;
        else
            base = 0xC0;

        if (!v1 || !o2) return -1;

        int bit = v1->num & 7;
        /* BIT/SET/RES b, (IX+d) */
        if (ind2 && (z80_is_ix(o2) || z80_is_iy(o2)) && ix2) {
            pre = z80_is_ix(o2) ? 0xDD : 0xFD;
            EMIT4(pre, 0xCB, ix2->num & 0xFF, base | (bit << 3) | 6);
        }
        /* BIT/SET/RES b, (HL) */
        if (ind2 && strcasecmp(o2, "HL") == 0) EMIT2(0xCB, base | (bit << 3) | 6);
        /* BIT/SET/RES b, r */
        s = z80_reg8(o2);
        if (IS_REG8(s))                        EMIT2(0xCB, base | (bit << 3) | s);
        return -1;
    }
    /* rotate/shift: RL RLC RR RRC SLA SRA SRL */
    {
        static const struct {const char*n;
        int code;} rot[] = {
            {"RLC", 0x00}, {"RRC", 0x08}, {"RL", 0x10}, {"RR", 0x18},
            {"SLA", 0x20}, {"SRA", 0x28}, {"SRL", 0x38}, {NULL, 0}
        };
        for (int i = 0; rot[i].n; i++) {
            if (strcasecmp(op, rot[i].n) != 0) continue;

            int base = rot[i].code;
            if (!o1) return -1;

            /* (IX+d) / (IY+d) */
            if (ind1 && (z80_is_ix(o1) || z80_is_iy(o1)) && ix1) {
                pre = z80_is_ix(o1) ? 0xDD : 0xFD;
                EMIT4(pre, 0xCB, ix1->num & 0xFF, base | 6);
            }
            /* (HL) */
            if (ind1 && strcasecmp(o1, "HL") == 0) EMIT2(0xCB, base | 6);
            /* r */
            r = z80_reg8(o1);
            if (IS_REG8(r) && !ind1)               EMIT2(0xCB, base | r);
            return -1;
        }
    }
    return -1; /* unknown */
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
