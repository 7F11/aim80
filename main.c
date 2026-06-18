#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "asm.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <source[.mac]>\n"
            "Options:\n"
            "  -o outfile    Object output file (.rel or .rex)\n"
            "  -f rel|rex    Output format (default: rel)\n"
            "  -l lstfile    Generate listing file\n"
            "  -x            Include cross-reference in listing\n"
            "  -z            Start in Z80 mode (default: 8080)\n"
            , prog);
}

static void make_rel_name(const char *infile, char *out, int outsize, const char *ext) {
    snprintf(out, outsize, "%s", infile);
    char *dot = strrchr(out, '.');
    char *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash)) strcpy(dot, ext);
    else                                strcat(out, ext);
}

static void init_pass(AsmCtx *ctx, int pass) {
    ctx->pass = pass;
    ls_init(&ctx->ls);
    ls_open(&ctx->ls, ctx->infile);
    expr_init(&ctx->exprctx, &ctx->symtab, &ctx->segment);
    ctx->exprctx.pass = pass;
    ctx->exprctx.listing = (struct Listing*)&ctx->listing;
    macro_init(&ctx->macroctx, &ctx->symtab, &ctx->ls);
    cond_init(&ctx->condctx, &ctx->symtab, &ctx->exprctx, pass);
    segment_init(&ctx->segment);
    ctx->has_start = 0;
    ctx->symtab.local_counter = 0;
    if (pass !=2) return;
    for (int i = 0; i < SYMTAB_SIZE; i++)
        for (SymEntry*e = ctx->symtab.buckets[i]; e; e = e->next) {
            e->chain_head = 0;
            e->chain_mode = 0;
            e->ref_count  = 0;
            e->def_count  = 0;
        }
}

