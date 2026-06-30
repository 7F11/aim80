#ifndef COND_H
#define COND_H
#include "symtab.h"
#include "expr.h"

#define COND_NEST_MAX 256

typedef struct {
    int active;         /* is this level's true branch active? */
    int seen_else;      /* have we encountered ELSE? */
    int parent_active;  /* was the enclosing level active? */
} CondLevel;

typedef struct {
    CondLevel stack[COND_NEST_MAX];
    int depth;
    int pass;           /* 1 or 2 */
    SymTab   *symtab;
    ExprCtx  *expr;
} CondCtx;

void cond_init(CondCtx *ctx, SymTab *symtab, ExprCtx *expr, int pass);
int  cond_active(CondCtx *ctx);     /* is current level active? */
int  cond_process(CondCtx *ctx, const char *directive, const char *arg);
/* returns: 1 = was a conditional directive (handled), 0 = not */
const char *cond_error(CondCtx *ctx);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
