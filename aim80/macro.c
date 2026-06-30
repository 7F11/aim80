#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "expr.h"
#include "macro.h"

void macro_init(MacroCtx *ctx, SymTab *symtab, LineSource *ls) {
    ctx->symtab = symtab;
    ctx->ls = ls;
}

/* collect body lines until matching ENDM */
int macro_collect_body(LineSource *ls, char *body, int maxlen) {
    int pos = 0, nest = 1;
    SrcLine sl;
    while (nest > 0 && ls_getline(ls, &sl) == 0) {
        char saved[LINE_MAX];
        snprintf(saved, sizeof(saved), "%s", sl.raw); /* save before split modifies it */
        line_split(&sl);
        const char *op = sl.op;
        if (op) {
            if (strcasecmp(op, "MACRO") == 0 || strcasecmp(op, "REPT") == 0 ||
                strcasecmp(op, "IRP") == 0 || strcasecmp(op, "IRPC") == 0)
                nest++;
            else if (strcasecmp(op, "ENDM") == 0) {
                nest--;
                if (nest == 0) break;
            }
        }
        int len = (int)strlen(saved);
        if (pos + len + 1 < maxlen) {
            memcpy(body + pos, saved, len);
            pos += len;
            body[pos++] = '\n';
        }
    }
    body[pos] = '\0';
    return pos;
}

/* parse comma-separated parameter names */
static int parse_params(const char *line, char params[][SYM_NAME_BUF], int max) {
    if (!line) return 0;

    int count = 0;
    const char *p = line;
    while (*p && count < max) {
        while (isspace(*p) || *p == ',') p++;
        if (!*p || *p == ';') break;

        int i = 0;
        while (*p && *p != ',' && !isspace(*p) && *p != ';') {
            if (i < SYM_NAME_BUF - 1) params[count][i++] = toupper((unsigned char)*p);
            p++;
        }
        params[count][i] = '\0';
        count++;
    }
    return count;
}

int macro_collect(MacroCtx *ctx, const char *name, const char *params, LineSource *ls) {
    MacroDef def;
    memset(&def, 0, sizeof(def));
    def.param_count = parse_params(params, def.params, 32);
    def.body_len = macro_collect_body(ls, def.body, MACRO_BODY_MAX);
    symtab_define_macro(ctx->symtab, name, &def);
    return 0;
}

/* parse arguments from macro call (comma-separated, respecting <> and quotes) */
static int parse_args(const char *line, const char **args, char *buf, int bufsize, int maxargs) {
    if (!line) return 0;

    int count = 0, bpos = 0;
    const char *p = line;
    while (*p && count < maxargs) {
        while (isspace(*p)) p++;
        if (!*p || *p == ';') break;

        args[count] = buf + bpos;
        int depth = 0, in_quote = 0;
        while (*p) {
            if (!in_quote && *p == '<') {
                depth++;
                if (depth == 1) {
                    p++;
                    continue;
                }
            }
            if (!in_quote && *p == '>') {
                depth--;
                if (depth == 0) {
                    p++;
                    break;
                }
            }
            if (*p == '!' && *(p + 1) && depth > 0 && !in_quote) {
                p++;
                if (bpos < bufsize - 1) buf[bpos++] = *p;
                p++;
                continue;
            }
            if (*p == '\'' || *p == '"') {
                if      (!in_quote)      in_quote = *p;
                else if (*p == in_quote) in_quote = 0;
            }
            if (!in_quote && depth == 0 && (*p == ',' || isspace(*p)))
                break;

            if (bpos < bufsize - 1) buf[bpos++] = *p;

            p++;
        }
        buf[bpos++] = '\0';
        count++;
        if (*p == ',') p++;
    }
    return count;
}

