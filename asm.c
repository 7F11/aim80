#define DBG 0
#if DBG
#define TRACE(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define TRACE_IF(cond, fmt, ...) do{if(cond)fprintf(stderr, fmt, ##__VA_ARGS__);}while(0)
#else
#define TRACE(fmt, ...) ((void)0)
#define TRACE_IF(cond, fmt, ...) ((void)0)
#endif
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "asm.h"

/* emit byte to REL/REX and listing */
static void emit_byte(AsmCtx *ctx, int b) {
    if (ctx->pass != 2) {
        if (segment_mode(&ctx->segment) == MODE_DATA_REL) ctx->dseg_has_bytes = 1;
        return;
    }
    if (ctx->outfmt == OUT_REX)
        rex_code_byte(&ctx->rex, b & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)),
                      segment_get_loc(&ctx->segment) - 1);
    else {
        ctx->dseg_emitting = 1;
        ctx->last_output_was_data = (segment_mode(&ctx->segment) == MODE_DATA_REL);
        rel_abs_byte(&ctx->rel, b & 0xFF);
    }
    lst_add_byte(&ctx->listing, b);
}

/* emit 16-bit word with relocation */
static void emit_word(AsmCtx *ctx, Value *val) {
    if (ctx->pass != 2) {
        if (segment_mode(&ctx->segment) == MODE_DATA_REL) ctx->dseg_has_bytes = 1;
        return;
    }
    if (val->external) {
        SymEntry *e = symtab_lookup_sym(&ctx->symtab, val->ext_name);
        int lst_nb_save = ctx->listing.stmt_nbytes; /* save for listing fixup */
        if (e) {
            if (ctx->outfmt == OUT_REX) {
                /* REX: just emit chain pointer bytes */
                int loc = segment_get_loc(&ctx->segment) - 2;
                int prev = e->chain_head;
                e->chain_head = loc;
                e->chain_mode = (int)segment_mode(&ctx->segment);
                e->ref_count++;
                int cv = prev & 0xFFFF;
                rex_code_byte(&ctx->rex, cv & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)), loc);
                rex_code_byte(&ctx->rex, (cv >> 8) & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)), loc + 1);
            }
            else if (val->complex_reloc && ctx->outfmt == OUT_REL) {
                /* complex expression (ext - reloc - const): emit RPN extension */
                rel_ext_operand_ext(&ctx->rel, e->name);
                rel_ext_operand(&ctx->rel, 0, MODE_DATA_REL);
                rel_ext_operator(&ctx->rel, 7); /* "-" */
                if (val->num != 0) {
                    rel_ext_operand(&ctx->rel, (-val->num) & 0xFFFF, MODE_ABSOLUTE);
                    rel_ext_operator(&ctx->rel, 7); /* "-" */
                }
                rel_ext_operator(&ctx->rel, 2); /* "WORD" */
                rel_abs_byte(&ctx->rel, 0x00);
                rel_abs_byte(&ctx->rel, 0x00);
                e->ref_count++;
            }
            else if (val->num != 0 && ctx->outfmt == OUT_REL) {
                /* non-zero offset: emit ext_plus_offset in place of chain word */
                int loc = segment_get_loc(&ctx->segment) - 2;
                int prev = e->chain_head;
                e->chain_head = loc;
                e->chain_mode = (int)segment_mode(&ctx->segment);
                e->ref_count++;
                rel_ext_plus_offset(&ctx->rel, e->name, val->num, MODE_ABSOLUTE);
                /* emit chain pointer */
                int cv = prev & 0xFFFF;
                if (prev != 0)
                    rel_reloc_word(&ctx->rel, cv, segment_mode(&ctx->segment));
                else {
                    rel_abs_byte(&ctx->rel, cv & 0xFF);
                    rel_abs_byte(&ctx->rel, (cv >> 8) & 0xFF);
                }
            }
            else {
                int loc = segment_get_loc(&ctx->segment) - 2;
                int prev = e->chain_head;
                e->chain_head = loc;
                e->chain_mode = (int)segment_mode(&ctx->segment);
                e->ref_count++;
                int cv = prev & 0xFFFF;
                if (prev != 0) {
                    rel_reloc_word(&ctx->rel, cv, segment_mode(&ctx->segment));
                    ctx->last_output_was_data = (segment_mode(&ctx->segment) == MODE_DATA_REL);
                    ctx->dseg_emitting = 1;
                }
                else {
                    emit_byte(ctx, cv & 0xFF);
                    emit_byte(ctx, (cv >> 8) & 0xFF);
                }
            }
        }
        else {
            emit_byte(ctx, val->num & 0xFF);
            emit_byte(ctx, (val->num >> 8) & 0xFF);
        }
        /* fix listing: show 0000* regardless of which REL path was taken */
        if (ctx->pass == 2) {
            ctx->listing.stmt_nbytes = lst_nb_save;
            lst_add_reloc_word(&ctx->listing, 0, MODE_EXTERNAL);
        }
    }
    else if (!val_is_absolute(*val)) {
        if (ctx->outfmt == OUT_REX) {
            int off = segment_get_loc(&ctx->segment) - 2;
            int seg_at = valmode_to_rex_seg(segment_mode(&ctx->segment));
            int seg_to = valmode_to_rex_seg(val->mode);
            rex_code_byte(&ctx->rex, val->num & 0xFF, seg_at, off);
            rex_code_byte(&ctx->rex, (val->num >> 8) & 0xFF, seg_at, off + 1);
            if (seg_to != seg_at || seg_to != 0)
                rex_reloc_fixup(&ctx->rex, seg_at, off, seg_to, 0, val->num);
        }
        else {
            if (val->mode == MODE_COMMON && ctx->outfmt == OUT_REL)
                rel_select_common(&ctx->rel, ctx->symtab.commons[val->common_id].name);
            rel_reloc_word(&ctx->rel, val->num, val->mode);
        }
        lst_add_reloc_word(&ctx->listing, val->num, val->mode);
    }
    else {
        emit_byte(ctx, val->num & 0xFF);
        emit_byte(ctx, (val->num >> 8) & 0xFF);
    }
}

