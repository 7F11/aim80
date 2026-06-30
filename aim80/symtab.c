#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "symtab.h"

/* value constructors */
Value val_absolute(int num) {
    return (Value){ .num = num, .mode = MODE_ABSOLUTE };
}

Value val_relative(int num, ValMode mode) {
    return (Value){ .num = num, .mode = mode };
}

Value val_common(int num, int common_id) {
    return (Value){ .num = num, .mode = MODE_COMMON, .common_id = common_id };
}

Value val_external(const char *name) {
    Value v = { .mode = MODE_ABSOLUTE, .external = 1 };
    strncpy(v.ext_name, name, SYM_NAME_BUF - 1);
    return v;
}

int val_is_absolute(Value v) {
    return v.mode == MODE_ABSOLUTE && !v.external;
}

/* hash */
static unsigned hash(const char *s) {
    unsigned h = 0;
    while (*s) h = h * 31 + (unsigned char)toupper(*s++);
    return h % SYMTAB_SIZE;
}

static void canonicalize(char *dst, const char *src, int maxlen, int sig_len) {
    int limit = sig_len < maxlen - 1 ? sig_len : maxlen - 1;
    int i;
    for (i = 0; i < limit && src[i]; i++) dst[i] = toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

void symtab_init(SymTab *st) {
    memset(st, 0, sizeof(*st));
    st->local_counter = 0;
    st->sig_len = SYM_NAME_DEFAULT;
}

void symtab_free(SymTab *st) {
    for (int i = 0; i < SYMTAB_SIZE; i++) {
        SymEntry *e = st->buckets[i];
        while (e) {
            SymEntry *next = e->next;
            if (e->macro) free(e->macro);
            free(e);
            e = next;
        }
        st->buckets[i] = NULL;
    }
}

SymEntry *symtab_lookup(SymTab *st, const char *name) {
    char canon[SYM_NAME_BUF];
    /* try with current sig_len */
    canonicalize(canon, name, SYM_NAME_BUF, st->sig_len);
    unsigned h = hash(canon);
    for (SymEntry *e = st->buckets[h]; e; e = e->next)
        if (strcmp(e->name, canon) == 0) return e;

    /* try with full name length (finds symbols defined under longer .SYMLEN) */
    int full_len = (int)strlen(name);
    if (full_len > st->sig_len) {
        int cap = full_len > SYM_NAME_MAXLEN ? SYM_NAME_MAXLEN : full_len;
        canonicalize(canon, name, SYM_NAME_BUF, cap);
        h = hash(canon);
        for (SymEntry *e = st->buckets[h]; e; e = e->next)
            if (strcmp(e->name, canon) == 0) return e;
    }
    return NULL;
}

/* lookup skipping macro entries (for expression evaluation - separate namespace) */
SymEntry *symtab_lookup_sym(SymTab *st, const char *name) {
    char canon[SYM_NAME_BUF];
    canonicalize(canon, name, SYM_NAME_BUF, st->sig_len);
    unsigned h = hash(canon);
    SymEntry *undef = NULL;
    for (SymEntry *e = st->buckets[h]; e; e = e->next)
        if (strcmp(e->name, canon) == 0 && e->type != SYM_MACRO) {
            if (e->defined) return e;
            if (!undef) undef = e;
        }
    if (undef) return undef;

    int full_len = (int)strlen(name);
    if (full_len > st->sig_len) {
        int cap = full_len > SYM_NAME_MAXLEN ? SYM_NAME_MAXLEN : full_len;
        canonicalize(canon, name, SYM_NAME_BUF, cap);
        h = hash(canon);
        for (SymEntry *e = st->buckets[h]; e; e = e->next)
            if (strcmp(e->name, canon) == 0 && e->type != SYM_MACRO) {
                if (e->defined) return e;
                if (!undef) undef = e;
            }
    }
    return undef;
}

#define SET_SYMBOL(sym, t, v) do { (sym)->type = t; (sym)->val = v; (sym)->defined = 1; } while(0)

SymEntry *symtab_define(SymTab *st, const char *name, SymType type, Value val) {
    SymEntry *e = symtab_lookup(st, name);
    if (e) {
        if (e->type == SYM_MACRO && type != SYM_MACRO) {
            /* separate namespace: check if non-macro entry already exists */
            SymEntry *sym = symtab_lookup_sym(st, name);
            if (sym) {
                SET_SYMBOL(sym, type, val);
                return sym;
            }
            goto create_new;
        }
        if (e->type != SYM_MACRO && type == SYM_MACRO) {
            /* macro alongside symbol */
            goto create_new;
        }
        SET_SYMBOL(e, type, val);
        return e;
    }
create_new:;
    e = calloc(1, sizeof(*e));
    canonicalize(e->name, name, SYM_NAME_BUF, st->sig_len);
    SET_SYMBOL(e, type, val);
    e->order = st->next_order++;
    unsigned h = hash(e->name);
    e->next = st->buckets[h];
    st->buckets[h] = e;
    return e;
}

SymEntry *symtab_define_macro(SymTab *st, const char *name, MacroDef *def) {
    SymEntry *e = symtab_define(st, name, SYM_MACRO, val_absolute(0));
    if (e->macro) free(e->macro);
    e->macro = malloc(sizeof(MacroDef));
    memcpy(e->macro, def, sizeof(MacroDef));
    return e;
}

int symtab_next_local(SymTab *st, char *buf, int bufsize) {
    if (st->local_counter > 0xFFFF) return -1;
    snprintf(buf, bufsize, "..%04X", st->local_counter++);
    return 0;
}

int symtab_get_common(SymTab *st, const char *name) {
    char canon[SYM_NAME_BUF];
    canonicalize(canon, name, SYM_NAME_BUF, st->sig_len);
    /* blank common: empty name */
    for (int i = 0; i < st->common_count; i++)
        if (strcmp(st->commons[i].name, canon) == 0) return i;

    if (st->common_count >= COMMON_MAX) return -1;

    int id = st->common_count++;
    snprintf(st->commons[id].name, SYM_NAME_BUF, "%s", canon);
    st->commons[id].size = 0;
    return id;
}

void symtab_set_siglen(SymTab *st, int len) {
    if (len < 1) len = 1;
    if (len > SYM_NAME_MAXLEN) len = SYM_NAME_MAXLEN;
    st->sig_len = len;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
