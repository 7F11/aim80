#define _GNU_SOURCE
#include "listing.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "expr.h"
void expr_init(ExprCtx *ctx, SymTab *symtab, Segment *segment) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->symtab = symtab;
    ctx->segment = segment;
    ctx->radix = 10;
}

/* skip whitespace */
static const char *skip(const char *p) {
    while (isspace(*p)) p++;
    return p;
}

/* read a symbol name, return length */
static int read_sym(const char *p, char *buf, int bufsize) {
    int i = 0;
    while (*p && (isalnum((unsigned char) *p) || *p == '$' || *p == '.' || *p == '?' || *p == '@' || *p == '_')) {
        if (i < bufsize - 1) buf[i++] = toupper((unsigned char)*p);
        p++;
    }
    buf[i] = '\0';
    return i;
}

/* parse number with radix suffix */
static int parse_num(const char *s, int len, int default_radix) {
    if (len == 0) return 0;

    int radix = default_radix, end = len;
    char last = toupper((unsigned char)s[len - 1]);
    if (last == 'H') {
        radix = 16;
        end--;
    }
    else if (last == 'O' || last == 'Q') {
        radix = 8;
        end--;
    }
    else if (last == 'B' && len > 1) {
        radix = 2;
        end--;
    }
    else if (last == 'D') {
        radix = 10;
        end--;
    }
    int val = 0;
    for (int i = 0; i < end; i++) {
        int d;
        char c = toupper((unsigned char)s[i]);
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;

        val = val * radix + d;
    }
    return val & 0xFFFF;
}

static int check_bad_digit(const char *s, int len, int default_radix) {
    if (len == 0) return 0;

    int radix = default_radix, end = len;
    char last = toupper((unsigned char)s[len - 1]);
    if (last == 'H') {
        radix = 16;
        end--;
    }
    else if (last == 'O' || last == 'Q') {
        radix = 8;
        end--;
    }
    else if (last == 'B' && len > 1) {
        radix = 2;
        end--;
    }
    else if (last == 'D') {
        radix = 10;
        end--;
    }
    for (int i = 0; i < end; i++) {
        int d;
        char c = toupper((unsigned char)s[i]);
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            break;

        if (d >= radix) return 1;
    }
    return 0;
}

/* mode arithmetic */
static ExprError mode_add(Value *a, Value *b, Value *r) {
    int an = a->num, bn = b->num;
    if (val_is_absolute(*a)) {
        *r = *b;
        r->num = an + bn;
        return EXPR_OK;
    }
    if (val_is_absolute(*b)) {
        *r = *a;
        r->num = an + bn;
        return EXPR_OK;
    }
    if (a->external) {
        *r = *a;
        r->num = an + bn;
        return EXPR_OK;
    }
    if (b->external) {
        *r = *b;
        r->num = an + bn;
        return EXPR_OK;
    }
    return EXPR_MODE_ERROR;
}

static ExprError mode_sub(Value *a, Value *b, Value *r) {
    int an = a->num, bn = b->num;
    if (val_is_absolute(*b)) {
        *r = *a;
        r->num = an - bn;
        return EXPR_OK;
    }
    if (a->mode == b->mode && !a->external && !b->external) {
        if (a->mode == MODE_COMMON && a->common_id != b->common_id) return EXPR_MODE_ERROR;
        *r = val_absolute(an - bn);
        return EXPR_OK;
    }
    if (a->external && val_is_absolute(*b)) {
        *r = *a;
        r->num = an - bn;
        return EXPR_OK;
    }
    /* external - relocatable: mark as complex (needs RPN extension) */
    if (a->external && !val_is_absolute(*b)) {
        *r = *a;
        r->num = an - bn;
        r->complex_reloc = 1;
        return EXPR_OK;
    }
    return EXPR_MODE_ERROR;
}

