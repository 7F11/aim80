#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "codegen.h"

#define EQ(var,string)     (strcasecmp(var,string) == 0)
#define EQ_DEF(var,string) (var && EQ(var,string))
#define OP(string)         EQ(op,string)
#define O1(string)         EQ(o1,string)
#define O1D(string)        EQ_DEF(o1,string)
#define O2(string)         EQ(o2,string)
#define O2D(string)        EQ_DEF(o2,string)
#define SRC(string)        EQ_DEF(src,string)
#define IXY(var)           (pre = EQ_DEF(var,"IX") ? 0xDD : EQ_DEF(var, "IY") ? 0xFD : 0)

#define MATCH(name, val) if (EQ(n, name)) return (val)
int z80_reg8(const char *n) {
    if (!n) return -1;
    MATCH("B",   0);
    MATCH("C",   1);
    MATCH("D",   2);
    MATCH("E",   3);
    MATCH("H",   4);
    MATCH("L",   5);
    MATCH("A",   7);
    MATCH("HL",  6);  /* (HL) encoded as reg 6 */
    /* undocumented half-index registers (values > 7, not matched by IS_REG8) */
    MATCH("IXH", 8);
    MATCH("IXL", 9);
    MATCH("IYH",10);
    MATCH("IYL",11);
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
    MATCH("B",  0);
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
    return n && strcasecmp(n, "IX") == 0;
}

