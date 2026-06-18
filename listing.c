#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "listing.h"

void lst_init(Listing *lst, FILE *fp) {
    memset(lst, 0, sizeof(*lst));
    lst->fp = fp;
    lst->enabled = 1;
    lst->page_size = 50;
    lst->page_num = 1;
    lst->list_false_cond = 1;
}

void lst_set_title(Listing *lst, const char *title) {
    snprintf(lst->title, sizeof(lst->title), "%.63s", title);
}

void lst_set_subtitle(Listing *lst, const char *subtitle) {
    snprintf(lst->pending_subtitle, sizeof(lst->pending_subtitle), "%.63s", subtitle);
}

void lst_set_page_size(Listing *lst, int size) {
    if (size >= 10 && size <= 255) lst->page_size = size;
}

static void print_header(Listing *lst) {
    if (lst->page_num == 1 && lst->page_sub == 0 && lst->line_on_page == 0)
        fputc('\f', lst->fp);

    if (lst->title[0]) fprintf(lst->fp, "%s", lst->title);

    if (lst->page_sub == -1)
        fprintf(lst->fp, "\tAIM80\tPAGE\tS\n");
    else if (lst->page_sub > 0)
        fprintf(lst->fp, "\tAIM80\tPAGE\t%d-%d\n", lst->page_num, lst->page_sub);
    else
        fprintf(lst->fp, "\tAIM80\tPAGE\t%d\n", lst->page_num);

    if (lst->subtitle[0]) fprintf(lst->fp, "%s\n", lst->subtitle);

    fprintf(lst->fp, "\n\n");
    lst->line_on_page = lst->subtitle[0] ? 5 : 4;
}

void lst_new_page(Listing *lst) {
    if (!lst->fp || !lst->enabled) return;

    fputc('\f', lst->fp);
    lst->page_sub++;
    lst->line_on_page = 0;
    /* promote pending subtitle for next header */
    if (lst->pending_subtitle[0]) {
        snprintf(lst->subtitle, sizeof(lst->subtitle), "%s", lst->pending_subtitle);
        lst->pending_subtitle[0] = '\0';
    }
}

void lst_enable(Listing *lst) {
    lst->enabled = 1;
}

void lst_disable(Listing *lst) {
    lst->enabled = 0;
}

static void check_page(Listing *lst) {
    if (lst->line_on_page >= lst->page_size) lst_new_page(lst);
}

static char mode_char(ValMode m) {
    switch (m) {
    case MODE_CODE_REL: return '\'';
    case MODE_DATA_REL: return '"';
    case MODE_COMMON:   return '!';
    case MODE_EXTERNAL: return '*';
    default:            return ' ';
    }
}

void lst_begin_stmt(Listing *lst, int addr, ValMode mode, int common_id,
                    const char *source, const char *file, int line,
                    int from_include, int from_macro) {
    lst->stmt_addr         = addr;
    lst->stmt_mode         = mode;
    lst->stmt_common_id    = common_id;
    lst->stmt_nbytes       = 0;
    lst->stmt_suppressed   = 0;
    lst->stmt_has_value    = 0;
    lst->stmt_has_label    = 0;
    lst->stmt_error        = 0;
    lst->stmt_line         = line;
    lst->stmt_from_include = from_include;
    lst->stmt_from_macro   = from_macro;
    snprintf(lst->stmt_source, sizeof(lst->stmt_source), "%s", source ? source : "");
    snprintf(lst->stmt_file, sizeof(lst->stmt_file), "%s", file ? file : "");
}

void lst_add_byte(Listing *lst, int byte) {
    if (lst->stmt_nbytes < (int)sizeof(lst->stmt_bytes)) {
        lst->stmt_modes[lst->stmt_nbytes]   = 0;
        lst->stmt_bytes[lst->stmt_nbytes++] = byte & 0xFF;
    }
    else
        lst->stmt_nbytes++;
}

void lst_add_word(Listing *lst, int word) {
    if (lst->stmt_nbytes < 30) {
        lst->stmt_bytes[lst->stmt_nbytes] = word & 0xFF;
        lst->stmt_modes[lst->stmt_nbytes] = 0;
        lst->stmt_nbytes++;
        lst->stmt_bytes[lst->stmt_nbytes] = (word >> 8) & 0xFF;
        lst->stmt_modes[lst->stmt_nbytes] = ' '; /* marks as absolute word */
        lst->stmt_nbytes++;
    }
}

void lst_set_suppressed(Listing *lst, int suppressed) {
    lst->stmt_suppressed = suppressed;
}