/* keyword match at position (must be followed by non-alnum) */
static int kwmatch(const char *p, const char *kw) {
    int len = (int)strlen(kw);
    if (strncasecmp(p, kw, len) != 0) return 0;
    if (isalnum((unsigned char)p[len]) || p[len] == '_') return 0;
    return len;
}

/* forward declarations */
static ExprError parse_or(ExprCtx *ctx, const char **pp, Value *r);

static ExprError parse_primary(ExprCtx *ctx, const char **pp, Value *r) {
    const char *p = skip(*pp);
    *r = val_absolute(0);

    /* parenthesized expression */
    if (*p == '(') {
        p++;
        ExprError err = parse_or(ctx, &p, r);
        if (err != EXPR_OK) {
            *pp = p;
            return err;
        }
        p = skip(p);
        if (*p == ')') p++;
        *pp = p;
        return EXPR_OK;
    }
    /* $ = location counter */
    if (*p == '$' && !isalnum((unsigned char)p[1]) && p[1] != '_') {
        *r = segment_here(ctx->segment);
        *pp = p + 1;
        return EXPR_OK;
    }
    /* string constant (1-2 chars) */
    if (*p == '\'' || *p == '"') {
        char delim = *p++;
        int v = 0, cnt = 0;
        while (*p && *p != delim) {
            if (cnt < 2) v = (v << 8) | (unsigned char)*p;
            cnt++;
            p++;
        }
        if (*p == delim) p++;
        if (cnt == 1) v &= 0xFF;
        *r = val_absolute(v);
        *pp = p;
        return EXPR_OK;
    }
    /* number */
    if (isdigit((unsigned char)*p)) {
        const char *start = p;
        while (isalnum((unsigned char)*p)) p++;

        int len = (int)(p - start);
        *r = val_absolute(parse_num(start, len, ctx->radix));
        if (check_bad_digit(start, len, ctx->radix)) ctx->num_error = 1;
        *pp = p;
        return EXPR_OK;
    }
    /* symbol or keyword */
    if (isalpha((unsigned char)*p) || *p == '_' || *p == '?' || *p == '@' || *p == '$' || *p == '.') {
        char sym[SYM_NAME_BUF];
        int len = read_sym(p, sym, sizeof(sym));
        p += len;
        /* check ## external marker — auto-declare as external */
        if (p[0] == '#' && p[1] == '#') {
            p += 2;
            char upper[SYM_NAME_BUF];
            for (int i = 0; sym[i] && i < SYM_NAME_BUF - 1; i++)upper[i] = toupper((unsigned char)sym[i]);

            upper[strlen(sym) > SYM_NAME_BUF - 1 ? SYM_NAME_BUF - 1 : (int)strlen(sym)] = '\0';
            SymEntry *e = symtab_lookup(ctx->symtab, upper);
            if (!e || e->type != SYM_EXTERNAL)
                symtab_define(ctx->symtab, upper, SYM_EXTERNAL, val_external(upper));

            e = symtab_lookup(ctx->symtab, upper);
            if (e) {
                *r = e->val;
                *pp = p;
                return EXPR_OK;
            }
        }
        SymEntry *e = symtab_lookup_sym(ctx->symtab, sym);
        if (!e && ctx->pass == 1 && ctx->create_fwdref) {
            e = symtab_define(ctx->symtab, sym, SYM_LABEL, val_absolute(0));
            e->defined = 0;
        }
        if (e && e->defined) {
            *r = e->val;
            if (e->multi_def) ctx->ext_error = 2;
            if (ctx->listing && ctx->pass == 2)
                lst_xref_add((Listing*)ctx->listing, e->name, ctx->src_line, 0);
            *pp = p;
            return EXPR_OK;
        }
        /* undefined — return 0 with error */
        snprintf(ctx->errmsg, sizeof(ctx->errmsg), "Undefined: %.30s", sym);
    }
    snprintf(ctx->errmsg, sizeof(ctx->errmsg), "Syntax error in expression");
    *pp = p;
    return EXPR_SYNTAX;
}

