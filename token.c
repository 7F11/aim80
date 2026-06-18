#include "token.h"
static const char *type_names[] = {
    [TOK_LINE]       = "LINE",
    [TOK_EOL]        = "EOL",
    [TOK_EOF]        = "EOF",
    [TOK_LABEL]      = "LABEL",
    [TOK_SYMBOL]     = "SYMBOL",
    [TOK_NUMBER]     = "NUMBER",
    [TOK_STRING]     = "STRING",
    [TOK_REG]        = "REG",
    [TOK_PLUS]       = "PLUS",
    [TOK_MINUS]      = "MINUS",
    [TOK_STAR]       = "STAR",
    [TOK_SLASH]      = "SLASH",
    [TOK_LPAREN]     = "LPAREN",
    [TOK_RPAREN]     = "RPAREN",
    [TOK_COMMA]      = "COMMA",
    [TOK_COLON]      = "COLON",
    [TOK_LANGLE]     = "LANGLE",
    [TOK_RANGLE]     = "RANGLE",
    [TOK_HASH2]      = "HASH2",
    [TOK_DOLLAR]     = "DOLLAR",
    [TOK_AMPERSAND]  = "AMPERSAND",
    [TOK_PERCENT]    = "PERCENT",
    [TOK_BANG]       = "BANG",
    [TOK_OP_MOD]     = "MOD",
    [TOK_OP_SHR]     = "SHR",
    [TOK_OP_SHL]     = "SHL",
    [TOK_OP_NOT]     = "NOT",
    [TOK_OP_AND]     = "AND",
    [TOK_OP_OR]      = "OR",
    [TOK_OP_XOR]     = "XOR",
    [TOK_OP_EQ]      = "EQ",
    [TOK_OP_NE]      = "NE",
    [TOK_OP_LT]      = "LT",
    [TOK_OP_LE]      = "LE",
    [TOK_OP_GT]      = "GT",
    [TOK_OP_GE]      = "GE",
    [TOK_OP_HIGH]    = "HIGH",
    [TOK_OP_LOW]     = "LOW",
    [TOK_OP_NUL]     = "NUL",
    [TOK_OP_TYPE]    = "TYPE",
    [TOK_OPCODE]     = "OPCODE",
    [TOK_CONDITION]  = "CONDITION",
    [TOK_PSEUDO]     = "PSEUDO",
    [TOK_COND]       = "COND",
    [TOK_MACRO]      = "MACRO",
    [TOK_ENDM]       = "ENDM",
    [TOK_EXITM]      = "EXITM",
    [TOK_LOCAL]      = "LOCAL",
    [TOK_REPT]       = "REPT",
    [TOK_IRP]        = "IRP",
    [TOK_IRPC]       = "IRPC",
    [TOK_COMMENT]    = "COMMENT",
};

const char *token_type_name(TokenType t) {
    if (t >= 0 && t < TOK_COUNT) return type_names[t];
    return "UNKNOWN";
}

const char *origin_name(Origin o) {
    switch (o) {
    case ORIG_SOURCE:  return "source";
    case ORIG_INCLUDE: return "include";
    case ORIG_MACRO:   return "macro";
    }
    return "unknown";
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