void lst_end_stmt(Listing *lst) {
    if (!lst->fp || !lst->enabled) return;

    if (lst->stmt_suppressed) {
        lst->stmt_suppressed = 0;
        return;
    }
    /* .XALL (default): suppress macro expansion lines that generate no code */
    if (lst->macro_list_mode == 0 && lst->stmt_from_macro && lst->stmt_nbytes == 0 && !lst->stmt_has_label)
        return;

    /* .SALL: suppress ALL macro expansion lines */
    if (lst->macro_list_mode == 2 && lst->stmt_from_macro) return;

    if (lst->line_on_page == 0) print_header(lst);

    check_page(lst);

    char mc = mode_char(lst->stmt_mode);

    /* format address */
    char addr_str[8];
    if (lst->stmt_nbytes > 0 || lst->stmt_has_value || lst->stmt_has_label)
        snprintf(addr_str, sizeof(addr_str), "%04X%c", lst->stmt_addr & 0xFFFF, mc);
    else
        snprintf(addr_str, sizeof(addr_str), "     ");

    /* format hex bytes — M80 style: words shown as 4-digit values */
    char hex_str[48] = "";
    int pos = 0;
    int show = lst->stmt_nbytes < LST_MAX_BYTES ? lst->stmt_nbytes : LST_MAX_BYTES;
    /* check if last 2 bytes should be displayed as a word (3/4 byte instruction with no reloc markers) */
    int word_at = -1;
    if (show >= 3 && !lst->stmt_modes[show - 1] && !lst->stmt_modes[show - 2]) {
        /* all unmarked - pair last 2 as word for 3/4 byte sequences */
        int all_unmarked = 1;
        for (int i = 0; i < show; i++) {
            if (lst->stmt_modes[i]) {
                all_unmarked = 0;
                break;
            }
        }
        if (all_unmarked && show <= 4) word_at = show - 2;
    }
    for (int i = 0; i < show; i++) {
        if (i + 1 < show && lst->stmt_modes[i + 1]) {
            /* this byte + next byte form a word — show as HHLL+mode */
            char mc = lst->stmt_modes[i + 1];
            if (mc == ' ')   /* absolute word, no suffix */
                pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X%02X",
                                lst->stmt_bytes[i + 1], lst->stmt_bytes[i]);
            else
                pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X%02X%c",
                                lst->stmt_bytes[i + 1], lst->stmt_bytes[i], mc);

            i++; /* skip next byte, already consumed */
        }
        else if (i == word_at) {
            pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X%02X",
                            lst->stmt_bytes[i + 1], lst->stmt_bytes[i]);
            i++;
        }
        else
            pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X", lst->stmt_bytes[i]);

        if (i < show - 1 && pos < (int)sizeof(hex_str) - 1) hex_str[pos++] = ' ';
    }
    /* include/macro marker */
    char marker = ' ';
    if (lst->stmt_from_include && lst->stmt_from_macro) marker = '+';
    else if (lst->stmt_from_include)                    marker = 'C';
    else if (lst->stmt_from_macro)                      marker = '+';

    /* print the line */
    char ec = lst->stmt_error ? lst->stmt_error : ' ';
    if (lst->stmt_source[0] == ';' && lst->stmt_nbytes == 0)
        fprintf(lst->fp, "%c %30s%s\n", ec, "", lst->stmt_source);
    else if (lst->stmt_nbytes == 0 && !lst->stmt_has_value &&
             lst->stmt_from_include && !lst->stmt_from_macro)
        fprintf(lst->fp, "%c %30s%s\n", ec, "", lst->stmt_source);
    else if (lst->stmt_nbytes == 0 && !lst->stmt_has_value && !lst->stmt_has_label)
        fprintf(lst->fp, "%c %24s%c     %s\n", ec, "", marker, lst->stmt_source);
    else
        fprintf(lst->fp, "%c %s   %-16s%c     %s\n", ec,
                addr_str, hex_str, marker, lst->stmt_source);

    lst->line_on_page++;

    /* continuation lines for extra bytes */
    int offset = LST_MAX_BYTES;
    while (offset < lst->stmt_nbytes) {
        check_page(lst);
        int cont_addr = (lst->stmt_addr + offset) & 0xFFFF;
        snprintf(addr_str, sizeof(addr_str), "%04X%c", cont_addr, mc);
        pos = 0;
        hex_str[0] = '\0';
        int end = offset + LST_MAX_BYTES;
        if (end > lst->stmt_nbytes) end = lst->stmt_nbytes;

        for (int i = offset; i < end; i++) {
            pos += snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X", lst->stmt_bytes[i]);
            if (i < end - 1 && pos < (int)sizeof(hex_str) - 1) hex_str[pos++] = ' ';
        }
        fprintf(lst->fp, "  %s   %-14s\n", addr_str, hex_str);
        lst->line_on_page++;
        offset += LST_MAX_BYTES;
    }
}

/* compare for qsort */
static int sym_cmp(const void *a, const void *b) {
    const SymEntry *sa = *(const SymEntry **)a;
    const SymEntry *sb = *(const SymEntry **)b;
    return strcmp(sa->name, sb->name);
}

