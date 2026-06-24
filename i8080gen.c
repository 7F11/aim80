#define _GNU_SOURCE
#include <string.h>
#include <ctype.h>
#include "codegen.h"

////////////////////////////////////////////////
// NOTE: REG macro includes a return 0 statement
////////////////////////////////////////////////

#define MATCH(n, v) if (strcasecmp(name, n) == 0) return v
/* 8080 register codes: B=0 C=1 D=2 E=3 H=4 L=5 M=6 A=7 */
int reg8080(const char *name) {
    if (!name) return -1;
    MATCH("B", 0);
    MATCH("C", 1);
    MATCH("D", 2);
    MATCH("E", 3);
    MATCH("H", 4);
    MATCH("L", 5);
    MATCH("M", 6);
    MATCH("A", 7);
    return -1;
}

/* register pair codes: B=0(BC) D=1(DE) H=2(HL) SP/PSW=3 */
int regpair(const char *name) {
    if (!name) return -1;
    MATCH("B",  0); MATCH("BC", 0);
    MATCH("D",  1); MATCH("DE", 1);
    MATCH("H",  2); MATCH("HL", 2);
    MATCH("SP", 3); MATCH("PSW", 3);
    return -1;
}

static void inst1(Instruction *inst, int b) {
    inst->bytes[0] = b & 0xFF;
    inst->len = 1;
    inst->has_reloc = 0;
    inst->reloc_pos = -1;
}

static void inst2(Instruction *inst, int b, Value *v) {
    inst->bytes[0] = b & 0xFF;
    inst->bytes[1] = v->num & 0xFF;
    inst->len = 2;
    if (!val_is_absolute(*v)) {
        inst->has_reloc = 1;
        inst->reloc_pos = 1;
        inst->reloc_val = *v;
    }
    else {
        inst->has_reloc = 0;
        inst->reloc_pos = -1;
    }
}

static void inst3(Instruction *inst, int b, Value *v) {
    inst->bytes[0] = b & 0xFF;
    inst->bytes[1] = v->num & 0xFF;
    inst->bytes[2] = (v->num >> 8) & 0xFF;
    inst->len = 3;
    if (!val_is_absolute(*v)) {
        inst->has_reloc = 1;
        inst->reloc_pos = 1;
        inst->reloc_size = 2;
        inst->reloc_val = *v;
    }
    else {
        inst->has_reloc = 0;
        inst->reloc_pos = -1;
    }
}

//////////////////////////////////////////////////
// NOTE: EMIT* macros include a return 0 statement
//////////////////////////////////////////////////

#define EMIT1(b1)     do { inst1(inst, b1);     return 0; } while(0)
#define EMIT2(b1, b2) do { inst2(inst, b1, b2); return 0; } while(0)
#define EMIT3(b1, v)  do { inst3(inst, b1, v);  return 0; } while(0)
#define OP(name) (strcasecmp(opcode, name) == 0)