/* substitute dummies in body, write to out */
static int substitute(const char *body, char *out, int outmax,
                      char dummies[][SYM_NAME_BUF], int ndummies,
                      const char **args, int nargs, SymTab *symtab) {
    int pos = 0;
    (void)symtab;
    const char *p = body;
    while (*p && pos < outmax - 1) {
        if (*p == '&') {
            /* Check if next identifier matches a dummy */
            const char *np = p + 1;
            char ts[SYM_NAME_BUF];
            int ti = 0;
            while (*np && (isalnum((unsigned char)*np) || *np == '?' || *np == '@' ||
                    *np == '_' || *np == '$' || *np == '.') && ti < SYM_NAME_BUF - 1) {
                ts[ti++] = toupper((unsigned char)*np);
                np++;
            }
            ts[ti] = '\0';

            int is_dummy = 0;
            for (int i = 0; i < ndummies; i++)
                if (strcmp(ts, dummies[i]) == 0) {
                    is_dummy = 1;
                    break;
                }
            if (is_dummy) {
                p++;
                continue;
            }                             /* strip & before dummy substitution */

            /* Only preserve boundary marker if preceded by . or _ (prefix concat pattern) */
            if (pos > 0 && (out[pos - 1] == '.' || out[pos - 1] == '_')) {
                out[pos++] = '\x01';
                p++;
                continue;
            }
            else {
                p++;
                continue;
            }                     /* just strip */
        }
        if (*p == ';' && p[1] == ';') {
            /* ;;
            macro comment — skip to end of line */
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '\x01') {
            p++;
            continue;
        }                                    /* word boundary marker from outer & */

        if (isalpha((unsigned char)*p) || *p == '?' || *p == '@' || *p == '_' || *p == '$' || *p == '.') {
            char sym[SYM_NAME_BUF];
            int si = 0;
            const char *start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '?' || *p == '@' ||
                    *p == '_' || *p == '$' || *p == '.')) {
                if (si < SYM_NAME_BUF - 1) sym[si++] = toupper((unsigned char)*p);
                p++;
            }
            sym[si] = '\0';

            int found = -1;
            for (int i = 0; i < ndummies; i++)
                if (strcmp(sym, dummies[i]) == 0) {
                    found = i;
                    break;
                }
            if (found >= 0 && found < nargs) {
                int alen = (int)strlen(args[found]);
                if (pos + alen < outmax) {
                    memcpy(out + pos, args[found], alen);
                    pos += alen;
                }
            }
            else if (found >= 0) {
                /* dummy with no arg — null */
            }
            else {
                int slen = (int)(p - start);
                if (pos + slen < outmax) {
                    memcpy(out + pos, start, slen);
                    pos += slen;
                }
            }
            continue;
        }
        out[pos++] = *p++;
    }
    out[pos] = '\0';
    return pos;
}

static void eval_percent(const char *in, char *out, int outmax, SymTab *symtab) {
    int pos = 0;
    const char *p = in;
    while (*p && pos < outmax - 1) {
        if (*p == '%' && (isdigit((unsigned char)p[1]) || p[1] == '(')) {
            p++;
            const char *end;
            Value val;
            ExprCtx tmpctx;
            expr_init(&tmpctx, symtab, NULL);
            tmpctx.radix = 10;
            if (expr_eval_str(&tmpctx, p, &end, &val) == EXPR_OK && end > p) {
                pos += snprintf(out + pos, outmax - pos, "%d", val.num);
                p = end;
                continue;
            }
            out[pos++] = '%';
        }
        else
            out[pos++] = *p++;
    }
    out[pos] = '\0';
}

