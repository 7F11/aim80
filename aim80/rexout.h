#ifndef REXOUT_H
#define REXOUT_H
#include <stdio.h>
#include "symtab.h"

/* record types */
#define REX_MODULE_NAME   0x01
#define REX_CODE_BYTES    0x02
#define REX_SET_LOC       0x03
#define REX_ENTRY_SYMBOL  0x04
#define REX_EXTERN_REF    0x05
#define REX_CHAIN_ADDR    0x06
#define REX_COMMON_DEF    0x07
#define REX_DATA_SIZE     0x08
#define REX_CODE_SIZE     0x09
#define REX_END_MODULE    0x0A
#define REX_SELECT_COMMON 0x0B
#define REX_LIB_SEARCH    0x0C
#define REX_RELOC_FIXUP   0x0D
#define REX_LONG_SYMBOL   0x10
#define REX_SOURCE_FILE   0x11
#define REX_LINE_NUMBER   0x12
#define REX_SECTION_ALIGN 0x14
#define REX_COMMENT       0x15
#define REX_EOF           0xFF

/* segment type encoding in records */
#define REX_SEG_ABS    0
#define REX_SEG_CODE   1
#define REX_SEG_DATA   2
#define REX_SEG_COMMON 3

typedef struct {
    FILE *fp;
    /* code byte accumulator */
    unsigned char codebuf[256];
    int codelen;
    int code_seg;       /* segment type of accumulated bytes */
    int code_offset;    /* starting offset */
} RexOut;

void rex_init(RexOut *r, FILE *fp);
void rex_write_header(RexOut *r);
void rex_flush_code(RexOut *r);

void rex_module_name(RexOut *r, const char *name);
void rex_code_byte(RexOut *r, int byte, int seg_type, int offset);
void rex_set_loc(RexOut *r, int seg_type, int offset);
void rex_entry_symbol(RexOut *r, const char *name, int seg_type, int value);
void rex_extern_ref(RexOut *r, const char *name, int seg_type, int chain_head);
void rex_chain_addr(RexOut *r, int seg_type, int head);
void rex_common_def(RexOut *r, const char *name, int size);
void rex_data_size(RexOut *r, int size);
void rex_code_size(RexOut *r, int size);
void rex_end_module(RexOut *r, int has_start, int seg_type, int start_addr);
void rex_eof(RexOut *r);

/* extensions */
void rex_source_file(RexOut *r, const char *name);
void rex_line_number(RexOut *r, int line, int offset);
void rex_comment(RexOut *r, const char *text);

int valmode_to_rex_seg(ValMode m);

#endif
void rex_reloc_fixup(RexOut *r, int seg_at, int off_at, int seg_to, int fixup_type, int value);

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
