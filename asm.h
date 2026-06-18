#ifndef ASM_H
#define ASM_H
#include "lines.h"
#include "symtab.h"
#include "segment.h"
#include "expr.h"
#include "macro.h"
#include "cond.h"
#include "codegen.h"
#include "relout.h"
#include "rexout.h"
#include "listing.h"

typedef enum { OUT_REL, OUT_REX } OutFormat;

typedef struct {
    LineSource ls;
    SymTab symtab;
    Segment segment;
    ExprCtx exprctx;
    MacroCtx macroctx;
    CondCtx condctx;

    const char *infile;
    const char *outfile;
    int pass;
    int z80_mode;
    int errors;
    int warnings;
    int has_start;
    int dseg_before_code;      /* DSEG appeared before first code byte */
    int dseg_used;             /* DSEG was entered at least once */
    int dseg_has_bytes;        /* data segment has actual emitted bytes */
    int dseg_emitting;         /* data bytes being emitted right now (since last DSEG switch) */
    int last_output_was_data;  /* last REL output was in data segment */
    int dseg_pending_setlc;    /* DSEG entered, emit SetLC on first byte */
    int dseg_setlc_addr;       /* address to use for pending SetLC */
    Value start_addr;
    char modname[8];

    RelOut rel;
    RexOut rex;
    FILE    *relfp;
    OutFormat outfmt;

    Listing listing;
    FILE    *lstfp;
} AsmCtx;

int asm_pass(AsmCtx *ctx);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
