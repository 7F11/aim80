#ifndef MACRO_H
#define MACRO_H
#include "symtab.h"
#include "lines.h"

/* MacroDef is defined in symtab.h */

typedef struct MacroDef {
    SymTab *symtab;
    LineSource *ls;
} MacroCtx;

void macro_init(MacroCtx *ctx, SymTab *symtab, LineSource *ls);

/* collect macro body lines until ENDM (called after MACRO keyword seen).
   Reads lines from ls. */
int macro_collect(MacroCtx *ctx, const char *name, const char *params, LineSource *ls);

/* expand macro call, push resulting lines onto ls */
int macro_expand(MacroCtx *ctx, const char *name, const char *args);

/* expand REPT/IRP/IRPC and push lines */
int macro_rept(MacroCtx *ctx, int count, const char *body);
int macro_irp(MacroCtx *ctx, const char *dummy, const char *arglist, const char *body);
int macro_irpc(MacroCtx *ctx, const char *dummy, const char *str, const char *body);

/* collect body lines until ENDM, return body text. Caller provides buffer. */
int macro_collect_body(LineSource *ls, char *body, int maxlen);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
