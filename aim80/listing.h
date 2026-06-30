#ifndef LISTING_H
#define LISTING_H
#include <stdio.h>
#include "symtab.h"
#include "segment.h"

#define LST_MAX_BYTES 6    /* max hex bytes shown per line */

typedef struct {
    FILE *fp;
    int enabled;           /* listing active */
    int page_size;         /* lines per page, default 50 */
    int page_num;          /* major page number */
    int page_sub;          /* sub-page (0=none, 1+=after PAGE directive) */
    int line_on_page;      /* lines printed on current page */
    char title[64];
    char subtitle[64];
    char pending_subtitle[64];
    int hex_mode;          /* 0=hex (default), 1=octal */

    /* per-statement accumulator */
    int stmt_addr;         /* address at start of statement */
    ValMode stmt_mode;     /* segment mode */
    int stmt_common_id;
    unsigned char stmt_bytes[32];
    char stmt_modes[32];   /* mode char for each byte: ' '=abs, '\''=code, '"'=data, '!'=common, 0=none */
    int stmt_nbytes;
    char stmt_source[512];
    char stmt_file[256];
    int stmt_line;
    int stmt_from_include;
    int stmt_from_macro;
    int stmt_suppressed;
    int stmt_has_value;    /* EQU/SET: always show address field */
    int stmt_has_label;    /* line defines a label: show address */
    char stmt_error;       /* error flag char (U, Q, O, etc.) or 0 */
    int macro_list_mode;   /* 0=.XALL (default), 1=.LALL, 2=.SALL */
    int list_false_cond;   /* 0=suppress (.SFCOND), 1=list (.LFCOND, default) */
    /* cross-reference */
    int xref_enabled;
    int xref_count;
    struct xref_entry {
        char name[17];
        int line;
        int is_def;
    } *xref;
    int xref_cap;
} Listing;

void lst_xref_add(Listing *lst, const char *name, int line, int is_def);
void lst_xref_output(Listing *lst);

void lst_init(Listing *lst, FILE *fp);
void lst_set_title(Listing *lst, const char *title);
void lst_set_subtitle(Listing *lst, const char *subtitle);
void lst_set_page_size(Listing *lst, int size);
void lst_new_page(Listing *lst);
void lst_enable(Listing *lst);
void lst_disable(Listing *lst);

/* called at start of each source statement */
void lst_begin_stmt(Listing *lst, int addr, ValMode mode, int common_id,
                    const char *source, const char *file, int line,
                    int from_include, int from_macro);

/* called as bytes are generated */
void lst_add_byte(Listing *lst, int byte);
void lst_add_word(Listing *lst, int word);
void lst_add_reloc_word(Listing *lst, int word, ValMode mode);

/* called at end of statement — flushes the line */
void lst_end_stmt(Listing *lst);

/* mark statement as suppressed (false conditional) */
void lst_set_suppressed(Listing *lst, int suppressed);

/* print symbol table at end */
void lst_symbol_table(Listing *lst, SymTab *st);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