int z80_is_iy(const char *n) {
    return n && strcasecmp(n, "IY") == 0;
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
#define IS_REG8(r) ((r) >= 0 && (r) <= 7 && (r) != 6)

int encode_z80(int cpu_mode, const char *op, const char *o1, const char *o2,
               Value *v1, Value *v2, int ind1, int ind2,
               Value *ix1, Value *ix2, Instruction *inst) {
    memset(inst, 0, sizeof(*inst));
    inst->reloc_pos = -1;
    int r, s, rr, cc, is_dec = 0;
    unsigned char pre;

    /* === no-operand === */
    if (OP("NOP"))  EMIT1(0x00);
    if (OP("HALT")) EMIT1(0x76);
    if (OP("DI"))   EMIT1(0xF3);
    if (OP("EI"))   EMIT1(0xFB);
    if (OP("EXX"))  EMIT1(0xD9);
    if (OP("CCF"))  EMIT1(0x3F);
    if (OP("SCF"))  EMIT1(0x37);
    if (OP("DAA"))  EMIT1(0x27);
    if (OP("CPL"))  EMIT1(0x2F);
    if (OP("RLA"))  EMIT1(0x17);
    if (OP("RLCA")) EMIT1(0x07);
    if (OP("RRA"))  EMIT1(0x1F);
    if (OP("RRCA")) EMIT1(0x0F);
    if (OP("NEG"))  EMIT2(0xED, 0x44);
    if (OP("RETI")) EMIT2(0xED, 0x4D);
    if (OP("RETN")) EMIT2(0xED, 0x45);
    if (OP("RLD"))  EMIT2(0xED, 0x6F);
    if (OP("RRD"))  EMIT2(0xED, 0x67);

    /* block instructions */
    if (OP("LDI"))  EMIT2(0xED, 0xA0);
    if (OP("LDIR")) EMIT2(0xED, 0xB0);
    if (OP("LDD"))  EMIT2(0xED, 0xA8);
    if (OP("LDDR")) EMIT2(0xED, 0xB8);
    if (OP("CPI"))  EMIT2(0xED, 0xA1);
    if (OP("CPIR")) EMIT2(0xED, 0xB1);
    if (OP("CPD"))  EMIT2(0xED, 0xA9);
    if (OP("CPDR")) EMIT2(0xED, 0xB9);
    if (OP("INI"))  EMIT2(0xED, 0xA2);
    if (OP("INIR")) EMIT2(0xED, 0xB2);
    if (OP("IND"))  EMIT2(0xED, 0xAA);
    if (OP("INDR")) EMIT2(0xED, 0xBA);
    if (OP("OUTI")) EMIT2(0xED, 0xA3);
    if (OP("OTIR")) EMIT2(0xED, 0xB3);
    if (OP("OUTD")) EMIT2(0xED, 0xAB);
    if (OP("OTDR")) EMIT2(0xED, 0xBB);

    /* === LD === */
    if (OP("LD")) {
        /* LD r, r' */
        if (o1 && o2 && !ind1 && !ind2) {
            r = z80_reg8(o1);
            s = z80_reg8(o2);
            if (IS_REG8(r) && IS_REG8(s)) EMIT1(0x40 | (r << 3) | s);
            /* LD SP, HL */
            if (O1("SP") && O2("HL"))     EMIT1(0xF9);
            /* LD SP, IX/IY */
            if (O1("SP") && IXY(o2))      EMIT2(pre, 0xF9);
            /* LD A, I / LD A, R */
            if (O1("A") && O2("I"))       EMIT2(0xED, 0x57);
            if (O1("A") && O2("R"))       EMIT2(0xED, 0x5F);
            if (O1("I") && O2("A"))       EMIT2(0xED, 0x47);
            if (O1("R") && O2("A"))       EMIT2(0xED, 0x4F);
        }
        /* LD r, (HL) */
        if (o1 && !ind1 && ind2 && O2D("HL")) {
            r = z80_reg8(o1);
            if (IS_REG8(r))     EMIT1(0x46 | (r << 3));
        }
        /* LD (HL), r */
        if (ind1 && !ind2 && O1D("HL")) {
            s = z80_reg8(o2);
            if (IS_REG8(s))     EMIT1(0x70 | s);
            /* LD (HL), n */
            if (v2)             EMIT2(0x36, v2->num & 0xFF);
        }
        /* LD r, n (immediate) */
        if (!ind1 && v2 && !ind2) {
            r = z80_reg8(o1);
            if (IS_REG8(r)) EMIT_REL8(v2, 0x06 | (r << 3));
        }
        /* LD A, (BC)/(DE) */
        if (O1D("A") && o2 && ind2) {
            if (O2("BC")) EMIT1(0x0A);
            if (O2("DE")) EMIT1(0x1A);
        }
        /* LD (BC), A / LD (DE), A */
        if (o1 && ind1 && O2D("A")) {
            if (O1("BC")) EMIT1(0x02);
            if (O1("DE")) EMIT1(0x12);
        }
        /* LD A, (nn) */
        if (O1D("A") && ind2 && v2)  EMIT_REL16(v2, 0x3A);
        /* LD (nn), A */
        if (ind1 && v1 && O2D("A"))  EMIT_REL16(v1, 0x32);
        /* LD rr, nn */
        if (o1 && !ind1 && v2 && !ind2) {
            rr = z80_reg16(o1);
            if (rr >= 0)             EMIT_REL16(v2, 0x01 | (rr << 4));
            if (IXY(o1))             EMIT_REL16(v2, pre, 0x21);
        }
        /* LD HL, (nn) */
        if (O1D("HL") && !ind1 && ind2 && v2) EMIT_REL16(v2, 0x2A);
        /* LD (nn), HL */
        if (ind1 && v1 && O2D("HL")) EMIT_REL16(v1, 0x22);
        /* LD rr, (nn) - ED prefix */
        if (o1 && !ind1 && ind2 && v2) {
            rr = z80_reg16(o1);
            if (rr >= 0 && rr != 2)  EMIT_REL16(v2, 0xED, 0x4B | (rr << 4));
            if (IXY(o1))             EMIT_REL16(v2, pre, 0x2A);
        }
        /* LD (nn), rr - ED prefix */
        if (ind1 && v1 && o2 && !ind2) {
            rr = z80_reg16(o2);
            if (rr >= 0 && rr != 2)  EMIT_REL16(v1, 0xED, 0x43 | (rr << 4));
            if (IXY(o2))             EMIT_REL16(v1, pre, 0x22);
        }
        /* LD r, (IX+d)/(IY+d) */
        if (o1 && !ind1 && ind2 && o2 && IXY(o2) && ix2) {
            r = z80_reg8(o1);
            if (IS_REG8(r)) EMIT3(pre, 0x46 | (r << 3), ix2->num & 0xFF);
        }
        /* LD (IX+d), s / (IY+d), s */
        if (ind1 && o1 && IXY(o1) && ix1) {
            if (o2 && !ind2) {
                s = z80_reg8(o2);
                if (IS_REG8(s)) EMIT3(pre, 0x70 | s, ix1->num & 0xFF);
            }
            /* LD (IX+d), n */
            if (v2)             EMIT_REL8(v2, pre, 0x36, ix1->num & 0xFF);
        }
    }
    /* === PUSH/POP === */
    if (OP("PUSH")) {
        if (IXY(o1)) EMIT2(pre, 0xE5);
        rr = z80_reg16af(o1);
        if (rr >= 0) EMIT1(0xC5 | (rr << 4));
        return -1;
    }
    if (OP("POP")) {
        if (IXY(o1)) EMIT2(pre, 0xE1);
        rr = z80_reg16af(o1);
        if (rr >= 0) EMIT1(0xC1 | (rr << 4));
        return -1;
    }
    /* === ALU: ADD ADC SUB SBC AND OR XOR CP === */
    {
        static const struct {
            const char *n;
            int g;
        } alu[] = {
            {"ADD", 0}, {"ADC", 1}, {"SUB", 2}, {"SBC", 3},
            {"AND", 4}, {"XOR", 5}, {"OR", 6},  {"CP", 7}, {NULL, 0}
        };
        for (int i = 0; alu[i].n; i++) {
            if (!OP(alu[i].n)) continue;

            int g = alu[i].g;
            /* ADD HL, rr / ADC HL, rr / SBC HL, rr */
            if (o2 && O1D("HL")) {
                rr = z80_reg16(o2);
                if (rr >= 0) {
                    if (g == 0) EMIT1(0x09 | (rr << 4));
                    if (g == 1) EMIT2(0xED, 0x4A | (rr << 4));
                    if (g == 3) EMIT2(0xED, 0x42 | (rr << 4));
                }
            }
            /* ADD IX, rr / ADD IY, rr */
            if (o2 && g == 0 && IXY(o1)) {
                rr = z80_reg16(o2);
                if (rr >= 0) EMIT2(pre, 0x09 | (rr << 4));
            }
            /* ALU A, r or ALU r (implicit A) */
            int has_o2 = (o2 && o2[0]);
            int has_v2 = (v2 != NULL);
            /* two-operand form: ALU A, src */
            if (O1D("A") && (has_o2 || has_v2)) {
                /* second operand is the source */
                const char *src = has_o2 ? o2 : NULL;
                int src_ind = ind2;
                Value *src_ix = ix2;
                Value *src_v  = has_v2 ? v2 : NULL;
                /* ALU A, (IX+d) / (IY+d) */
                if (src_ind && IXY(src) && src_ix)
                    EMIT3(pre, 0x86 | (g << 3), src_ix->num & 0xFF);
                /* ALU A, (HL) */
                if (src && src_ind && EQ(src, "HL")) EMIT1(0x86 | (g << 3));
                /* ALU A, r */
                if (src) {
                    s = z80_reg8(src);
                    if (IS_REG8(s) && !src_ind)      EMIT1(0x80 | (g << 3) | s);
                }
                /* ALU A, n */
                if (src_v)                           EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                break;
            }
            /* single-operand form: ALU src (implicit A) */
            {
                const char *src = o1;
                int src_ind = ind1;
                Value *src_ix = ix1;
                Value *src_v  = v1;
                if (!src || !src[0]) {
                    if (src_v) EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                    break;
                }
                if (src_ind && IXY(src) && src_ix)
                    EMIT3(pre, 0x86 | (g << 3), src_ix->num & 0xFF);
                if (src_ind && EQ(src, "HL")) EMIT1(0x86 | (g << 3));
                s = z80_reg8(src);
                if (IS_REG8(s) && !src_ind)   EMIT1(0x80 | (g << 3) | s);
                if (src_v && !src_ind)        EMIT2(0xC6 | (g << 3), src_v->num & 0xFF);
                break;
            }
        }
    }
    /* === INC/DEC === */
    if (OP("INC") || (is_dec = OP("DEC"))) {
        if (!o1) return -1;
        /* INC/DEC (IX+d) */
        if (ind1 && IXY(o1) && ix1)
            EMIT3(pre, is_dec ? 0x35 : 0x34, ix1->num & 0xFF);
        /* INC/DEC r (check before rr since B/D/H match both) */
        r = z80_reg8(o1);
        if (IS_REG8(r) && !ind1) EMIT1((is_dec ? 0x05 : 0x04) | (r << 3));
        /* INC/DEC (HL) */
        if (ind1 && O1("HL"))    EMIT1(is_dec ? 0x35 : 0x34);
        /* INC/DEC rr */
        rr = z80_reg16(o1);
        if (!ind1) {
            if (rr >= 0)         EMIT1((is_dec ? 0x0B : 0x03) | (rr << 4));
            if (IXY(o1))         EMIT2(pre, is_dec ? 0x2B : 0x23);
        }
        return -1;
    }
    /* === JP === */
    if (OP("JP")) {
        if (ind1 && !o2) {
            if (O1D("HL")) EMIT1(0xE9);
            if (IXY(o1))   EMIT2(pre, 0xE9);
            /* JP (nn) - absolute jump */
            if (v1)        EMIT_REL16(v1, 0xC3);
        }
        /* JP cc, nn */
        if (o1 && v2) {
            cc = z80_cond(o1);
            if (cc >= 0)   EMIT_REL16(v2, 0xC2 | (cc << 3));
        }
        /* JP nn */
        if (v1 && !ind1)   EMIT_REL16(v1, 0xC3);

        if (cpu_mode & INST_SET_ZXNEXT) {
        /* JP (C) */
            if (ind2 && O1D("C")) EMIT2(0xED, 0x98);
        }

        return -1;
    }
    /* === JR === */
    if (OP("JR")) {
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
    if (OP("DJNZ")) {
        if (v1) EMIT2(0x10, v1->num & 0xFF);
        return -1;
    }
    /* === CALL === */
    if (OP("CALL")) {
        if (o1 && v2) {
            cc = z80_cond(o1);
            if (cc >= 0) EMIT_REL16(v2, 0xC4 | (cc << 3));
        }
        if (v1) EMIT_REL16(v1, 0xCD);
        return -1;
    }
    /* === RET === */
    if (OP("RET")) {
        if (!o1)     EMIT1(0xC9);
        cc = z80_cond(o1);
        if (cc >= 0) EMIT1(0xC0 | (cc << 3));
        return -1;
    }
    /* === RST === */
    if (OP("RST") && v1) {
        int n = v1->num;
        if (n <= 0x38 && (n & 7) == 0) EMIT1(0xC7 | n);
        /* also accept 0-7 */
        if (n <= 7) EMIT1(0xC7 | (n << 3));
        return -1;
    }
    /* === EX === */
    if (OP("EX")) {
        if (o1 && o2) {
            if (O1("DE") && O2("HL"))                EMIT1(0xEB);
            if (O1("AF") && (O2("AF'") || O2("AF"))) EMIT1(0x08);
            if (ind1 && O1("SP")) {
                if (O2("HL"))   EMIT1(0xE3);
                if (IXY(o2)) EMIT2(pre, 0xE3);
            }
        }
        return -1;
    }
    /* === IM === */
    if (OP("IM") && v1) {
        if (v1->num == 0) EMIT2(0xED, 0x46);
        if (v1->num == 1) EMIT2(0xED, 0x56);
        if (v1->num == 2) EMIT2(0xED, 0x5E);
        return -1;
    }
    /* === IN/OUT === */
    if (OP("IN")) {
        if (o1) {
            if (O1("A") && ind2 && v2) EMIT2(0xDB, v2->num & 0xFF);
            if (ind2 && O2("C")) {
                r = z80_reg8(o1);
                if (r >= 0)            EMIT2(0xED, 0x40 | (r << 3));
            }
        }
        return -1;
    }
    if (OP("OUT")) {
        if (o2) {
            if (ind1 && v1 && O2("A")) EMIT2(0xD3, v1->num & 0xFF);
            if (ind1 && O1("C")) {
                r = z80_reg8(o2);
                if (r >= 0)            EMIT2(0xED, 0x41 | (r << 3));
            }
        }
        return -1;
    }
    /* === CB prefix: BIT/SET/RES and rotate/shift === */
    if (OP("BIT") || OP("SET") || OP("RES")) {
        if (!v1 || !o2) return -1;

        int base = OP("BIT") ? 0x40 :
                   OP("RES") ? 0x80 :
                               0xC0;
        int bit = v1->num & 7;
        /* BIT/SET/RES b, (IX+d) */
        if (ind2 && IXY(o2) && ix2)
            EMIT4(pre, 0xCB, ix2->num & 0xFF, base | (bit << 3) | 6);
        /* BIT/SET/RES b, (HL) */
        if (ind2 && O2("HL")) EMIT2(0xCB, base | (bit << 3) | 6);
        /* BIT/SET/RES b, s */
        s = z80_reg8(o2);
        if (IS_REG8(s))       EMIT2(0xCB, base | (bit << 3) | s);
        return -1;
    }
    /* rotate/shift: RL RLC RR RRC SLA SRA SRL */
    {
        static const struct {
            const char *n;
            int code;
        } rot[] = {
            {"RLC", 0x00}, {"RRC", 0x08}, {"RL", 0x10}, {"RR", 0x18},
            {"SLA", 0x20}, {"SRA", 0x28}, {"SRL", 0x38}, {NULL, 0}
        };
        for (int i = 0; rot[i].n; i++) {
            if (!OP(rot[i].n)) continue;
            if (!o1) return -1;

            int base = rot[i].code;
            /* (IX+d) / (IY+d) */
            if (ind1 && IXY(o1) && ix1)
                EMIT4(pre, 0xCB, ix1->num & 0xFF, base | 6);
            /* (HL) */
            if (ind1 && O1("HL"))    EMIT2(0xCB, base | 6);
            /* s */
            s = z80_reg8(o1);
            if (IS_REG8(s) && !ind1) EMIT2(0xCB, base | s);
            return -1;
        }
    }


    /* === Z80 undocumented instructions (CPU_Z80U only) === */
    if (cpu_mode & INST_SET_Z80U) {
        /* SLL s (CB 30+s) */
        if (OP("SLL") && o1) {
            int s = z80_reg8(o1);
            if (IS_REG8(s)) EMIT2(0xCB, 0x30 | s);
        }
        /* LD IXH/IXL/IYH/IYL, s/n */
        if (OP("LD") && o1) {
            int ixr = -1, pre = 0;
            if      (O1("IXH")) { ixr = 4; pre = 0xDD; }
            else if (O1("IXL")) { ixr = 5; pre = 0xDD; }
            else if (O1("IYH")) { ixr = 4; pre = 0xFD; }
            else if (O1("IYL")) { ixr = 5; pre = 0xFD; }
            if (ixr >= 0) {
                if (v2) EMIT3(pre, 0x26 | ((ixr - 4) << 3), v2->num & 0xFF);
                int s = z80_reg8(o2);
                if (s >= 0 && s != 4 && s != 5 && s != 6)
                    EMIT2(pre, 0x60 | ((ixr - 4) << 3) | s);
            }
            /* LD r, IXH/IXL/IYH/IYL */
            if (o2) {
                int ixs = -1;
                pre = 0;
                if      (O2("IXH")) { ixs = 4; pre = 0xDD; }
                else if (O2("IXL")) { ixs = 5; pre = 0xDD; }
                else if (O2("IYH")) { ixs = 4; pre = 0xFD; }
                else if (O2("IYL")) { ixs = 5; pre = 0xFD; }
                if (ixs >= 0) {
                    int r = z80_reg8(o1);
                    if (r >= 0 && r != 4 && r != 5 && r != 6)
                        EMIT2(pre, 0x44 | (r << 3) | (ixs - 4));
                }
            }
        }
    }

    /* === Z180 extensions (CPU_Z180 only) === */
    if (cpu_mode & INST_SET_Z180) {
        if (OP("SLP"))   EMIT2(0xED, 0x76);
        if (OP("OTIM"))  EMIT2(0xED, 0x83);
        if (OP("OTDM"))  EMIT2(0xED, 0x8B);
        if (OP("OTIMR")) EMIT2(0xED, 0x93);
        if (OP("OTDMR")) EMIT2(0xED, 0x9B);
        /* MLT rr */
        if (OP("MLT") && o1) {
            int rr = z80_reg16(o1);
            if (rr >= 0) EMIT2(0xED, 0x4C | (rr << 4));
        }
        /* TST A,r  or  TST A,n */
        if (OP("TST") && O1D("A")) {
            if (o2) {
                int r = z80_reg8(o2);
                if (r >= 0) EMIT2(0xED, 0x04 | (r << 3));
            }
            if (v2) EMIT3(0xED, 0x64, v2->num & 0xFF);
        }
        /* TSTIO n */
        if (OP("TSTIO") && v1)
            EMIT3(0xED, 0x74, v1->num & 0xFF);
        /* IN0 r,(n) */
        if (OP("IN0") && o1 && v2 && ind2) {
            int r = z80_reg8(o1);
            if (r >= 0) EMIT3(0xED, 0x00 | (r << 3), v2->num & 0xFF);
        }
        /* OUT0 (n),r */
        if (OP("OUT0") && v1 && ind1 && o2) {
            int r = z80_reg8(o2);
            if (r >= 0) EMIT3(0xED, 0x01 | (r << 3), v1->num & 0xFF);
        }
    }


    /* === R800 extensions (CPU_R800 only) === */
    if (cpu_mode & INST_SET_R800) {
        /* MULUB A,r : ED C1+r*8 (r = B,C,D,E) */
        if (OP("MULUB") && o2 && O1D("A")) {
            int r = z80_reg8(o2);
            if (r >= 0 && r <= 3) EMIT2(0xED, 0xC1 | (r << 3));
        }
        /* MULUW HL,rr : ED C3+rr*16 (rr = BC,SP) */
        if (OP("MULUW") && o2 && O1D("HL")) {
            int rr = z80_reg16(o2);
            if (rr == 0) EMIT2(0xED, 0xC3);       /* BC */
            if (rr == 3) EMIT2(0xED, 0xF3);       /* SP */
        }
    }


    /* === ZX Spectrum Next extensions (CPU_ZXNEXT only) === */
    if (cpu_mode & INST_SET_ZXNEXT) {
        if (OP("MUL") && O1D("D") && O2D("E")) EMIT2(0xED, 0x30);
        if (OP("SWAPNIB"))                     EMIT2(0xED, 0x23);
        if (OP("MIRROR") && O1D("A"))          EMIT2(0xED, 0x24);
        if (OP("PIXELDN"))                     EMIT2(0xED, 0x93);
        if (OP("PIXELAD"))                     EMIT2(0xED, 0x94);
        if (OP("SETAE"))                       EMIT2(0xED, 0x95);
        if (OP("OUTINB"))                      EMIT2(0xED, 0x90);
        if (OP("LDIX"))                        EMIT2(0xED, 0xA4);
        if (OP("LDDX"))                        EMIT2(0xED, 0xAC);
        if (OP("LDIRX"))                       EMIT2(0xED, 0xB4);
        if (OP("LDDRX"))                       EMIT2(0xED, 0xBC);
        if (OP("LDPIRX"))                      EMIT2(0xED, 0xB7);
        if (OP("LDIRSCALE"))                   EMIT2(0xED, 0xB6);
        if (O1D("DE") && O2D("B")) {
            if (OP("BSLA"))                    EMIT2(0xED, 0x28);
            if (OP("BSRA"))                    EMIT2(0xED, 0x29);
            if (OP("BSRL"))                    EMIT2(0xED, 0x2A);
            if (OP("BSRF"))                    EMIT2(0xED, 0x2B);
            if (OP("BRLC"))                    EMIT2(0xED, 0x2C);
        }
        /* TEST nn */
        if (OP("TEST") && v1)
            EMIT3(0xED, 0x27, v1->num & 0xFF);
        /* NEXTREG r,n */
        if (OP("NEXTREG") && v1 && v2)
            EMIT4(0xED, 0x91, v1->num & 0xFF, v2->num & 0xFF);
        /* NEXTREG r,A */
        if (OP("NEXTREG") && v1 && O2D("A"))
            EMIT3(0xED, 0x92, v1->num & 0xFF);
        /* ADD HL/DE/BC,A */
        if (OP("ADD") && o1 && O2D("A")) {
            if (O1("HL")) EMIT2(0xED, 0x31);
            if (O1("DE")) EMIT2(0xED, 0x32);
            if (O1("BC")) EMIT2(0xED, 0x33);
        }
        /* ADD HL/DE/BC,nn */
        if (OP("ADD") && o1 && v2 && !o2) {
            if (O1("HL")) EMIT4(0xED, 0x34, v2->num & 0xFF, (v2->num >> 8) & 0xFF);
            if (O1("DE")) EMIT4(0xED, 0x35, v2->num & 0xFF, (v2->num >> 8) & 0xFF);
            if (O1("BC")) EMIT4(0xED, 0x36, v2->num & 0xFF, (v2->num >> 8) & 0xFF);
        }
        /* PUSH nn (large immediate) */
        if (OP("PUSH") && v1 && !o1)
            EMIT4(0xED, 0x8A, (v1->num >> 8) & 0xFF, v1->num & 0xFF);
    }

    return -1; /* unknown */
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
