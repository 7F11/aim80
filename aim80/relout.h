#ifndef RELOUT_H
#define RELOUT_H
#include <stdio.h>
#include "symtab.h"

typedef struct {
    FILE *fp;
    unsigned char buf;  /* current byte being built */
    int bitpos;         /* bits written in current byte (0-7) */
    /* buffer mode: if membuf != NULL, write to membuf instead of fp */
    unsigned char *membuf;
    int membuf_len;
    int membuf_cap;
} RelOut;

void rel_init(RelOut *r, FILE *fp);
void rel_init_buf(RelOut *r, unsigned char *buf, int cap);
void rel_flush(RelOut *r);

/* write a single bit */
void rel_bit(RelOut *r, int b);
/* write n bits from value (MSB first) */
void rel_bits(RelOut *r, int value, int nbits);

/* emit an absolute byte */
void rel_abs_byte(RelOut *r, int byte);
/* emit a relocatable 16-bit value */
void rel_reloc_word(RelOut *r, int value, ValMode mode);

/* special link items */
void rel_entry_symbol(RelOut *r, const char *name);
void rel_select_common(RelOut *r, const char *name);
void rel_program_name(RelOut *r, const char *name);
void rel_define_entry(RelOut *r, const char *name, int value, ValMode mode);
void rel_chain_external(RelOut *r, const char *name, int head, ValMode mode);
void rel_ext_plus_offset(RelOut *r, const char *name, int offset, ValMode mode);
void rel_define_common_size(RelOut *r, const char *name, int size);
void rel_define_data_size(RelOut *r, int size);
void rel_define_code_size(RelOut *r, int size);
void rel_set_loc(RelOut *r, int addr, ValMode mode);
void rel_chain_address(RelOut *r, int head, ValMode mode);
void rel_end_program(RelOut *r, int has_start, int start_addr, ValMode start_mode);
void rel_end_file(RelOut *r);

#endif
void rel_ext_operand(RelOut *r, int value, ValMode mode);
void rel_ext_operator(RelOut *r, int op);
/* Extension operators: 1=BYTE(LOW), 2=WORD, 3=HIGH, 4=LOW */
void rel_ext_operand_ext(RelOut *r, const char *name);
void rel_request_library(RelOut *r, const char *name);

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
