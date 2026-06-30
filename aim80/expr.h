#ifndef EXPR_H
#define EXPR_H
#include "symtab.h"
#include "segment.h"

typedef enum {
    EXPR_OK,
    EXPR_UNDEFINED,
    EXPR_MODE_ERROR,
    EXPR_EXTERN_ERROR,
    EXPR_SYNTAX,
    EXPR_DIVZERO
} ExprError;

typedef struct {
    SymTab  *symtab;
    Segment *segment;
    int radix;
    int pass;
    int create_fwdref;       /* 1 = create undefined entries on first lookup (instruction context) */
    char errmsg[128];
    int num_error;           /* bad digit detected in number literal */
    int reloc_error;         /* illegal relocation in expression */
    int ext_error;           /* external used in illegal context */
    struct Listing *listing; /* for cross-reference recording */
    int src_line;            /* current source line number */
} ExprCtx;

void      expr_init(ExprCtx *ctx, SymTab *symtab, Segment *segment);

/* Evaluate expression from string. *endp is updated to point past consumed chars. */
ExprError expr_eval_str(ExprCtx *ctx, const char *str, const char **endp, Value *result);

/* Convenience: evaluate entire string */
ExprError expr_eval(ExprCtx *ctx, const char *str, Value *result);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