int macro_expand(MacroCtx *ctx, const char *name, const char *argline) {

    /* find macro entry specifically (M80 has separate macro namespace) */
    SymEntry *e = symtab_lookup(ctx->symtab, name);
    if (e && e->type != SYM_MACRO)
        /* first hit wasn't macro; scan rest of bucket */
        for (SymEntry *s = e->next; s; s = s->next)
            if (strcmp(s->name, e->name) == 0 && s->type == SYM_MACRO) {
                e = s;
                break;
            }
    if (!e || e->type != SYM_MACRO || !e->macro)
        return -1;

    MacroDef *def = e->macro;

    const char *args[32];
    char argbuf[2048];
    int nargs = parse_args(argline, args, argbuf, sizeof(argbuf), 32);

    /* handle LOCAL */
    char local_d[32][SYM_NAME_BUF], local_s[32][SYM_NAME_BUF];
    int nlocals = 0;
    char *body = def->body;
    char processed[MACRO_BODY_MAX];
    int ppos = 0;
    const char *lp = body;
    while (*lp) {
        const char *eol = strchr(lp, '\n');
        int llen = eol ? (int)(eol - lp) : (int)strlen(lp);
        char line[LINE_MAX];
        memcpy(line, lp, llen);
        line[llen] = '\0';
        /* check for LOCAL */
        char *s = line;
        while (isspace(*s)) s++;

        if (strncasecmp(s, "LOCAL", 5) == 0 && (isspace(s[5]))) {
            s += 5;
            while (*s && nlocals < 32) {
                while (isspace(*s) || *s == ',') s++;
                if (!*s) break;

                int ni = 0;
                while (*s && *s != ',' && !isspace(*s)) {
                    if (ni < SYM_NAME_BUF - 1)
                        local_d[nlocals][ni++] = toupper((unsigned char)*s);
                    s++;
                }
                local_d[nlocals][ni] = '\0';
                symtab_next_local(ctx->symtab, local_s[nlocals], SYM_NAME_BUF);
                nlocals++;
            }
        }
        else if (ppos + llen + 1 < MACRO_BODY_MAX) {
            memcpy(processed + ppos, line, llen);
            ppos += llen;
            processed[ppos++] = '\n';
        }
        lp = eol ? eol + 1 : lp + llen;
    }
    processed[ppos] = '\0';

    /* substitute params */
    char expanded1[MACRO_BODY_MAX];
    substitute(processed, expanded1, MACRO_BODY_MAX, def->params, def->param_count, args, nargs, ctx->symtab);

    /* substitute locals */
    char final[MACRO_BODY_MAX];
    if (nlocals > 0) {
        const char *la[32];
        for (int i = 0; i < nlocals; i++) la[i] = local_s[i];

        char expanded2[MACRO_BODY_MAX];
        substitute(expanded1, expanded2, MACRO_BODY_MAX, local_d, nlocals, la, nlocals, ctx->symtab);
        eval_percent(expanded2, final, MACRO_BODY_MAX, ctx->symtab);
    }
    else {
        eval_percent(expanded1, final, MACRO_BODY_MAX, ctx->symtab);
    }
    ls_push_lines(ctx->ls, final, ORIG_MACRO);
    return 0;
}

int macro_rept(MacroCtx *ctx, int count, const char *body) {
    char expanded[MACRO_BODY_MAX * 4];
    int pos = 0;
    for (int i = 0; i < count && pos < (int)sizeof(expanded) - 1; i++) {
        int blen = (int)strlen(body);
        if (pos + blen < (int)sizeof(expanded) - 1) {
            memcpy(expanded + pos, body, blen);
            pos += blen;
        }
    }
    expanded[pos] = '\0';
    ls_push_lines(ctx->ls, expanded, ORIG_MACRO);
    return 0;
}

int macro_irp(MacroCtx *ctx, const char *dummy, const char *arglist, const char *body) {
    const char *args[256];
    char argbuf[4096];
    int nargs = parse_args(arglist, args, argbuf, sizeof(argbuf), 256);
    char dummies[1][SYM_NAME_BUF];
    snprintf(dummies[0], SYM_NAME_BUF, "%s", dummy);
    for (int i = 0; dummies[0][i]; i++)dummies[0][i] = toupper((unsigned char)dummies[0][i]);

    char expanded[MACRO_BODY_MAX * 4];
    int pos = 0;
    for (int i = 0; i < nargs; i++) {
        const char *a[1] = {args[i]};
        char sub[MACRO_BODY_MAX];
        substitute(body, sub, MACRO_BODY_MAX, dummies, 1, a, 1, ctx->symtab);
        int slen = (int)strlen(sub);
        if (pos + slen < (int)sizeof(expanded) - 1) {
            memcpy(expanded + pos, sub, slen);
            pos += slen;
        }
    }
    expanded[pos] = '\0';
    ls_push_lines(ctx->ls, expanded, ORIG_MACRO);
    return 0;
}

int macro_irpc(MacroCtx *ctx, const char *dummy, const char *str, const char *body) {
    char dummies[1][SYM_NAME_BUF];
    snprintf(dummies[0], SYM_NAME_BUF, "%s", dummy);
    for (int i = 0; dummies[0][i]; i++)
        dummies[0][i] = toupper((unsigned char)dummies[0][i]);

    char expanded[MACRO_BODY_MAX * 4];
    int pos = 0;
    for (int i = 0; str[i]; i++) {
        char ch[2] = {str[i], '\0'};
        const char *a[1] = {ch};
        char sub[MACRO_BODY_MAX];
        substitute(body, sub, MACRO_BODY_MAX, dummies, 1, a, 1, ctx->symtab);
        int slen = (int)strlen(sub);
        if (pos + slen < (int)sizeof(expanded) - 1) {
            memcpy(expanded + pos, sub, slen);
            pos += slen;
        }
    }
    expanded[pos] = '\0';
    ls_push_lines(ctx->ls, expanded, ORIG_MACRO);
    return 0;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