static void emit_instr(AsmCtx *ctx, Instruction *inst) {
    int base_offset = segment_get_loc(&ctx->segment); /* address of first byte */
    segment_advance(&ctx->segment, inst->len);
    if (ctx->pass != 2) return;

    /* For REX: set correct starting offset for this instruction */
    if (ctx->outfmt == OUT_REX) {
        if (ctx->rex.codelen > 0 && (ctx->rex.code_offset + ctx->rex.codelen) != base_offset)
            rex_flush_code(&ctx->rex);

        if (ctx->rex.codelen == 0)
            ctx->rex.code_offset = base_offset;
    }
    if (inst->has_reloc && inst->reloc_pos >= 0) {
        for (int i = 0; i < inst->reloc_pos; i++) {
            if (ctx->outfmt == OUT_REX)
                rex_code_byte(&ctx->rex, inst->bytes[i] & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)),
                              base_offset + i);
            else
                emit_byte(ctx, inst->bytes[i]);
        }
        if (inst->reloc_size == 1 && !val_is_absolute(inst->reloc_val) && ctx->outfmt == OUT_REL) {
            /* byte-sized relocatable: emit extension link items */
            if (inst->reloc_val.external)
                rel_ext_operand_ext(&ctx->rel, inst->reloc_val.ext_name);
            else
                rel_ext_operand(&ctx->rel, inst->reloc_val.num, inst->reloc_val.mode);

            if (inst->reloc_val.byte_op == 2)
                rel_ext_operator(&ctx->rel, 3); /* HIGH */
            else if (inst->reloc_val.byte_op == 1)
                rel_ext_operator(&ctx->rel, 4); /* LOW */
            rel_ext_operator(&ctx->rel, 1); /* BYTE */
            rel_abs_byte(&ctx->rel, 0x00);
        }
        else if (inst->reloc_size == 1) {
            if (ctx->outfmt == OUT_REX)
                rex_code_byte(&ctx->rex, inst->bytes[inst->reloc_pos] & 0xFF,
                              valmode_to_rex_seg(segment_mode(&ctx->segment)), base_offset + inst->reloc_pos);
            else
                emit_byte(ctx, inst->bytes[inst->reloc_pos]);
        }
        else
            emit_word(ctx, &inst->reloc_val);
        for (int i = inst->reloc_pos + inst->reloc_size; i < inst->len; i++) {
            if (ctx->outfmt == OUT_REX)
                rex_code_byte(&ctx->rex, inst->bytes[i] & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)),
                              base_offset + i);
            else
                emit_byte(ctx, inst->bytes[i]);
        }
    }
    else {
        if (ctx->outfmt == OUT_REX)
            for (int i = 0; i < inst->len; i++)
                rex_code_byte(&ctx->rex, inst->bytes[i] & 0xFF, valmode_to_rex_seg(segment_mode(&ctx->segment)),
                              base_offset + i);
        else
            for (int i = 0; i < inst->len; i++)emit_byte(ctx, inst->bytes[i]);
    }
}

/* parse operands for Z80/8080 instruction encoding */
static void do_opcode(AsmCtx *ctx, const char *opcode, const char *args, SrcLine *sl) {
    char op1[32] = "", op2[32] = "";
    Value val1 = val_absolute(0), val2 = val_absolute(0);
    int has_val1 = 0, has_val2 = 0, ind1 = 0, ind2 = 0;
    Value idx1 = val_absolute(0), idx2 = val_absolute(0);
    int has_idx1 = 0, has_idx2 = 0;

    if (!args || !*args) {
        Instruction inst;
        int rc = (ctx->cpu_mode & INST_SET_Z80) ?
                 encode_z80(ctx->cpu_mode, opcode, NULL, NULL, NULL, NULL, 0, 0, NULL, NULL, &inst) :
                 encode_8080(ctx->cpu_mode, opcode, NULL, NULL, NULL, NULL, &inst);
        if (rc == 0)
            emit_instr(ctx, &inst);
        else if (ctx->pass == 2) {
            fprintf(stderr, "%s:%d: Bad opcode: %s\n", sl->file, sl->line, opcode);
            ctx->errors++;
            ctx->listing.stmt_error = 'O';
        }
        return;
    }

    /* split args by comma (respecting parens) */
    char abuf[256];
    snprintf(abuf, sizeof(abuf), "%s", args);
    char *a1 = abuf, *a2 = NULL;
    int depth = 0;
    for (char *p = abuf; *p; p++) {
        if (*p == '(' || *p == '<')
            depth++;
        else if (*p == ')' || *p == '>')
            depth--;
        else if (*p == '\'' || *p == '"') {
            char q = *p;
            p++;
            while (*p && *p != q) p++;
            if (!*p) break;
        }
        else if (*p == ',' && depth == 0) {
            *p = '\0';
            a2 = p + 1;
            break;
        }
    }

    /* parse operand into reg/value/indirect */
    #define PARSE_OP(str, opbuf, val, has_v, ind, idx, has_i) do { \
        const char *_s = str; while(isspace(*_s)) _s++; \
        if (*_s=='(') { ind=1; _s++; while(isspace(*_s)) _s++; \
            if (*_s=='(' && ((_s[1]|0x20)=='i') && ((_s[2]|0x20)=='x'||(_s[2]|0x20)=='y')) _s++; } \
            /* check for register */ \
        const char *_e = _s; \
        while(isalnum((unsigned char)*_e)||*_e=='\'') _e++; \
        int _rlen=(int)(_e-_s); char _rb[16]=""; \
        if(_rlen>0&&_rlen<16){memcpy(_rb,_s,_rlen);_rb[_rlen]='\0';} \
        int _is_reg = 0; \
        if (ctx->cpu_mode & INST_SET_Z80) { \
            _is_reg = (z80_reg8(_rb)>=0||z80_reg16(_rb)>=0||z80_reg16af(_rb)>=0|| \
                       z80_cond(_rb)>=0||z80_is_ix(_rb)||z80_is_iy(_rb)|| \
                       strcasecmp(_rb,"I")==0||strcasecmp(_rb,"R")==0|| \
                       strcasecmp(_rb,"AF'")==0); \
        } else { _is_reg = (reg8080(_rb)>=0||regpair(_rb)>=0); } \
        if (_is_reg && (!*_e || *_e==')'||*_e=='+' ||*_e=='-')) { \
            snprintf(opbuf,32,"%.*s",_rlen,_s); \
            _s = _e; \
            if (ind && (z80_is_ix(opbuf)||z80_is_iy(opbuf))) { \
                if (*_s=='+'||*_s=='-') { \
                    int sign=(*_s=='-')?-1:1; _s++; \
                    const char *_end; Value _iv; \
                    expr_eval_str(&ctx->exprctx, _s, &_end, &_iv); \
                    idx.num = (sign<0) ? ((-_iv.num)&0xFF) : (_iv.num&0xFF); \
                    has_i=1; _s=_end; \
                } else { has_i=1; idx=val_absolute(0); } \
            } \
            if (ind && *_s==')') { _s++; if(*_s==')') _s++; } \
        } else { \
            /* expression */ \
            const char *_end; \
            int _rc = expr_eval_str(&ctx->exprctx, _s, &_end, &val); has_v=1; \
            if (_rc != EXPR_OK && ctx->pass==2) { \
                while(isspace(*_s))_s++; \
                if (*_s&&*_s!=','&&*_s!=')') { ctx->listing.stmt_error='U'; ctx->errors++; } \
                else { ctx->listing.stmt_error='Q'; ctx->warnings++; } \
            } \
            _s=_end; if(ind && *_s==')') _s++; \
        } \
    } while(0)

    ctx->exprctx.create_fwdref = (ctx->pass == 1) ? 1 : 0;
    ctx->exprctx.num_error   = 0;
    ctx->exprctx.reloc_error = 0;
    ctx->exprctx.ext_error   = 0;
    PARSE_OP(a1, op1, val1, has_val1, ind1, idx1, has_idx1);
    if (a2) PARSE_OP(a2, op2, val2, has_val2, ind2, idx2, has_idx2);

    ctx->exprctx.create_fwdref = 0;

    #undef PARSE_OP

    if (ctx->pass == 2) {
        if (ctx->exprctx.num_error) {
            ctx->listing.stmt_error = 'N';
            ctx->errors++;
        }
        else if (ctx->exprctx.ext_error == 2) {
            ctx->listing.stmt_error = 'D';
            ctx->errors++;
        }
        else if (ctx->exprctx.reloc_error) {
            ctx->listing.stmt_error = 'R';
            ctx->errors++;
        }
        else if (ctx->exprctx.ext_error == 1) {
            ctx->listing.stmt_error = 'E';
            ctx->errors++;
        }
    }
    /* JR/DJNZ relative offset */
    if ((ctx->cpu_mode & INST_SET_Z80) && (!strcasecmp(opcode, "JR") || !strcasecmp(opcode, "DJNZ"))) {
        Value *tgt = has_val2 ? &val2 : (has_val1 ? &val1 : NULL);
        if (tgt) {
            int pc = segment_get_loc(&ctx->segment) + 2;
            tgt->num = (tgt->num - pc) & 0xFF;
        }
    }

    Instruction inst;
    int rc = (ctx->cpu_mode & INST_SET_Z80) ?
        encode_z80(ctx->cpu_mode, opcode,
                    op1[0]   ? op1   : NULL,
                    op2[0]   ? op2   : NULL,
                    has_val1 ? &val1 : NULL,
                    has_val2 ? &val2 : NULL,
                    ind1,
                    ind2,
                    has_idx1 ? &idx1 : NULL,
                    has_idx2 ? &idx2 : NULL,
                    &inst
        )                                   :
        encode_8080(ctx->cpu_mode, opcode,
                    op1[0]   ? op1   : NULL,
                    op2[0]   ? op2   : NULL,
                    has_val1 ? &val1 : NULL,
                    has_val2 ? &val2 : NULL,
                    &inst
        );

    if (rc == 0)
        emit_instr(ctx, &inst);
    else {
        if (ctx->pass == 2) {
            fprintf(stderr, "%s:%d: Cannot encode: %s %s\n",
                sl->file, sl->line, opcode, args ? args : "");
            ctx->errors++;
            ctx->listing.stmt_error = 'Q';
        }
        segment_advance(&ctx->segment, 1);
    }
}