int encode_8080(int cpu_mode, const char *opcode, const char *op1, const char *op2,
                Value *val1, Value *val2, Instruction *inst) {
    memset(inst, 0, sizeof(*inst));
    inst->reloc_pos = -1;

    /* no-operand instructions */
    if (OP("NOP"))  EMIT1(0x00);
    if (OP("HLT"))  EMIT1(0x76);
    if (OP("RET"))  EMIT1(0xC9);
    if (OP("RLC"))  EMIT1(0x07);
    if (OP("RRC"))  EMIT1(0x0F);
    if (OP("RAL"))  EMIT1(0x17);
    if (OP("RAR"))  EMIT1(0x1F);
    if (OP("DAA"))  EMIT1(0x27);
    if (OP("CMA"))  EMIT1(0x2F);
    if (OP("STC"))  EMIT1(0x37);
    if (OP("CMC"))  EMIT1(0x3F);
    if (OP("XCHG")) EMIT1(0xEB);
    if (OP("XTHL")) EMIT1(0xE3);
    if (OP("SPHL")) EMIT1(0xF9);
    if (OP("PCHL")) EMIT1(0xE9);
    if (OP("EI"))   EMIT1(0xFB);
    /* 8085 extensions */
    if (cpu_mode & INST_SET_8085) {
        if (OP("RIM"))  EMIT1(0x20);
        if (OP("SIM"))  EMIT1(0x30);
    }
    if (OP("DI"))   EMIT1(0xF3);
    /* conditional returns: RC RNC RZ RNZ RP RM RPE RPO */
    if (OP("RNZ"))  EMIT1(0xC0);
    if (OP("RZ"))   EMIT1(0xC8);
    if (OP("RNC"))  EMIT1(0xD0);
    if (OP("RC"))   EMIT1(0xD8);
    if (OP("RPO"))  EMIT1(0xE0);
    if (OP("RPE"))  EMIT1(0xE8);
    if (OP("RP"))   EMIT1(0xF0);
    if (OP("RM"))   EMIT1(0xF8);
    /* MOV r, r */
    if (OP("MOV") && op1 && op2) {
        int d = reg8080(op1), s = reg8080(op2);
        if (d >= 0 && s >= 0) EMIT1(0x40 | (d << 3) | s);
        return -1;
    }
    /* MVI r, imm8 */
    if (OP("MVI") && op1 && val2) {
        int d = reg8080(op1);
        if (d >= 0) EMIT2(0x06 | (d << 3), val2);
        return -1;
    }
    /* accumulator ALU: ADD ADC SUB SBB ANA XRA ORA CMP */
    {
        static const struct { const char *name; int base; } alu[] = {
            {"ADD", 0x80}, {"ADC", 0x88}, {"SUB", 0x90}, {"SBB", 0x98},
            {"ANA", 0xA0}, {"XRA", 0xA8}, {"ORA", 0xB0}, {"CMP", 0xB8},
            {NULL, 0}
        };
        for (int i = 0; alu[i].name; i++)
            if (OP(alu[i].name) && op1) {
                int r = reg8080(op1);
                if (r >= 0) EMIT1(alu[i].base | r);
                return -1;
            }
    }
    /* immediate ALU: ADI ACI SUI SBI ANI XRI ORI CPI */
    {
        static const struct { const char *name; int op; } imm[] = {
            {"ADI", 0xC6}, {"ACI", 0xCE}, {"SUI", 0xD6}, {"SBI", 0xDE},
            {"ANI", 0xE6}, {"XRI", 0xEE}, {"ORI", 0xF6}, {"CPI", 0xFE},
            {NULL, 0}
        };
        for (int i = 0; imm[i].name; i++)
            if (OP(imm[i].name) && val1) EMIT2(imm[i].op, val1);
    }
    /* INR DCR */
    if (OP("INR") && op1) {
        int r = reg8080(op1);
        if (r >= 0) EMIT1(0x04 | (r << 3));
        return -1;
    }
    if (OP("DCR") && op1) {
        int r = reg8080(op1);
        if (r >= 0) EMIT1(0x05 | (r << 3));
        return -1;
    }
    /* register pair ops: INX DCX DAD PUSH POP */
    if (OP("INX") && op1) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT1(0x03 | (rp << 4));
        return -1;
    }
    if (OP("DCX") && op1) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT1(0x0B | (rp << 4));
        return -1;
    }
    if (OP("DAD") && op1) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT1(0x09 | (rp << 4));
        return -1;
    }
    if (OP("PUSH") && op1) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT1(0xC5 | (rp << 4));
        return -1;
    }
    if (OP("POP") && op1) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT1(0xC1 | (rp << 4));
        return -1;
    }
    /* LXI rp, imm16 */
    if (OP("LXI") && op1 && val2) {
        int rp = regpair(op1);
        if (rp >= 0) EMIT3(0x01 | (rp << 4), val2);
        return -1;
    }
    /* LDAX STAX */
    if (OP("LDAX") && op1) {
        int rp = regpair(op1);
        if (rp == 0) EMIT1(0x0A);
        if (rp == 1) EMIT1(0x1A);
        return -1;
    }
    if (OP("STAX") && op1) {
        int rp = regpair(op1);
        if (rp == 0) EMIT1(0x02);
        if (rp == 1) EMIT1(0x12);
        return -1;
    }
    /* 3-byte address instructions */
    if (val1) {
        if (OP("JMP"))  EMIT3(0xC3, val1);
        if (OP("CALL")) EMIT3(0xCD, val1);
        if (OP("LDA"))  EMIT3(0x3A, val1);
        if (OP("STA"))  EMIT3(0x32, val1);
        if (OP("LHLD")) EMIT3(0x2A, val1);
        if (OP("SHLD")) EMIT3(0x22, val1);
        /* conditional jumps */
        if (OP("JNZ"))  EMIT3(0xC2, val1);
        if (OP("JZ"))   EMIT3(0xCA, val1);
        if (OP("JNC"))  EMIT3(0xD2, val1);
        if (OP("JC"))   EMIT3(0xDA, val1);
        if (OP("JPO"))  EMIT3(0xE2, val1);
        if (OP("JPE"))  EMIT3(0xEA, val1);
        if (OP("JP"))   EMIT3(0xF2, val1);
        if (OP("JM"))   EMIT3(0xFA, val1);
        /* conditional calls */
        if (OP("CNZ"))  EMIT3(0xC4, val1);
        if (OP("CZ"))   EMIT3(0xCC, val1);
        if (OP("CNC"))  EMIT3(0xD4, val1);
        if (OP("CC"))   EMIT3(0xDC, val1);
        if (OP("CPO"))  EMIT3(0xE4, val1);
        if (OP("CPE"))  EMIT3(0xEC, val1);
        if (OP("CP"))   EMIT3(0xF4, val1);
        if (OP("CM"))   EMIT3(0xFC, val1);
        /* IN OUT */
        if (OP("IN"))   EMIT2(0xDB, val1);
        if (OP("OUT"))  EMIT2(0xD3, val1);
        /* RST n */
        if (OP("RST")) {
            int n = val1->num & 7;
            EMIT1(0xC7 | (n << 3));
        }
    }
    return -1; /* unknown opcode */
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
