#include <string.h>
#include "relout.h"

static void rel_putc(RelOut *r, int c) {
    if (r->membuf) {
        if (r->membuf_len < r->membuf_cap)
            r->membuf[r->membuf_len++] = (unsigned char)c;
    }
    else
        fputc(c, r->fp);
}

void rel_init(RelOut *r, FILE *fp) {
    memset(r, 0, sizeof(*r));
    r->fp = fp;
}

void rel_init_buf(RelOut *r, unsigned char *buf, int cap) {
    memset(r, 0, sizeof(*r));
    r->membuf     = buf;
    r->membuf_cap = cap;
}

void rel_flush(RelOut *r) {
    if (r->bitpos > 0) {
        rel_putc(r, r->buf);
        r->buf    = 0;
        r->bitpos = 0;
    }
}

void rel_bit(RelOut *r, int b) {
    if (b) r->buf |= (1 << (7 - r->bitpos));
    r->bitpos++;
    if (r->bitpos >= 8) {
        rel_putc(r, r->buf);
        r->buf    = 0;
        r->bitpos = 0;
    }
}

void rel_bits(RelOut *r, int value, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) rel_bit(r, (value >> i) & 1);
}

/* 8 bits, LSB first within the byte but MSB-first bit ordering */
static void rel_byte(RelOut *r, int byte) {
    rel_bits(r, byte & 0xFF, 8);
}

static void rel_word(RelOut *r, int word) {
    rel_byte(r, word & 0xFF);
    rel_byte(r, (word >> 8) & 0xFF);
}

/* A field: 2-bit address type + 16-bit value */
static void rel_a_field(RelOut *r, int value, ValMode mode) {
    int type;
    switch (mode) {
    case MODE_ABSOLUTE:  type = 0; break;
    case MODE_CODE_REL:  type = 1; break;
    case MODE_DATA_REL:  type = 2; break;
    case MODE_COMMON:    type = 3; break;
    default:             type = 0; break;
    }
    rel_bits(r, type, 2);
    rel_word(r, value);
}

/* B field: 3-bit length + chars */
static void rel_b_field(RelOut *r, const char *name) {
    int len = (int)strlen(name);
    if (len > 6) len = 6;
    rel_bits(r, len, 3);
    for (int i = 0; i < len; i++) rel_byte(r, name[i] & 0x7F);
}

void rel_abs_byte(RelOut *r, int byte) {
    rel_bit(r, 0);             /* absolute item */
    rel_byte(r, byte & 0xFF);
}

void rel_reloc_word(RelOut *r, int value, ValMode mode) {
    rel_bit(r, 1);             /* relocatable item */
    int type;
    switch (mode) {
    case MODE_CODE_REL: type = 1; break;
    case MODE_DATA_REL: type = 2; break;
    case MODE_COMMON:   type = 3; break;
    default:            type = 0; break;
    }
    rel_bits(r, type, 2);
    rel_word(r, value);
}

/* special link item header: 1 00 xxxx */
static void rel_special(RelOut *r, int ctrl) {
    rel_bit(r, 1);
    rel_bits(r, 0, 2);         /* 00 = special */
    rel_bits(r, ctrl, 4);
}

/* type 0: entry symbol (B only) */
void rel_entry_symbol(RelOut *r, const char *name) {
    rel_special(r, 0);
    rel_b_field(r, name);
}

/* type 1: select common (B only) */
void rel_select_common(RelOut *r, const char *name) {
    rel_special(r, 1);
    rel_b_field(r, name);
}

/* type 2: program name (B only) */
void rel_program_name(RelOut *r, const char *name) {
    rel_special(r, 2);
    rel_b_field(r, name);
}

/* type 5: define common size (A + B) */
void rel_define_common_size(RelOut *r, const char *name, int size) {
    rel_special(r, 5);
    rel_a_field(r, size, MODE_ABSOLUTE);
    rel_b_field(r, name);
}

/* type 6: chain external (A + B) */
void rel_chain_external(RelOut *r, const char *name, int head, ValMode mode) {
    rel_special(r, 6);
    rel_a_field(r, head, mode);
    rel_b_field(r, name);
}

/* type 9: external + offset (A field only) */
void rel_ext_plus_offset(RelOut *r, const char *name, int offset, ValMode mode) {
    (void)name;
    rel_special(r, 9);
    rel_a_field(r, offset, mode);
}

/* type 7: define entry point (A + B) */
void rel_define_entry(RelOut *r, const char *name, int value, ValMode mode) {
    rel_special(r, 7);
    rel_a_field(r, value, mode);
    rel_b_field(r, name);
}

/* type 10: define data size (A only) */
void rel_define_data_size(RelOut *r, int size) {
    rel_special(r, 10);
    rel_a_field(r, size, MODE_ABSOLUTE);
}

/* type 11: set location counter (A only) */
void rel_set_loc(RelOut *r, int addr, ValMode mode) {
    rel_special(r, 11);
    rel_a_field(r, addr, mode);
}

/* type 12: chain address (A only) */
void rel_chain_address(RelOut *r, int head, ValMode mode) {
    rel_special(r, 12);
    rel_a_field(r, head, mode);
}

/* type 13: define program size (A only) */
void rel_define_code_size(RelOut *r, int size) {
    rel_special(r, 13);
    rel_a_field(r, size, MODE_CODE_REL);
}

/* type 14: end program (A only) — forces byte boundary */
void rel_end_program(RelOut *r, int has_start, int start_addr, ValMode start_mode) {
    rel_special(r, 14);
    if (has_start) rel_a_field(r, start_addr, start_mode);
    else           rel_a_field(r, 0, MODE_ABSOLUTE);
    rel_flush(r);
}

/* type 15: end file */
void rel_end_file(RelOut *r) {
    rel_special(r, 15);
    rel_flush(r);
    /* M80 writes CP/M EOF marker (0x1A) after EndFile */
    rel_byte(r, 0x1A);
    rel_flush(r);
}

/* type 4: extension link item - operand (value with address type) */
void rel_ext_operand(RelOut *r, int value, ValMode mode) {
    int atype;
    switch (mode) {
    case MODE_ABSOLUTE:  atype = 0; break;
    case MODE_CODE_REL:  atype = 1; break;
    case MODE_DATA_REL:  atype = 2; break;
    case MODE_COMMON:    atype = 3; break;
    default:             atype = 0; break;
    }
    rel_special(r, 4);
    rel_bits(r, 4, 3);  /* B_len = 4 */
    rel_byte(r, 'C');
    rel_byte(r, atype);
    rel_byte(r, value & 0xFF);
    rel_byte(r, (value >> 8) & 0xFF);
}

/* type 4: extension link item - operator */
void rel_ext_operator(RelOut *r, int op) {
    rel_special(r, 4);
    rel_bits(r, 2, 3);  /* B_len = 2 */
    rel_byte(r, 'A');
    rel_byte(r, op);
}

/* type 4: extension link item - operand (external symbol) */
void rel_ext_operand_ext(RelOut *r, const char *name) {
    rel_special(r, 4);
    int len = (int)strlen(name);
    if (len > 6) len = 6;
    rel_bits(r, 1 + len, 3);  /* B_len = 1 + name_len */
    rel_byte(r, 'B');
    for (int i = 0; i < len; i++) rel_byte(r, name[i] & 0x7F);
}

/* type 11: request library search */
void rel_request_library(RelOut *r, const char *name) {
    rel_special(r, 3);
    rel_b_field(r, name);
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