/* check if op is a known opcode */
static int is_opcode(const char *op, int mode) {
    static const char *ops8080[] = {
        "ACI", "ADC", "ADD", "ADI", "ANA", "ANI", "CALL", "CC", "CM", "CMA", "CMC",
        "CMP", "CNC", "CNZ", "CP", "CPE", "CPI", "CPO", "CZ", "DAA", "DAD", "DCR",
        "DCX", "DI", "EI", "HLT", "IN", "INR", "INX", "JC", "JM", "JMP", "JNC",
        "JNZ", "JP", "JPE", "JPO", "JZ", "LDA", "LDAX", "LHLD", "LXI", "MOV",
        "MVI", "NOP", "ORA", "ORI", "OUT", "PCHL", "POP", "PUSH", "RAL", "RAR",
        "RC", "RET", "RLC", "RM", "RNC", "RNZ", "RP", "RPE", "RPO", "RRC", "RST",
        "RZ", "SBB", "SBI", "SHLD", "SPHL", "STA", "STAX", "STC", "SUB", "SUI",
        "XCHG", "XRA", "XRI", "XTHL", NULL
    };
    static const char *ops8085[] = { "RIM", "SIM", NULL };
    static const char *opsz80[] = {
        "ADC", "ADD", "AND", "BIT", "CALL", "CCF", "CP", "CPD", "CPDR", "CPI",
        "CPIR", "CPL", "DAA", "DEC", "DI", "DJNZ", "EI", "EX", "EXX", "HALT",
        "IM", "IN", "INC", "IND", "INDR", "INI", "INIR", "JP", "JR", "LD",
        "LDD", "LDDR", "LDI", "LDIR", "NEG", "NOP", "OR", "OTDR", "OTIR", "OUT",
        "OUTD", "OUTI", "POP", "PUSH", "RES", "RET", "RETI", "RETN", "RL", "RLA",
        "RLC", "RLCA", "RLD", "RR", "RRA", "RRC", "RRCA", "RRD", "RST", "SBC",
        "SCF", "SET", "SLA", "SRA", "SRL", "SUB", "XOR", NULL
    };
    static const char *opsz180[] = {
        "MLT", "TST", "TSTIO", "SLP", "IN0", "OUT0",
        "OTIM", "OTDM", "OTIMR", "OTDMR", NULL
    };
    static const char *opsz80u[] = { "SLL", NULL };
    static const char *opsr800[] = { "MULUB", "MULUW", NULL };
    static const char *opsnext[] = {
        "MUL", "SWAPNIB", "MIRROR", "TEST", "NEXTREG",
        "PIXELDN", "PIXELAD", "SETAE", "OUTINB",
        "LDIX", "LDIRX", "LDDX", "LDDRX", "LDPIRX", "LDIRSCALE",
        "BSLA", "BSRA", "BSRL", "BSRF", "BRLC", NULL
    };
    if (mode & INST_SET_Z80)
        for (int i = 0; opsz80[i]; i++)
            if (strcasecmp(op, opsz80[i]) == 0) return 1;
    if (mode & INST_SET_Z80U)
        for (int i = 0; opsz80u[i]; i++)
            if (strcasecmp(op, opsz80u[i]) == 0) return 1;
    if (mode & INST_SET_Z180)
        for (int i = 0; opsz180[i]; i++)
            if (strcasecmp(op, opsz180[i]) == 0) return 1;
    if (mode & INST_SET_R800)
        for (int i = 0; opsr800[i]; i++)
            if (strcasecmp(op, opsr800[i]) == 0) return 1;
    if (mode & INST_SET_ZXNEXT)
        for (int i = 0; opsnext[i]; i++)
            if (strcasecmp(op, opsnext[i]) == 0) return 1;
    if (mode & INST_SET_8080)
        for (int i = 0; ops8080[i]; i++)
            if (strcasecmp(op, ops8080[i]) == 0) return 1;
    if (mode & INST_SET_8085)
        for (int i = 0; ops8085[i]; i++)
            if (strcasecmp(op, ops8085[i]) == 0) return 1;
    return 0;
}