/* HIGH / LOW */
static ExprError parse_highlow(ExprCtx *ctx, const char **pp, Value *r) {
    const char *p = skip(*pp);
    int n;
    if ((n = kwmatch(p, "HIGH"))) {
        p += n;
        ExprError e = parse_highlow(ctx, &p, r);
        if (e) return e;
        if (r->mode == MODE_ABSOLUTE || r->external) r->num = (r->num >> 8) & 0xFF;
        else                                         r->byte_op = 2;
        *pp = p;
        return EXPR_OK;
    }
    if ((n = kwmatch(p, "LOW"))) {
        p += n;
        ExprError e = parse_highlow(ctx, &p, r);
        if (e) return e;
        if (r->mode == MODE_ABSOLUTE || r->external) r->num = r->num & 0xFF;
        else                                         r->byte_op = 1;
        *pp = p;
        return EXPR_OK;
    }
    if ((n = kwmatch(p, "TYPE"))) {
        p += n;
        ExprError e = parse_highlow(ctx, &p, r);
        int tb = 0;
        if (e == EXPR_OK) {
            if (r->external)                                  tb = 0x80;
            else if (r->mode != MODE_ABSOLUTE || r->num != 0) tb = 0x20;

            if      (r->mode == MODE_CODE_REL) tb |= 1;
            else if (r->mode == MODE_DATA_REL) tb |= 2;
        }
        r->num = tb;
        r->mode = MODE_ABSOLUTE;
        r->external = 0;
        r->byte_op = 0;
        *pp = p;
        return EXPR_OK;
    }
    *pp = p;
    return parse_primary(ctx, pp, r);
}

/* * / MOD SHR SHL */
static ExprError parse_mul(ExprCtx *ctx, const char **pp, Value *r) {
    ExprError err = parse_highlow(ctx, pp, r);
    if (err) return err;

    for (;;) {
        const char *p = skip(*pp);
        int n;
        if (*p == '*') {
            p++;
            Value rr;
            err = parse_highlow(ctx, &p, &rr);
            if (err) return err;
            r->num *= rr.num;
        }
        else if (*p == '/') {
            p++;
            Value rr;
            err = parse_highlow(ctx, &p, &rr);
            if (err) return err;
            if (!rr.num) return EXPR_DIVZERO;
            r->num /= rr.num;
        }
        else if ((n = kwmatch(p, "MOD"))) {
            p += n;
            Value rr;
            err = parse_highlow(ctx, &p, &rr);
            if (err) return err;
            if (!rr.num) return EXPR_DIVZERO;
            r->num %= rr.num;
        }
        else if ((n = kwmatch(p, "SHR"))) {
            p += n;
            Value rr;
            err = parse_highlow(ctx, &p, &rr);
            if (err) return err;
            r->num = (unsigned)r->num >> rr.num;
        }
        else if ((n = kwmatch(p, "SHL"))) {
            p += n;
            Value rr;
            err = parse_highlow(ctx, &p, &rr);
            if (err) return err;
            r->num <<= rr.num;
        }
        else
            break;
        r->num &= 0xFFFF;
        *pp = p;
    }
    return EXPR_OK;
}

/* unary +/- */
static ExprError parse_unary(ExprCtx *ctx, const char **pp, Value *r) {
    const char *p = skip(*pp);
    if (*p == '-') {
        p++;
        *pp = p;
        ExprError e = parse_mul(ctx, pp, r);
        if (e) return e;
        r->num = (-r->num) & 0xFFFF;
        return EXPR_OK;
    }
    if (*p == '+') {
        p++;
        *pp = p;
        return parse_mul(ctx, pp, r);
    }
    *pp = p;
    return parse_mul(ctx, pp, r);
}

