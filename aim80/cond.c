#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "cond.h"
static char errbuf[128];

void cond_init(CondCtx *ctx, SymTab *symtab, ExprCtx *expr, int pass) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->symtab = symtab;
    ctx->expr = expr;
    ctx->pass = pass;
    /* level 0: always active */
    ctx->stack[0].active = 1;
    ctx->stack[0].parent_active = 1;
}

int cond_active(CondCtx *ctx) {
    return ctx->stack[ctx->depth].active;
}

/* extract angle-bracket delimited arg: <text> → text */
static const char *strip_angles(const char *s, char *buf, int bufsize) {
    while (isspace(*s)) s++;

    if (*s == '<') {
        s++;
        int i = 0;
        while (*s && *s != '>' && i < bufsize - 1) buf[i++] = *s++;
        buf[i] = '\0';
        return buf;
    }
    /* no angles — return as-is */
    strncpy(buf, s, bufsize - 1);
    buf[bufsize - 1] = '\0';
    return buf;
}

static void push_level(CondCtx *ctx, int active) {
    if (ctx->depth >= COND_NEST_MAX - 1) {
        snprintf(errbuf, sizeof(errbuf), "Conditional nesting overflow");
        return;
    }
    int parent = ctx->stack[ctx->depth].active;
    ctx->depth++;
    ctx->stack[ctx->depth].parent_active = parent;
    ctx->stack[ctx->depth].active        = parent && active;
    ctx->stack[ctx->depth].seen_else     = 0;
}

int cond_process(CondCtx *ctx, const char *directive, const char *arg) {
    errbuf[0] = '\0';

    /* ENDIF */
    if (strcasecmp(directive, "ENDIF") == 0) {
        if (ctx->depth <= 0) {
            snprintf(errbuf, sizeof(errbuf), "ENDIF without IF");
            return -1;
        }
        ctx->depth--;
        return 1;
    }
    /* ELSE */
    if (strcasecmp(directive, "ELSE") == 0) {
        if (ctx->depth <= 0) {
            snprintf(errbuf, sizeof(errbuf), "ELSE without IF");
            return -1;
        }
        if (ctx->stack[ctx->depth].seen_else) {
            snprintf(errbuf, sizeof(errbuf), "Multiple ELSE for one IF");
            return -1;
        }
        ctx->stack[ctx->depth].seen_else = 1;
        /* flip: active becomes inactive and vice versa, but only if parent is active */
        if (ctx->stack[ctx->depth].parent_active)
            ctx->stack[ctx->depth].active = !ctx->stack[ctx->depth].active;

        return 1;
    }
    /* all IF variants — if we're in a false block, just push inactive */
    if (!cond_active(ctx)) {
        /* still need to track nesting */
        if (strncasecmp(directive, "IF", 2) == 0) {
            push_level(ctx, 0);
            return 1;
        }
        return 0;
    }
    /* IF / IFT */
    if (strcasecmp(directive, "IF") == 0 || strcasecmp(directive, "IFT") == 0) {
        /* Handle IF NUL <arg> specially */
        const char *a = arg; while (isspace(*a)) a++;

        if (strncasecmp(a, "NUL", 3) == 0 && (a[3] == ' ' || a[3] == '\t' || a[3] == 0 || a[3] == ';')) {
            a += 3;
            while (isspace(*a)) a++;
            push_level(ctx, (!*a || *a == ';') ? 1 : 0);
        }
        else {
            Value val;
            ExprError err = expr_eval(ctx->expr, arg, &val);
            if (err != EXPR_OK)
                push_level(ctx, 0);
            else
                push_level(ctx, val.num != 0);
        }
        return 1;
    }
    /* IFE / IFF */
    if (strcasecmp(directive, "IFE") == 0 || strcasecmp(directive, "IFF") == 0) {
        Value val;
        ExprError err = expr_eval(ctx->expr, arg, &val);
        push_level(ctx, err != EXPR_OK ? 0 : val.num == 0);
        return 1;
    }
    /* IF1 */
    if (strcasecmp(directive, "IF1") == 0) {
        push_level(ctx, ctx->pass == 1);
        return 1;
    }
    /* IF2 */
    if (strcasecmp(directive, "IF2") == 0) {
        push_level(ctx, ctx->pass == 2);
        return 1;
    }
    /* IFDEF */
    if (strcasecmp(directive, "IFDEF") == 0) {
        char sym[SYM_NAME_BUF];
        const char *p = arg;
        while (isspace(*p)) p++;

        int i = 0;
        while (*p && !isspace(*p) && *p != ';' && i < SYM_NAME_BUF - 1) sym[i++] = *p++;
        sym[i] = '\0';
        SymEntry *e = symtab_lookup(ctx->symtab, sym);
        push_level(ctx, e && (e->defined || e->type == SYM_EXTERNAL));
        return 1;
    }
    /* IFNDEF */
    if (strcasecmp(directive, "IFNDEF") == 0) {
        char sym[SYM_NAME_BUF];
        const char *p = arg;
        while (isspace(*p)) p++;

        int i = 0;
        while (*p && !isspace(*p) && *p != ';' && i < SYM_NAME_BUF - 1) sym[i++] = *p++;
        sym[i] = '\0';
        SymEntry *e = symtab_lookup(ctx->symtab, sym);
        push_level(ctx, !e || (!e->defined && e->type != SYM_EXTERNAL));
        return 1;
    }
    /* IFB */
    if (strcasecmp(directive, "IFB") == 0) {
        char buf[256];
        strip_angles(arg, buf, sizeof(buf));
        /* blank = empty or only whitespace */
        const char *p = buf;
        while (isspace(*p)) p++;

        push_level(ctx, *p == '\0');
        return 1;
    }
    /* IFNB */
    if (strcasecmp(directive, "IFNB") == 0) {
        char buf[256];
        strip_angles(arg, buf, sizeof(buf));
        const char *p = buf;
        while (isspace(*p)) p++;

        push_level(ctx, *p != '\0');
        return 1;
    }
    /* IFIDN */
    if (strcasecmp(directive, "IFIDN") == 0) {
        /* arg is <arg1>, <arg2> */
        char a1[256], a2[256];
        const char *p = arg;
        while (isspace(*p)) p++;

        strip_angles(p, a1, sizeof(a1));
        /* find comma after first > */
        while (*p && *p != ',') p++;
        if (*p == ',') p++;

        strip_angles(p, a2, sizeof(a2));
        push_level(ctx, strcasecmp(a1, a2) == 0);
        return 1;
    }
    /* IFDIF */
    if (strcasecmp(directive, "IFDIF") == 0) {
        char a1[256], a2[256];
        const char *p = arg;
        while (isspace(*p)) p++;

        strip_angles(p, a1, sizeof(a1));
        while (*p && *p != ',') p++;
        if (*p == ',') p++;

        strip_angles(p, a2, sizeof(a2));
        push_level(ctx, strcasecmp(a1, a2) != 0);
        return 1;
    }
    /* Z80 compat: COND = IFT, ENDC = ENDIF */
    if (strcasecmp(directive, "COND") == 0) return cond_process(ctx, "IFT", arg);
    if (strcasecmp(directive, "ENDC") == 0) return cond_process(ctx, "ENDIF", arg);

    return 0; /* not a conditional directive */
}

const char *cond_error(CondCtx *ctx) {
    (void)ctx;
    return errbuf[0] ? errbuf : NULL;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
