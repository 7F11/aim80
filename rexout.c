#include <string.h>
#include "rexout.h"

void rex_init(RexOut *r, FILE *fp) {
    memset(r, 0, sizeof(*r));
    r->fp       = fp;
    r->code_seg = -1;
}

/* write a TLV record: tag(1) + length(2 LE) + payload */
static void write_record(RexOut *r, int tag, const unsigned char *data, int len) {
    fputc(tag & 0xFF, r->fp);
    fputc(len & 0xFF, r->fp);
    fputc((len >> 8) & 0xFF, r->fp);
    if (len > 0) fwrite(data, 1, len, r->fp);
}

static void write_u16(unsigned char *buf, int val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

int valmode_to_rex_seg(ValMode m) {
    switch (m) {
    case MODE_ABSOLUTE:  return REX_SEG_ABS;
    case MODE_CODE_REL:  return REX_SEG_CODE;
    case MODE_DATA_REL:  return REX_SEG_DATA;
    case MODE_COMMON:    return REX_SEG_COMMON;
    }
    return REX_SEG_ABS;
}

void rex_write_header(RexOut *r) {
    /* magic + version + flags + address size */
    unsigned char hdr[6] = { 'R', 'E', 'X', 0x01, 0x00, 0x02 };
    fwrite(hdr, 1, 6, r->fp);
}

void rex_flush_code(RexOut *r) {
    if (r->codelen <= 0) return;

    /* CODE_BYTES: seg_type(1) + offset(2) + data(n) */
    unsigned char buf[259];
    buf[0] = r->code_seg & 0xFF;
    write_u16(buf + 1, r->code_offset);
    memcpy(buf + 3, r->codebuf, r->codelen);
    write_record(r, REX_CODE_BYTES, buf, 3 + r->codelen);
    r->codelen  = 0;
    r->code_seg = -1;
}

void rex_module_name(RexOut *r, const char *name) {
    int len = (int)strlen(name) + 1; /* include null */
    write_record(r, REX_MODULE_NAME, (const unsigned char *)name, len);
}

void rex_code_byte(RexOut *r, int byte, int seg_type, int offset) {
    /* accumulate consecutive bytes in same segment */
    if (r->codelen > 0 &&
           (seg_type != r->code_seg ||
           offset != r->code_offset + r->codelen ||
           r->codelen >= 250)
    ) rex_flush_code(r);

    if (r->codelen == 0) {
        r->code_seg    = seg_type;
        r->code_offset = offset;
    }
    r->codebuf[r->codelen++] = byte & 0xFF;
}

void rex_set_loc(RexOut *r, int seg_type, int offset) {
    rex_flush_code(r);
    unsigned char buf[3];
    buf[0] = seg_type;
    write_u16(buf + 1, offset);
    write_record(r, REX_SET_LOC, buf, 3);
}

void rex_entry_symbol(RexOut *r, const char *name, int seg_type, int value) {
    rex_flush_code(r);
    int nlen = (int)strlen(name) + 1;
    unsigned char buf[256];
    memcpy(buf, name, nlen);
    buf[nlen] = seg_type;
    write_u16(buf + nlen + 1, value);
    write_record(r, REX_ENTRY_SYMBOL, buf, nlen + 3);
}

void rex_extern_ref(RexOut *r, const char *name, int seg_type, int chain_head) {
    rex_flush_code(r);
    int nlen = (int)strlen(name) + 1;
    unsigned char buf[256];
    memcpy(buf, name, nlen);
    buf[nlen] = seg_type;
    write_u16(buf + nlen + 1, chain_head);
    write_record(r, REX_EXTERN_REF, buf, nlen + 3);
}

void rex_chain_addr(RexOut *r, int seg_type, int head) {
    rex_flush_code(r);
    unsigned char buf[3];
    buf[0] = seg_type;
    write_u16(buf + 1, head);
    write_record(r, REX_CHAIN_ADDR, buf, 3);
}

void rex_common_def(RexOut *r, const char *name, int size) {
    rex_flush_code(r);
    int nlen = (int)strlen(name) + 1;
    unsigned char buf[256];
    memcpy(buf, name, nlen);
    write_u16(buf + nlen, size);
    write_record(r, REX_COMMON_DEF, buf, nlen + 2);
}

void rex_data_size(RexOut *r, int size) {
    rex_flush_code(r);
    unsigned char buf[2];
    write_u16(buf, size);
    write_record(r, REX_DATA_SIZE, buf, 2);
}

void rex_code_size(RexOut *r, int size) {
    rex_flush_code(r);
    unsigned char buf[2];
    write_u16(buf, size);
    write_record(r, REX_CODE_SIZE, buf, 2);
}

void rex_end_module(RexOut *r, int has_start, int seg_type, int start_addr) {
    rex_flush_code(r);
    unsigned char buf[4];
    buf[0] = has_start ? 1 : 0;
    buf[1] = seg_type;
    write_u16(buf + 2, start_addr);
    write_record(r, REX_END_MODULE, buf, 4);
}

void rex_eof(RexOut *r) {
    rex_flush_code(r);
    write_record(r, REX_EOF, NULL, 0);
}

void rex_source_file(RexOut *r, const char *name) {
    int len = (int)strlen(name) + 1;
    write_record(r, REX_SOURCE_FILE, (const unsigned char *)name, len);
}

void rex_line_number(RexOut *r, int line, int offset) {
    unsigned char buf[4];
    write_u16(buf, line);
    write_u16(buf + 2, offset);
    write_record(r, REX_LINE_NUMBER, buf, 4);
}

void rex_comment(RexOut *r, const char *text) {
    int len = (int)strlen(text) + 1;
    write_record(r, REX_COMMENT, (const unsigned char *)text, len);
}

/* Record 0x0D: RELOC_FIXUP - inter-segment word relocation */
void rex_reloc_fixup(RexOut *r, int seg_at, int off_at, int seg_to, int fixup_type, int value) {
    rex_flush_code(r);
    unsigned char rec[10] = {
        REX_RELOC_FIXUP,        /* record type */
        7, 0,                   /* payload length: 7 bytes */
        seg_at,                 /* segment containing the word */
        off_at & 0xFF,          /* offset of word (low byte) */
        (off_at >> 8) & 0xFF,   /* offset of word (high byte) */
        seg_to,                 /* target segment */
        fixup_type,             /* 0=word, 1=low, 2=high */
        value & 0xFF,           /* pre-relocation value (low byte) */
        (value >> 8) & 0xFF    /* pre-relocation value (high byte) */
    };
    fwrite(rec, 1, sizeof(rec), r->fp);
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