void lst_symbol_table(Listing *lst, SymTab *st) {
    if (!lst->fp) return;

    SymEntry *syms[4096], *macros[256];
    int scount = 0, mcount = 0;
    for (int i = 0; i < SYMTAB_SIZE; i++)
        for (SymEntry *e = st->buckets[i]; e; e = e->next) {
            if (e->type == SYM_MACRO && mcount < 256)
                macros[mcount++] = e;
            else if (e->type != SYM_MACRO && scount < 4096)
                syms[scount++] = e;
        }
    lst_new_page(lst);
    lst->page_sub = -1;
    print_header(lst);
    if (mcount > 0) {
        qsort(macros, mcount, sizeof(SymEntry*), sym_cmp);
        fprintf(lst->fp, "Macros:\n");
        lst->line_on_page++;
        for (int i = 0; i < mcount; i++) fprintf(lst->fp, "%-16s", macros[i]->name);
        fprintf(lst->fp, "\n\n");
        lst->line_on_page += 2;
    }
    if (scount == 0)
        return;

    qsort(syms, scount, sizeof(SymEntry*), sym_cmp);
    fprintf(lst->fp, "Symbols:\n");
    lst->line_on_page++;
    int col = 0;
    for (int i = 0; i < scount; i++) {
        check_page(lst);
        SymEntry *e = syms[i];
        char mc;
        if (e->type == SYM_EXTERNAL)
            fprintf(lst->fp, "%04X*\t%-16s", e->chain_head & 0xFFFF, e->name);
        else if (!e->defined)
            fprintf(lst->fp, "%04XU\t%-16s", e->val.num & 0xFFFF, e->name);
        else {
            mc = mode_char(e->val.mode);
            if (e->is_public)
                fprintf(lst->fp, "%04XI%c\t%-16s", e->val.num & 0xFFFF, mc, e->name);
            else
                fprintf(lst->fp, "%04X%c\t%-16s", e->val.num & 0xFFFF, mc, e->name);
        }
        if (++col >= 3) {
            fprintf(lst->fp, "\n");
            col = 0;
            lst->line_on_page++;
        }
    }
    if (col > 0) {
        fprintf(lst->fp, "\n");
        lst->line_on_page++;
    }
}

void lst_add_reloc_word(Listing *lst, int word, ValMode mode) {
    char mc = mode_char(mode);
    if (lst->stmt_nbytes < 30) {
        lst->stmt_bytes[lst->stmt_nbytes] = word & 0xFF;
        lst->stmt_modes[lst->stmt_nbytes] = 0;
        lst->stmt_nbytes++;
        lst->stmt_bytes[lst->stmt_nbytes] = (word >> 8) & 0xFF;
        lst->stmt_modes[lst->stmt_nbytes] = mc;
        lst->stmt_nbytes++;
    }
}

void lst_xref_add(Listing *lst, const char *name, int line, int is_def) {
    if (!lst->xref_enabled || !name[0]) return;

    if (lst->xref_count >= lst->xref_cap) {
        lst->xref_cap = lst->xref_cap ? lst->xref_cap * 2 : 1024;
        lst->xref = realloc(lst->xref, lst->xref_cap * sizeof(lst->xref[0]));
    }
    struct xref_entry *e = &lst->xref[lst->xref_count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->line = line;
    e->is_def = is_def;
}

static int xref_cmp(const void *a, const void *b) {
    const struct xref_entry *xa = a, *xb = b;
    int c = strcmp(xa->name, xb->name);
    if (c) return c;
    return xa->line - xb->line;
}

void lst_xref_output(Listing *lst) {
    if (!lst->fp || !lst->xref_enabled || lst->xref_count == 0) return;

    qsort(lst->xref, lst->xref_count, sizeof(lst->xref[0]), xref_cmp);

    lst_new_page(lst);
    lst->page_sub = -1; /* use 'S' page? or just next sub */
    print_header(lst);
    fprintf(lst->fp, "Cross Reference\n\n");
    lst->line_on_page += 2;

    int i = 0;
    while (i < lst->xref_count) {
        char *cur_name = lst->xref[i].name;
        fprintf(lst->fp, "%-16s", cur_name);
        int col = 16;
        while (i < lst->xref_count && strcmp(lst->xref[i].name, cur_name) == 0) {
            int prev_line = (i > 0) ? lst->xref[i - 1].line : -1;
            /* skip duplicate line entries for same symbol */
            if (lst->xref[i].line == prev_line &&
                strcmp(lst->xref[i].name, cur_name) == 0) {
                i++;
                continue;
            }
            char buf[8];
            if (lst->xref[i].is_def)
                snprintf(buf, sizeof(buf), "%d#", lst->xref[i].line);
            else
                snprintf(buf, sizeof(buf), "%d", lst->xref[i].line);

            if (col + (int)strlen(buf) + 1 > 78) {
                fprintf(lst->fp, "\n%16s", "");
                col = 16;
                lst->line_on_page++;
            }
            fprintf(lst->fp, " %s", buf);
            col += strlen(buf) + 1;
            i++;
        }
        fprintf(lst->fp, "\n");
        lst->line_on_page++;
    }
    free(lst->xref);
    lst->xref = NULL;
    lst->xref_count = 0;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