/* collect macro/rept body lines, listing each one */
static int collect_body_listed(AsmCtx *ctx, char *body, int maxlen) {
    int pos = 0, nest = 1;
    SrcLine sl;
    while (nest > 0 && ls_getline(&ctx->ls, &sl) == 0) {
        char saved[LINE_MAX];
        snprintf(saved, sizeof(saved), "%s", sl.raw);
        line_split(&sl);
        const char *op = sl.op;
        if (op) {
            if (strcasecmp(op, "MACRO") == 0 || strcasecmp(op, "REPT") == 0 ||
                strcasecmp(op, "IRP")   == 0 || strcasecmp(op, "IRPC") == 0)
                nest++;
            else if (strcasecmp(op, "ENDM") == 0) {
                nest--;
                if (nest == 0) {
                    /* list closing ENDM as template (no address) */
                    if (ctx->pass == 2) {
                        lst_begin_stmt(&ctx->listing, 0, MODE_ABSOLUTE, 0,
                                       saved, sl.file, sl.line, 1, 0);
                        lst_end_stmt(&ctx->listing);
                    }
                    break;
                }
            }
        }

        /* list body lines */
        if (ctx->pass == 2) {
            Value here = segment_here(&ctx->segment);
            lst_begin_stmt(&ctx->listing, here.num, here.mode, here.common_id,
                           saved, sl.file, sl.line, 1, 0);
            lst_end_stmt(&ctx->listing);
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

/* --- Extracted handler functions --- */

#define IS(name) (strcasecmp(sl->op, name) == 0)

static int handle_conditional(AsmCtx *ctx, SrcLine *sl) {
    if (!sl->op) return 0;
    if (!(IS("IF")    || IS("IFT")    ||
          IS("IFE")   || IS("IFF")    ||
          IS("IF1")   || IS("IF2")    ||
          IS("IFDEF") || IS("IFNDEF") ||
          IS("IFB")   || IS("IFNB")   ||
          IS("IFIDN") || IS("IFDIF")  ||
          IS("ELSE")  || IS("ENDIF")  ||
          IS("COND")  || IS("ENDC"))) return 0;

    int crc = cond_process(&ctx->condctx, sl->op, sl->args ? sl->args : "");
    if (crc < 0 && ctx->pass == 2) {
        ctx->listing.stmt_error = 'C';
        ctx->errors++;
    }
    if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
    return 1;
}

static int handle_label(AsmCtx *ctx, SrcLine *sl) {
    if (!sl->label) return 0;

    Value here = segment_here(&ctx->segment);
    SymEntry *e_pre = symtab_lookup_sym(&ctx->symtab, sl->label);
    int was_defined = (e_pre && e_pre->defined && e_pre->type == SYM_LABEL);
    int old_val = was_defined ? e_pre->val.num : -1;
    SymEntry *e = symtab_define(&ctx->symtab, sl->label, SYM_LABEL, here);
    if (sl->label_public) e->is_public = 1;

    if (ctx->pass == 2) {
        if (was_defined && old_val != here.num) {
            /* M if already seen on this pass (def_count>0), P if first time */
            ctx->listing.stmt_error = (e->def_count > 0) ? 'M' : 'P';
            if (e->def_count > 0) e->multi_def = 1;
            ctx->errors++;
        }
        e->def_count++;
        ctx->listing.stmt_has_label = 1;
        lst_xref_add(&ctx->listing, e->name, sl->line, 1);
    }
    return 1;
}

static int handle_segment_dir(AsmCtx *ctx, SrcLine *sl) {
    if (IS("CSEG")) {
        segment_set_type(&ctx->segment, SEG_CODE, 0);
        if (ctx->pass == 2 && ctx->outfmt == OUT_REL)
            rel_set_loc(&ctx->rel, segment_get_loc(&ctx->segment),
                        MODE_CODE_REL);

        if (ctx->pass == 2) ctx->listing.stmt_has_label = 1;
    }
    else if (IS("DSEG")) {
        segment_set_type(&ctx->segment, SEG_DATA, 0);
        ctx->dseg_used = 1;
        if (ctx->pass == 1 && ctx->segment.code_loc == 0) ctx->dseg_before_code = 1;
        if (ctx->pass == 2 && ctx->outfmt == OUT_REL) {
            rel_set_loc(&ctx->rel, segment_get_loc(&ctx->segment), MODE_DATA_REL);
            ctx->dseg_emitting = 1;
            ctx->last_output_was_data = 1;
        }
        if (ctx->pass == 2) ctx->listing.stmt_has_label = 1;
    }
    else if (IS("ASEG")) {
        segment_set_type(&ctx->segment, SEG_ABS, 0);
        if (ctx->pass == 2 && ctx->outfmt == OUT_REL && ctx->segment.abs_loc != 0)
            rel_set_loc(&ctx->rel,
                        ctx->segment.abs_loc,
                        MODE_ABSOLUTE);
    }
    else if (IS("COMMON") && sl->args) {
        char*p = sl->args;
        while (isspace(*p)) p++;
        char cn[SYM_NAME_BUF] = "";
        if (*p == '/') {
            p++;
            int ci = 0;
            while (*p && *p != '/' && ci < SYM_NAME_BUF - 1) cn[ci++] = *p++;
            cn[ci] = '\0';
        }
        int cid = symtab_get_common(&ctx->symtab, cn);
        if (cid >= 0) {
            segment_set_type(&ctx->segment, SEG_COMMON, cid);
            if (ctx->pass == 2 && ctx->outfmt == OUT_REL) {
                rel_select_common(&ctx->rel, cn);
                rel_set_loc(&ctx->rel, segment_get_loc(&ctx->segment), MODE_COMMON);
            }
        }
    }
    else if (IS("ORG") && sl->args) {
        Value val;
        if (expr_eval(&ctx->exprctx, sl->args, &val) == EXPR_OK) {
            segment_set_loc(&ctx->segment, val.num);
            if (ctx->pass == 2 && ctx->outfmt == OUT_REL)
                rel_set_loc(&ctx->rel, val.num, segment_mode(&ctx->segment));
            if (ctx->pass == 2 && ctx->outfmt == OUT_REX)
                rex_set_loc(&ctx->rex,
                            valmode_to_rex_seg(segment_mode(&ctx->segment)),
                            val.num);
        }
    }
    else
        return 0;
    if (ctx->pass == 2) lst_end_stmt(&ctx->listing);

    return 1;
}

static int handle_data_dir(AsmCtx *ctx, SrcLine *sl) {
    /* DB / DEFB / DEFM */
    if (IS("DB") || IS("DEFB") || IS("DEFM")) {
        if (!sl->args) return 1;
        const char *p = sl->args;
        while (*p) {
            while (isspace(*p)) p++;
            if (!*p) break;

            /* Try expression first */
            const char *end;
            Value val;
            int rc = expr_eval_str(&ctx->exprctx, p, &end, &val);
            if (rc == EXPR_OK && end > p &&
                !(*p == '\'' && end == p + 2 && *(p + 1) != *p)) {
                /* Expression succeeded - check if it's a bare single char */
                int use_expr = 1;
                if (*p == '\'' || *p == '"') {
                    char q = *p;
                    const char *qe = p + 1;
                    while (*qe && *qe != q) qe++;
                    if (*qe == q && qe + 1 == end) use_expr = 0; /* just 'X', no operator */
                }
                if (use_expr) {
                    segment_advance(&ctx->segment, 1);
                    if (ctx->pass == 2 && ctx->outfmt == OUT_REL &&
                        !val_is_absolute(val) && !val.external) {
                        if (val.external) rel_ext_operand_ext(&ctx->rel, val.ext_name);
                        else              rel_ext_operand(&ctx->rel, val.num, val.mode);
                        if (val.byte_op == 2)      rel_ext_operator(&ctx->rel, 3);
                        else if (val.byte_op == 1) rel_ext_operator(&ctx->rel, 4);
                        rel_ext_operator(&ctx->rel, 1);
                        rel_abs_byte(&ctx->rel, 0x00);
                    } else {
                        emit_byte(ctx, (val.byte_op == 2) ?
                                        ((val.num >> 8) & 0xFF) :
                                        (val.num & 0xFF));
                    }
                    if (end <= p) break;
                    p = end;
                } else {
                    goto do_string;
                }
            } else if (rc == EXPR_OK) {
do_string:
                if (*p == '\'' || *p == '"') {
                    char q = *p++;
                    while (*p && *p != q) {
                        segment_advance(&ctx->segment, 1);
                        emit_byte(ctx, (unsigned char)*p);
                        p++;
                    }
                    if (*p) p++;
                }
            } else {
                /* expression failed - skip to comma */
                const char *s = p;
                int d = 0;
                while (*s) {
                    if (*s == '(') d++;
                    else if (*s == ')') d--;
                    else if (*s == ',' && d == 0) break;
                    s++;
                }
                end = s;
                segment_advance(&ctx->segment, 1);
                emit_byte(ctx, val.num & 0xFF);
                if (end <= p) break;
                p = end;
            }
            while (isspace(*p)) p++;
            if (*p == ',') p++;
        }
        return 1;
    }

    /* DW / DEFW */
    if (IS("DW") || IS("DEFW")) {
        if (!sl->args) return 1;
        const char *p = sl->args;
        while (*p) {
            while (isspace(*p)) p++;
            if (!*p) break;
            const char *end;
            Value val;
            int rc = expr_eval_str(&ctx->exprctx, p, &end, &val);
            if (rc != EXPR_OK) {
                const char *s = p;
                int d = 0;
                while (*s) {
                    if (*s == '(') d++;
                    else if (*s == ')') d--;
                    else if (*s == ',' && d == 0) break;
                    s++;
                }
                end = s;
            }
            segment_advance(&ctx->segment, 2);
            emit_word(ctx, &val);
            if (end <= p) break;
            p = end;
            while (isspace(*p)) p++;
            if (*p == ',') p++;
        }
        return 1;
    }

    /* DS / DEFS */
    if (IS("DS") || IS("DEFS")) {
        if (sl->args) {
            const char *end;
            Value val;
            int rc = expr_eval_str(&ctx->exprctx, sl->args, &end, &val);
            if (rc == EXPR_OK) {
                if (!val_is_absolute(val) && ctx->pass == 2) {
                    ctx->listing.stmt_error = 'R';
                    ctx->errors++;
                }
                int count = val.num;
                while (isspace(*end)) end++;
                int has_fill = 0, fill = 0;
                if (*end == ',') {
                    end++;
                    Value fv;
                    if (expr_eval_str(&ctx->exprctx, end, &end, &fv) == EXPR_OK) {
                        has_fill = 1;
                        fill = fv.num & 0xFF;
                    }
                }
                if (has_fill && segment_mode(&ctx->segment) == MODE_DATA_REL)
                    ctx->dseg_has_bytes = 1;
                if (has_fill && ctx->pass == 2) {
                    for (int i = 0; i < count; i++)
                        emit_byte(ctx, (unsigned char)fill);
                }
                segment_advance(&ctx->segment, count);
                if (!has_fill && ctx->pass == 2 && ctx->outfmt == OUT_REL &&
                    segment_mode(&ctx->segment) != MODE_CODE_REL)
                    rel_set_loc(&ctx->rel, segment_get_loc(&ctx->segment),
                        segment_mode(&ctx->segment));
            }
        }
        return 1;
    }

    /* DC (define character - last byte has bit 7 set) */
    if (IS("DC")) {
        if (sl->args) {
            const char *p = sl->args;
            while (isspace(*p)) p++;
            if (*p == '\'' || *p == '"') {
                char q = *p++;
                int len = 0;
                const char *s = p;
                while (*p && *p != q) {
                    len++;
                    p++;
                }
                segment_advance(&ctx->segment, len);
                if (ctx->pass == 2) {
                    for (int i = 0; i < len - 1; i++)
                        emit_byte(ctx, (unsigned char)s[i]);
                    if (len > 0)
                        emit_byte(ctx, (unsigned char)s[len - 1] | 0x80);
                }
            }
        }
        return 1;
    }

    return 0;
}

static int handle_symbol_dir(AsmCtx *ctx, SrcLine *sl) {
    if (IS("PUBLIC") || IS("ENTRY") || IS("GLOBAL")) {
        if (sl->args) {
            char *p = sl->args;
            while (*p) {
                while (isspace(*p) || *p == ',') p++;
                if (!*p) break;
                char nm[SYM_NAME_BUF];
                int ni = 0;
                while (*p && *p != ',' && !isspace(*p) && ni < SYM_NAME_BUF - 1)
                    nm[ni++] = toupper((unsigned char)*p++);
                nm[ni] = '\0';
                SymEntry *e = symtab_lookup_sym(&ctx->symtab, nm);
                if (!e) {
                    e = symtab_define(&ctx->symtab, nm, SYM_LABEL, val_absolute(0));
                    e->defined = 0;
                }
                if (e) e->is_public = 1;
                if (ctx->outfmt == OUT_REL && strlen(nm) > 6 && ctx->pass == 2) {
                    fprintf(stderr, "%s:%d: public symbol '%s' truncated to 6 chars in REL output\n",
                            sl->file, sl->line, nm);
                    ctx->warnings++;
                }
            }
        }
    }
    else if (IS("EXT") || IS("EXTRN") || IS("EXTERNAL")) {
        if (sl->args) {
            char *p = sl->args;
            while (*p) {
                while (isspace(*p) || *p == ',') p++;
                if (!*p) break;
                char nm[SYM_NAME_BUF];
                int ni = 0;
                while (*p && *p != ',' && !isspace(*p) && ni < SYM_NAME_BUF - 1)
                    nm[ni++] = toupper((unsigned char)*p++);
                nm[ni] = '\0';
                if (ctx->outfmt == OUT_REL && ni > 6 && ctx->pass == 2) {
                    fprintf(stderr, "%s:%d: external symbol '%s' truncated to 6 chars in REL output\n", sl->file, sl->line, nm);
                    ctx->warnings++;
                }
                symtab_define(&ctx->symtab, nm, SYM_EXTERNAL, val_external(nm));
            }
        }
    }
    else if (IS("BYTE")) {
        /* BYTE EXTRN name — byte-sized external declaration */
        if (sl->args) {
            const char *a = sl->args;
            while (isspace(*a)) a++;
            if (strncasecmp(a, "EXT", 3) == 0) {
                while (*a && !isspace(*a)) a++;
                while (isspace(*a)) a++;
            }
            while (*a) {
                while (isspace(*a) || *a == ',') a++;
                if (!*a) break;
                char nm[SYM_NAME_BUF];
                int ni = 0;
                while (*a && *a != ',' && !isspace(*a) && ni < SYM_NAME_BUF - 1)
                    nm[ni++] = toupper((unsigned char)*a++);
                nm[ni] = '\0';
                if (ctx->outfmt == OUT_REL && ni > 6 && ctx->pass == 2) {
                    fprintf(stderr, "%s:%d: external symbol '%s' truncated to 6 chars in REL output\n", sl->file, sl->line, nm);
                    ctx->warnings++;
                }
                symtab_define(&ctx->symtab, nm, SYM_EXTERNAL, val_external(nm));
            }
        }
    }
    else
        return 0;

    if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
    return 1;
}

static int handle_listing_dir(AsmCtx *ctx, SrcLine *sl) {
    if (IS(".LIST")) {
        if (ctx->pass == 2) lst_enable(&ctx->listing);
    }
    else if (IS(".XLIST")) {
        if (ctx->pass == 2) lst_disable(&ctx->listing);
    }
    else if (IS(".LALL")) {
        ctx->listing.macro_list_mode = 1; /* list all macro lines */
    }
    else if (IS(".SALL")) {
        ctx->listing.macro_list_mode = 2; /* suppress all macro lines */
    }
    else if (IS(".XALL")) {
        ctx->listing.macro_list_mode = 0; /* list only lines that generate code (default) */
    }
    else if (IS(".SFCOND")) {
        ctx->listing.list_false_cond = 0; /* suppress false conditionals */
    }
    else if (IS(".LFCOND")) {
        ctx->listing.list_false_cond = 1; /* list false conditionals */
    }
    else if (IS(".TFCOND")) {
        ctx->listing.list_false_cond ^= 1; /* toggle */
    }
    else if (IS(".CREF") || IS(".XCREF")) {
        /* cross-reference control — no-op (no CREF output supported) */
    }
    else if (IS("PAGE") || IS("$EJECT") || IS("*EJECT")) {
        /* show the PAGE line on current page, then break */
        if (ctx->pass == 2) {
            lst_end_stmt(&ctx->listing);
            lst_new_page(&ctx->listing);
        }
        return 1;
    }
    else if (IS("SUBTTL")) {
        if (sl->args) lst_set_subtitle(&ctx->listing, sl->args);
    }
    else if (IS("TITLE") || IS("$TITLE")) {
        if (sl->args) {
            char*p = sl->args;
            while (isspace(*p)) p++;
            char t[64];
            int ti = 0;
            if (*p == '\'') p++;

            while (*p && *p != '\'' && ti < 63) t[ti++] = *p++;
            t[ti] = '\0';
            lst_set_title(&ctx->listing, t);
            int mi = 0;
            for (int i = 0; t[i] && t[i] != ' ' && t[i] != '\t' && mi < 6; i++)
                ctx->modname[mi++] = toupper((unsigned char)t[i]);

            ctx->modname[mi] = '\0';
        }
    }
    else if (IS("NAME")) {
        if (sl->args) {
            const char *p = sl->args;
            while (isspace(*p)) p++;
            char nm[7];
            int ni = 0;
            if (*p == '(') {
                p++;
                while (isspace(*p)) p++;
                if (*p == '\'') p++;
                while (*p && *p != '\'' && *p != ')' && ni < 6)
                    nm[ni++] = toupper((unsigned char)*p++);
            }
            else if (*p == '\'') {
                p++;
                while (*p && *p != '\'' && ni < 6)
                    nm[ni++] = toupper((unsigned char)*p++);
            }
            else
                while (*p && !isspace(*p) && ni < 6)
                    nm[ni++] = toupper((unsigned char)*p++);
            nm[ni] = '\0';
            strncpy(ctx->modname, nm, sizeof(ctx->modname) - 1);
        }
    }
    else if (IS(".PRINTX")) {
        /* Print message to stderr on both passes (M80 prints on pass 1 only) */
        if (ctx->pass == 1 && sl->args) {
            const char *p = sl->args;
            while (isspace(*p)) p++;
            if (*p && (*p == '\'' || *p == '"' || *p == '/' || *p == '<')) {
                char delim = *p;
                if (delim == '<') delim = '>';
                p++;
                const char*e = p;
                while (*e && *e != delim) e++;
                fprintf(stderr, "%.*s\n", (int)(e - p), p);
            }
            else if (*p) fprintf(stderr, "%s\n", p);
        }
    }
    else if (IS(".RADIX") && sl->args) {
        int r = (int)strtol(sl->args, NULL, 10);
        if (r >= 2 && r <= 16)
            ctx->exprctx.radix = r;
        else if (ctx->pass == 2) {
            ctx->listing.stmt_error = 'A';
            ctx->errors++;
        }
    }
    else
        return 0;
    if (ctx->pass == 2) lst_end_stmt(&ctx->listing);

    return 1;
}

static int handle_misc_dir(AsmCtx *ctx, SrcLine *sl) {
    if (IS(".Z80"))
        ctx->cpu_mode = CPU_Z80;
    else if (IS(".8080"))
        ctx->cpu_mode = CPU_8080;
    else if (IS(".8085"))
        ctx->cpu_mode = CPU_8085;
    else if (IS(".Z80UNDOC"))
        ctx->cpu_mode = CPU_Z80U;
    else if (IS(".Z180"))
        ctx->cpu_mode = CPU_Z180;
    else if (IS(".R800"))
        ctx->cpu_mode = CPU_R800;
    else if (IS(".ZXNEXT"))
        ctx->cpu_mode = CPU_ZXNEXT;
    else if (IS(".REQUEST")) {
        if (ctx->pass == 2 && sl->args && ctx->outfmt == OUT_REL) {
            const char *p = sl->args;
            while (*p) {
                while (isspace(*p) || *p == ',') p++;
                if (!*p) break;

                char name[17];
                int ni = 0;
                while (*p && *p != ',' && !isspace(*p) && ni < 16)
                    name[ni++] = toupper((unsigned char)*p++);
                name[ni] = '\0';
                rel_request_library(&ctx->rel, name);
            }
        }
    }
    else if (IS(".PHASE") && sl->args) {
        Value val;
        if (expr_eval(&ctx->exprctx, sl->args, &val) == EXPR_OK)
            segment_phase(&ctx->segment, val.num);
    }
    else if (IS(".DEPHASE"))
        segment_dephase(&ctx->segment);
    else if (IS(".SYMLEN") && sl->args) {
        Value val;
        if (expr_eval(&ctx->exprctx, sl->args, &val) == EXPR_OK)
            symtab_set_siglen(&ctx->symtab, val.num);
    }
    else if (IS("INCLUDE") || IS("$INCLUDE") || IS("MACLIB")) {
        char *p = sl->args;
        if (!p) {
            if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
            return 1;
        }
        while (isspace(*p)) p++;

        int flen = (int)strlen(p);
        while (flen > 0 && (isspace(p[flen - 1]))) flen--;
        p[flen] = '\0';
        if (flen >= 2 && ((p[0] == '\'' && p[flen - 1] == '\'') || (p[0] == '"' && p[flen - 1] == '"'))) {
            p[flen - 1] = '\0';
            p++;
        }
        if (IS("MACLIB") && !strchr(p, '.')) {
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "%s.LIB", p);
            ls_include(&ctx->ls, tmp);
        }
        else {
            int rc = ls_include(&ctx->ls, p);
            if (rc < 0 && ctx->pass == 2)
                fprintf(stderr, "%s:%d: Cannot include '%s'\n", sl->file, sl->line, p);
        }
    }
    else if (IS("EXITM")) {
        /* Flush remaining macro expansion lines from stack */
        int nest = 1;
        while (ctx->ls.lstack_top > 0 && nest > 0) {
            char *line = ctx->ls.lstack[--ctx->ls.lstack_top];
            const char *p = line;
            while (isspace(*p)) p++;
            if (strncasecmp(p, "ENDM", 4) == 0 && (p[4] == 0 || p[4] == ' ' || p[4] == '\t' || p[4] == ';'))
                nest--;
            else if (strncasecmp(p, "MACRO", 5) == 0 || (strncasecmp(p, "IRP", 3) == 0) || (strncasecmp(p, "REPT", 4) == 0))
                nest++;

            free(line);
        }
    }
    else if (IS("END")) {
        if (sl->args && *sl->args) {
            Value val;
            if (expr_eval(&ctx->exprctx, sl->args, &val) == EXPR_OK) {
                ctx->has_start = 1;
                ctx->start_addr = val;
            }
        }
        if (ctx->pass == 2 && ctx->outfmt == OUT_REL) {
            /* emit entry points and externals in M80 hash bucket order */
            SymEntry *all[2048];
            int na = 0;
            for (int i = 0; i < SYMTAB_SIZE; i++)
                for (SymEntry *e = ctx->symtab.buckets[i]; e; e = e->next) {
                    if (e->type == SYM_EXTERNAL)
                        all[na++] = e;
                    else if (e->is_public && e->defined && na < 2048)
                        all[na++] = e;
                }
            /* Sort by first-char bucket then alphabetically */
            for (int i = 0; i < na - 1; i++) {
                for (int j = i + 1; j < na; j++) {
                    unsigned char a0 = all[i]->name[0], b0 = all[j]->name[0];
                    int ci = a0 == '$' ? 0 :
                             a0 == '.' ? 1 :
                             a0 == '?' ? 2 :
                             a0 == '_' ? 3 :
                             a0 == '@' ? 5 :
                             a0 - ';';
                    int cj = b0 == '$' ? 0 :
                             b0 == '.' ? 1 :
                             b0 == '?' ? 2 :
                             b0 == '_' ? 3 :
                             b0 == '@' ? 5 :
                             b0 - ';';
                    int cmp = (ci != cj) ? (ci - cj) : strcmp(all[i]->name, all[j]->name);
                    if (cmp > 0) {
                        SymEntry *t = all[i];
                        all[i] = all[j];
                        all[j] = t;
                    }
                }
            }
            for (int i = 0; i < na; i++) {
                if (all[i]->type == SYM_EXTERNAL)
                    rel_chain_external(&ctx->rel, all[i]->name, all[i]->chain_head,
                        all[i]->ref_count > 0 ? (ValMode)all[i]->chain_mode : MODE_ABSOLUTE);
                else
                    rel_define_entry(&ctx->rel, all[i]->name, all[i]->val.num, all[i]->val.mode);
            }
            rel_end_program(&ctx->rel, ctx->has_start,
                ctx->has_start ? ctx->start_addr.num : 0,
                ctx->has_start ? ctx->start_addr.mode : MODE_ABSOLUTE);
            rel_end_file(&ctx->rel);
        }
        if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
        return -1; /* special: caller should break */
    }
    else if (IS("REPT") && sl->args) {
        Value val;
        expr_eval(&ctx->exprctx, sl->args, &val);
        char body[MACRO_BODY_MAX];
        collect_body_listed(ctx, body, sizeof(body));
        macro_rept(&ctx->macroctx, val.num, body);
        return 1;
    }
    else if (IS("IRP") && sl->args) {
        if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
        char*p = sl->args;
        while (isspace(*p)) p++;

        char dummy[32];
        int di = 0;
        while (*p && *p != ',' && !isspace(*p) && di < 31) dummy[di++] = *p++;
        dummy[di] = '\0';
        if (*p == ',') p++;

        while (isspace(*p)) p++;
        char*al = p;
        if (*al == '<') {
            al++;
            char*e = al + strlen(al) - 1;
            while (e > al && *e != '>') e--;
            if (*e == '>') *e = '\0';
        }
        char body[MACRO_BODY_MAX];
        collect_body_listed(ctx, body, sizeof(body));
        macro_irp(&ctx->macroctx, dummy, al, body);
        return 1;
    }
    else if (IS("IRPC") && sl->args) {
        if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
        char*p = sl->args;
        while (isspace(*p)) p++;

        char dummy[32];
        int di = 0;
        while (*p && *p != ',' && !isspace(*p) && di < 31) dummy[di++] = *p++;
        dummy[di] = '\0';
        if (*p == ',') p++;

        while (isspace(*p)) p++;

        char*s = p;
        if (*s == '<') {
            s++;
            char*e = s + strlen(s) - 1;
            while (e > s && *e != '>') e--;
            if (*e == '>') *e = '\0';
        }
        char body[MACRO_BODY_MAX];
        collect_body_listed(ctx, body, sizeof(body));
        macro_irpc(&ctx->macroctx, dummy, s, body);
        return 1;
    }
    else
        return 0;
    if (ctx->pass == 2) lst_end_stmt(&ctx->listing);

    return 1;
}

/* --- Main assembler pass --- */

int asm_pass(AsmCtx *ctx) {
    SrcLine sl;

    while (ls_getline(&ctx->ls, &sl) == 0) {
        char raw_save[LINE_MAX];
        snprintf(raw_save, sizeof(raw_save), "%s", sl.raw);
        line_split(&sl);

        /* listing */
        if (ctx->pass == 2) {
            Value here = segment_here(&ctx->segment);
            lst_begin_stmt(&ctx->listing, here.num, here.mode, here.common_id,
                           raw_save, sl.file, sl.line,
                           sl.origin == ORIG_INCLUDE, sl.origin == ORIG_MACRO);
        }
        ctx->exprctx.src_line = sl.line;

        /* handle conditionals */
        if (handle_conditional(ctx, &sl)) continue;

        if (!cond_active(&ctx->condctx)) {
            if (ctx->pass == 2) {
                if (!ctx->listing.list_false_cond) ctx->listing.stmt_suppressed = 1;
                lst_end_stmt(&ctx->listing);
            }
            continue;
        }
        /* define label */
        if (sl.label) {
            /* check if "label" is name and "op" is EQU/SET/DEFL/MACRO */
            if (sl.op && strcasecmp(sl.op, "MACRO") == 0) {
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                char body[MACRO_BODY_MAX];
                collect_body_listed(ctx, body, sizeof(body));
                MacroDef def;
                memset(&def, 0, sizeof(def));
                def.body_len = (int)strlen(body);
                memcpy(def.body, body, def.body_len + 1);
                if (sl.args) {
                    const char *p = sl.args;
                    int pc = 0;
                    while (*p && pc < 32) {
                        while (isspace(*p) || *p == ',') p++;
                        if (!*p) break;
                        int ni = 0;
                        while (*p && *p != ',' && !isspace(*p) && ni < SYM_NAME_BUF - 1)
                            def.params[pc][ni++] = toupper((unsigned char)*p++);
                        def.params[pc][ni] = 0;
                        pc++;
                    }
                    def.param_count = pc;
                }
                symtab_define_macro(&ctx->symtab, sl.label, &def);
                continue;
            }
            if (sl.op && strcasecmp(sl.op, "EQU") == 0) {
                Value val;
                ExprError err = expr_eval(&ctx->exprctx, sl.args ? sl.args : "0", &val);
                if (err == EXPR_OK) {
                    symtab_define(&ctx->symtab, sl.label, SYM_EQU, val);
                    if (ctx->pass == 2) {
                        ctx->listing.stmt_addr = val.num;
                        ctx->listing.stmt_mode = val.mode;
                        ctx->listing.stmt_has_value = 1;
                    }
                }
                else if (ctx->pass == 2) {
                    fprintf(stderr, "%s:%d: EQU error\n", sl.file, sl.line);
                    ctx->errors++;
                }
                if (ctx->pass == 2)
                    lst_end_stmt(&ctx->listing);

                continue;
            }
            if (sl.op && (strcasecmp(sl.op, "SET") == 0 ||
                         strcasecmp(sl.op, "DEFL") == 0 ||
                         strcasecmp(sl.op, "ASET") == 0)) {
                Value val;
                expr_eval(&ctx->exprctx, sl.args ? sl.args : "0", &val);
                if (val.external && ctx->pass == 2) {
                    ctx->listing.stmt_error = 'E';
                    ctx->errors++;
                }
                symtab_define(&ctx->symtab, sl.label, SYM_SET, val);
                if (ctx->pass == 2)
                    lst_end_stmt(&ctx->listing);

                continue;
            }
            /* check if "label" is actually a macro call (only no-colon labels) */ if (!sl.label_colon) {
                SymEntry *me = symtab_lookup(&ctx->symtab, sl.label);
                if (me && me->type != SYM_MACRO)
                    for (SymEntry *s = me->next; s; s = s->next)
                        if (strcmp(s->name, me->name) == 0 && s->type == SYM_MACRO) {
                            me = s;
                            break;
                        }
                if (me && me->type == SYM_MACRO) {
                    char fullargs[LINE_MAX] = "";
                    if (sl.op)
                        snprintf(fullargs, sizeof(fullargs), "%s%s%s",
                                 sl.op, sl.args ? "," : "", sl.args ? sl.args : "");

                    macro_expand(&ctx->macroctx, sl.label, fullargs[0] ? fullargs : NULL);
                    if (ctx->pass == 2)
                        lst_end_stmt(&ctx->listing);

                    continue;
                }
            }
            handle_label(ctx, &sl);
        }
        if (!sl.op) {
            if (ctx->pass == 2)
                lst_end_stmt(&ctx->listing);
            continue;
        }
        /* .COMMENT block */
        if (strcasecmp(sl.op, ".COMMENT") == 0) {
            const char *p = sl.args;
            if (!p) p = "";
            while (isspace(*p)) p++;

            int delim = *p ? *p : '%';
            /* skip lines until delimiter found */
            SrcLine cl;
            while (ls_getline(&ctx->ls, &cl) == 0)
                if (strchr(cl.raw, delim)) break;                        // END breaks here
            if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
            continue;
        }
        /* MACRO definition */
        if (strcasecmp(sl.op, "MACRO") == 0 && sl.label) {
            if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
            char body[MACRO_BODY_MAX];
            collect_body_listed(ctx, body, sizeof(body));
            MacroDef def;
            memset(&def, 0, sizeof(def));
            def.body_len = (int)strlen(body);
            memcpy(def.body, body, def.body_len + 1);
            if (sl.args) {
                const char *p = sl.args;
                int pc = 0;
                while (*p && pc < 32) {
                    while (isspace(*p) || *p == ',') p++;
                    if (!*p) break;
                    int ni = 0;
                    while (*p && *p != ',' && !isspace(*p) && ni < SYM_NAME_BUF - 1)
                        def.params[pc][ni++] = toupper((unsigned char)*p++);
                    def.params[pc][ni] = 0;
                    pc++;
                }
                def.param_count = pc;
            }
            symtab_define_macro(&ctx->symtab, sl.label, &def);
            continue;
        }
        /* Check: is op a macro name? (macros override instructions in M80) */
        if (!sl.label_colon) {
            SymEntry *me = symtab_lookup(&ctx->symtab, sl.op);
            if (me && me->type != SYM_MACRO)
                for (SymEntry *s = me->next; s; s = s->next)
                    if (strcmp(s->name, me->name) == 0 && s->type == SYM_MACRO) {
                        me = s;
                        break;
                    }
            if (me && me->type == SYM_MACRO) {
                macro_expand(&ctx->macroctx, sl.op, sl.args);
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                continue;
            }
        }
        if (!is_opcode(sl.op, ctx->cpu_mode)) {
            SymEntry *e = symtab_lookup(&ctx->symtab, sl.op);
            /* if first hit isn't macro, check for macro in chain (separate namespace) */
            if (e && e->type != SYM_MACRO)
                for (SymEntry *s = e->next; s; s = s->next)
                    if (strcmp(s->name, e->name) == 0 && s->type == SYM_MACRO) {
                        e = s;
                        break;
                    }
            if (e && e->type == SYM_MACRO) {
                macro_expand(&ctx->macroctx, sl.op, sl.args);
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                continue;
            }
            /* name MACRO params */
            if (strcasecmp(sl.op, "MACRO") == 0) {
                /* label already parsed — sl.label is the macro name */
                /* but if there's no label, this is an error */
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                continue;
            }
            /* EQU / SET / DEFL */
            if (sl.args) {
                /* check if args starts with EQU/SET/DEFL */
                const char *a = sl.args;
                while (isspace(*a)) a++;

                /* This pattern: "name EQU expr" was split as label=NULL, op=name, args="EQU expr" */
                /* OR: label=name (no colon), op=EQU, args=expr */
            }
            /* Check: is op EQU/SET/DEFL? */
            if (strcasecmp(sl.op, "EQU") == 0 && sl.label) {
                Value val;
                ExprError err = expr_eval(&ctx->exprctx, sl.args ? sl.args : "0", &val);
                if (err == EXPR_OK) {
                    symtab_define(&ctx->symtab, sl.label, SYM_EQU, val);
                    if (ctx->pass == 2) {
                        ctx->listing.stmt_addr = val.num;
                        ctx->listing.stmt_mode = val.mode;
                        ctx->listing.stmt_has_value = 1;
                    }
                }
                else if (ctx->pass == 2) {
                    fprintf(stderr, "%s:%d: EQU error\n", sl.file, sl.line);
                    ctx->errors++;
                }
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                continue;
            }
            if ((strcasecmp(sl.op, "SET") == 0 || strcasecmp(sl.op, "DEFL") == 0 || strcasecmp(sl.op, "ASET") == 0) && sl.label) {
                Value val;
                expr_eval(&ctx->exprctx, sl.args ? sl.args : "0", &val);
                symtab_define(&ctx->symtab, sl.label, SYM_SET, val);
                if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
                continue;
            }
            /* unknown symbol as operator — error */
            if (ctx->pass == 2 && !strcasecmp(sl.op, "INCLUDE") == 0) {
                /* don't error on pseudo-ops we handle below */
            }
        }
        /* Z80 SET opcode: in Z80 mode, SET without label is the bit-set opcode */
        if ((ctx->cpu_mode & INST_SET_Z80) && strcasecmp(sl.op, "SET") == 0 && !sl.label) {
            do_opcode(ctx, "SET", sl.args, &sl);
            if (ctx->pass == 2) lst_end_stmt(&ctx->listing);
            continue;
        }
        /* directive handlers */
        if (handle_segment_dir(ctx, &sl)) continue;
        if (handle_data_dir(ctx, &sl))    continue;
        if (handle_symbol_dir(ctx, &sl))  continue;
        if (handle_listing_dir(ctx, &sl)) continue;

        int rc = handle_misc_dir(ctx, &sl);
        if (rc == -1) break;    /* END directive */
        if (rc == 1)  continue;

        if (is_opcode(sl.op, ctx->cpu_mode))
            do_opcode(ctx, sl.op, sl.args, &sl);
        else if (sl.op[0] == '"' || sl.op[0] == '\'') {
            /* implicit DB — line starts with quoted string */
            if (ctx->pass == 2) {
                fprintf(stderr, "%s:%d: Unrecognized: %s\n", sl.file, sl.line, sl.op);
                ctx->errors++;
                ctx->listing.stmt_error = 'O';
            }
        }
        else if (ctx->pass == 2) {
            /* unrecognized */
            fprintf(stderr, "%s:%d: Unrecognized: %s\n", sl.file, sl.line, sl.op);
            ctx->errors++;
            ctx->listing.stmt_error = 'O';
        }

        if (ctx->pass == 2)
            lst_end_stmt(&ctx->listing);
    }
    return ctx->errors;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