/* + - */
static ExprError parse_add(ExprCtx *ctx, const char **pp, Value *r) {
    ExprError err = parse_unary(ctx, pp, r);
    if (err) return err;

    for (;;) {
        const char *p = skip(*pp);
        if (*p == '+') {
            p++;
            Value rr;
            err = parse_unary(ctx, &p, &rr);
            if (err) return err;
            err = mode_add(r, &rr, r);
            if (err) {
                ctx->reloc_error = 1;
                return err;
            }
        }
        else if (*p == '-') {
            p++;
            Value rr;
            err = parse_unary(ctx, &p, &rr);
            if (err) return err;
            err = mode_sub(r, &rr, r);
            if (err) {
                ctx->reloc_error = 1;
                return err;
            }
        }
        else
            break;
        r->num &= 0xFFFF;
        *pp = p;
    }
    return EXPR_OK;
}

/* EQ NE LT LE GT GE */
static ExprError parse_cmp(ExprCtx *ctx, const char **pp, Value *r) {
    ExprError err = parse_add(ctx, pp, r);
    if (err) return err;

    for (;;) {
        const char *p = skip(*pp);
        int n, cmp = 0;
        Value rr;
        if ((n = kwmatch(p, "EQ"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num == rr.num);
        }
        else if ((n = kwmatch(p, "NE"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num != rr.num);
        }
        else if ((n = kwmatch(p, "LE"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num <= rr.num);
        }
        else if ((n = kwmatch(p, "LT"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num < rr.num);
        }
        else if ((n = kwmatch(p, "GE"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num >= rr.num);
        }
        else if ((n = kwmatch(p, "GT"))) {
            p += n;
            err = parse_add(ctx, &p, &rr);
            if (err) return err;
            cmp = (r->num > rr.num);
        }
        else
            break;
        *r = val_absolute(cmp ? 0xFFFF : 0);
        *pp = p;
    }
    return EXPR_OK;
}

/* NOT */
static ExprError parse_not(ExprCtx *ctx, const char **pp, Value *r) {
    const char *p = skip(*pp);
    int n;
    if ((n = kwmatch(p, "NOT"))) {
        p += n;
        *pp = p;
        ExprError e = parse_cmp(ctx, pp, r);
        if (e) return e;
        r->num = (~r->num) & 0xFFFF;
        return EXPR_OK;
    }
    *pp = p;
    return parse_cmp(ctx, pp, r);
}

/* AND */
static ExprError parse_and(ExprCtx *ctx, const char **pp, Value *r) {
    ExprError err = parse_not(ctx, pp, r);
    if (err) return err;

    for (;;) {
        const char *p = skip(*pp);
        int n;
        if ((n = kwmatch(p, "AND"))) {
            p += n;
            Value rr;
            err = parse_not(ctx, &p, &rr);
            if (err) return err;
            r->num &= rr.num;
            *pp = p;
        }
        else
            break;
    }
    return EXPR_OK;
}

/* OR XOR */
static ExprError parse_or(ExprCtx *ctx, const char **pp, Value *r) {
    ExprError err = parse_and(ctx, pp, r);
    if (err) return err;

    for (;;) {
        const char *p = skip(*pp);
        int n;
        if ((n = kwmatch(p, "OR"))) {
            p += n;
            Value rr;
            err = parse_and(ctx, &p, &rr);
            if (err) return err;
            r->num |= rr.num;
            *pp = p;
        }
        else if ((n = kwmatch(p, "XOR"))) {
            p += n;
            Value rr;
            err = parse_and(ctx, &p, &rr);
            if (err) return err;
            r->num ^= rr.num;
            *pp = p;
        }
        else
            break;
    }
    return EXPR_OK;
}

ExprError expr_eval_str(ExprCtx *ctx, const char *str, const char **endp, Value *result) {
    *result = val_absolute(0);
    ExprError err = parse_or(ctx, &str, result);
    result->num &= 0xFFFF;
    if (endp) *endp = str;
    return err;
}

ExprError expr_eval(ExprCtx *ctx, const char *str, Value *result) {
    return expr_eval_str(ctx, str, NULL, result);
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
