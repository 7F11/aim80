#ifndef CODEGEN_H
#define CODEGEN_H
#include "symtab.h"

/* encoded instruction — up to 4 bytes */
typedef struct {
    unsigned char bytes[6];
    int len;
    int reloc_pos;          /* byte offset of relocatable value (-1 if none) */
    int reloc_size;         /* 1 or 2 bytes */
    Value reloc_val;
    int has_reloc;
} Instruction;

/* 8080 register encoding */
int reg8080(const char *name);
int regpair(const char *name);

/* encode an 8080 instruction */
int encode_8080(const char *opcode, const char *op1, const char *op2,
                Value *val1, Value *val2, Instruction *inst);

/* Z80 register encoding */
int z80_reg8(const char *name);     /* B=0 C=1 D=2 E=3 H=4 L=5 (HL)=6 A=7 */
int z80_reg16(const char *name);    /* BC=0 DE=1 HL=2 SP=3 */
int z80_reg16af(const char *name);  /* BC=0 DE=1 HL=2 AF=3 (for PUSH/POP) */
int z80_cond(const char *name);     /* NZ=0 Z=1 NC=2 C=3 PO=4 PE=5 P=6 M=7 */
int z80_is_ix(const char *name);
int z80_is_iy(const char *name);

/* encode a Z80 instruction.
   op1/op2 are operand strings (register names, "(HL)", "(IX+n)", etc.)
   val1/val2 are expression values for immediate/address operands
   ind1/ind2: 1 if operand is indirect (parenthesized) */
int encode_z80(const char *opcode, const char *op1, const char *op2,
               Value *val1, Value *val2,
               int ind1, int ind2,
               Value *idx_off1, Value *idx_off2,
               Instruction *inst);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