int main(int argc, char **argv) {
    const char *infile = NULL;
    const char *outfile = NULL;
    const char *lstfile = NULL;
    int initial_z80 = 0;
    int do_listing = 0, do_xref = 0;
    OutFormat outfmt = OUT_REL;
    int c;

    while ((c = getopt(argc, argv, "o:l:f:xz8h")) != -1)
        switch (c) {
        case 'o': outfile = optarg;
        break;
        case 'l': do_listing = 1;
        lstfile = optarg;
        break;
        case 'f':
            if (strcasecmp(optarg, "rex") == 0)
                outfmt = OUT_REX;
            else if (strcasecmp(optarg, "rel") == 0)
                outfmt = OUT_REL;
            else {
                fprintf(stderr, "Unknown format: %s\n", optarg);
                return 1;
            }
            break;
        case 'x': do_xref = 1;
        break;
        case 'z': initial_z80 = 1;
        break;
        case '8': initial_z80 = 0;
        break;
        case 'h': usage(argv[0]);
        return 0;
        default: usage(argv[0]);
        return 1;
        }
    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }
    infile = argv[optind];

    /* default .mac extension */
    char infile_buf[512];
    const char *dot = strrchr(infile, '.');
    const char *slash = strrchr(infile, '/');
    if (!dot || (slash && dot < slash)) {
        snprintf(infile_buf, sizeof(infile_buf), "%s.mac", infile);
        infile = infile_buf;
    }
    /* default output name */
    char rel_name[512];
    if (!outfile) {
        make_rel_name(infile, rel_name, sizeof(rel_name),
                      outfmt == OUT_REX ? ".rex" : ".rel");
        outfile = rel_name;
    }
    AsmCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.infile   = infile;
    ctx.outfile  = outfile;
    ctx.outfmt   = outfmt;
    ctx.z80_mode = initial_z80;
    symtab_init(&ctx.symtab);

    /* derive module name from filename */
    const char *base = strrchr(infile, '/');
    base = base ? base + 1 : infile;
    snprintf(ctx.modname, sizeof(ctx.modname), "%.6s", base);
    char *mdot = strchr(ctx.modname, '.');
    if (mdot) *mdot = '\0';

    for (int i = 0; ctx.modname[i]; i++)
        ctx.modname[i] = toupper((unsigned char)ctx.modname[i]);

    /* === PASS 1 === */
    init_pass(&ctx, 1);
    asm_pass(&ctx);

    if (ctx.errors > 0) {
        fprintf(stderr, "%d error(s) in pass 1\n", ctx.errors);
        symtab_free(&ctx.symtab);
        return 1;
    }

    /* === PASS 2 === */
    int p1_code_size = ctx.segment.code_loc;
    int p1_data_size = ctx.segment.data_loc;
    /* set common block sizes from pass 1 LCs */
    for (int i = 0; i < ctx.symtab.common_count; i++)
        ctx.symtab.commons[i].size = ctx.segment.common_loc[i];

    ctx.errors = 0;
    init_pass(&ctx, 2);

    /* open object output */
    ctx.relfp = fopen(outfile, "wb");
    if (!ctx.relfp) {
        perror(outfile);
        symtab_free(&ctx.symtab);
        return 1;
    }
    if (outfmt == OUT_REX) {
        rex_init(&ctx.rex, ctx.relfp);
        rex_write_header(&ctx.rex);
        rex_module_name(&ctx.rex, ctx.modname);
    }
    else {
        rel_init(&ctx.rel, ctx.relfp);
        /* init deferred data segment buffer (disabled pending investigation) */
        rel_program_name(&ctx.rel, ctx.modname);
        /* emit entry symbols (type 0) in declaration order */
        SymEntry *sorted[1024];
        int ns = 0;
        for (int i = 0; i < SYMTAB_SIZE; i++)
            for (SymEntry *e = ctx.symtab.buckets[i]; e; e = e->next)
                if (e->is_public && e->defined && e->type != SYM_EXTERNAL && ns < 1024)
                    sorted[ns++] = e;
        for (int i = 0; i < ns - 1; i++)
            for (int j = i + 1; j < ns; j++) {
                if (sorted[i]->order <= sorted[j]->order) continue;
                SymEntry *t = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = t;
            }
        for (int i = 0; i < ns; i++)
            rel_entry_symbol(&ctx.rel, sorted[i]->name);
        /* emit segment sizes before code (M80 compatibility) */
        /* emit common block sizes first */
        for (int i = 0; i < ctx.symtab.common_count; i++)
            if (ctx.symtab.commons[i].size > 0)
                rel_define_common_size(&ctx.rel, ctx.symtab.commons[i].name, ctx.symtab.commons[i].size);

        rel_define_data_size(&ctx.rel, p1_data_size);
        if (p1_code_size > 0) rel_define_code_size(&ctx.rel, p1_code_size);

        int has_externals = 0;
        for (int i = 0; i < SYMTAB_SIZE && !has_externals; i++)
            for (SymEntry *e = ctx.symtab.buckets[i]; e; e = e->next)
                if (e->type == SYM_EXTERNAL) {
                    has_externals = 1;
                    break;
                }
        /* emit SetLC records for data segment allocations and code start */
        if (p1_data_size > 0 && ctx.dseg_before_code && !ctx.dseg_has_bytes) {
            /* DS-only data before code: inline pass 2 handles SetLC emission */
        }
        else if (p1_data_size > 0 && ctx.dseg_before_code && ctx.dseg_has_bytes) {
            /* Data with bytes before code: pass 2 emits DSEG content inline */
        }
        else if (p1_data_size > 0 && ctx.dseg_has_bytes) {
            /* DSEG after code with actual bytes — pass 2 handles inline */
        }
    }
    /* open listing file */
    if (do_listing) {
        char lst_name[512];
        if (!lstfile) {
            snprintf(lst_name, sizeof(lst_name), "%s", infile);
            char *d = strrchr(lst_name, '.');
            char *s = strrchr(lst_name, '/');
            if (d && (!s || d > s)) strcpy(d, ".prn");
            else                    strcat(lst_name, ".prn");
            lstfile = lst_name;
        }
        ctx.lstfp = fopen(lstfile, "w");
        if (!ctx.lstfp)
            perror(lstfile);
        else {
            lst_init(&ctx.listing, ctx.lstfp);
            if (do_xref) ctx.listing.xref_enabled = 1;
        }
    }
    asm_pass(&ctx);

    /* emit segment sizes, symbols, end */
    if (outfmt == OUT_REX) {
        rex_flush_code(&ctx.rex);
        rex_code_size(&ctx.rex, ctx.segment.code_loc);
        if (ctx.segment.data_loc > 0)
            rex_data_size(&ctx.rex, ctx.segment.data_loc);

        for (int i = 0; i < ctx.symtab.common_count; i++)
            if (ctx.symtab.commons[i].size > 0)
                rex_common_def(&ctx.rex, ctx.symtab.commons[i].name,
                               ctx.symtab.commons[i].size);

        for (int i = 0; i < SYMTAB_SIZE; i++)
            for (SymEntry *e = ctx.symtab.buckets[i]; e; e = e->next)
                if (e->is_public && e->defined && e->type != SYM_EXTERNAL)
                    rex_entry_symbol(&ctx.rex, e->name,
                                     valmode_to_rex_seg(e->val.mode), e->val.num);

        for (int i = 0; i < SYMTAB_SIZE; i++)
            for (SymEntry *e = ctx.symtab.buckets[i]; e; e = e->next)
                if (e->type == SYM_EXTERNAL && e->ref_count > 0)
                    rex_extern_ref(&ctx.rex, e->name, e->chain_mode, e->chain_head);

        rex_end_module(&ctx.rex, ctx.has_start,
                       ctx.has_start ? valmode_to_rex_seg(ctx.start_addr.mode) : 0,
                       ctx.has_start ? ctx.start_addr.num : 0);
        rex_eof(&ctx.rex);
    }
    fclose(ctx.relfp);

    /* finalize listing */
    if (ctx.lstfp) {
        lst_symbol_table(&ctx.listing, &ctx.symtab);
        lst_xref_output(&ctx.listing);
        if (ctx.warnings)
            fprintf(ctx.lstfp, "\n%d Fatal error(s),%d Warning(s)\n", ctx.errors, ctx.warnings);
        else if (ctx.errors)
            fprintf(ctx.lstfp, "\n%d Fatal error(s)\n", ctx.errors);
        else
            fprintf(ctx.lstfp, "\nNo Fatal error(s)\n");

        fclose(ctx.lstfp);
    }
    if (ctx.errors > 0) {
        if (ctx.warnings)
            fprintf(stderr, "%d Fatal error(s),%d Warning(s)\n", ctx.errors, ctx.warnings);
        else
            fprintf(stderr, "%d error(s)\n", ctx.errors);

        symtab_free(&ctx.symtab);
        return 1;
    }
    fprintf(stderr, "No errors. %s created.\n", outfile);
    symtab_free(&ctx.symtab);
    return 0;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
