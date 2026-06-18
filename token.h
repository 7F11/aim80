#ifndef TOKEN_H
#define TOKEN_H
typedef enum {
    /* structural */
    TOK_LINE,       /* start of a new source line */
    TOK_EOL,        /* end of line */
    TOK_EOF,        /* end of file */

    /* identifiers and values */
    TOK_LABEL,      /* symbol followed by : or :: */
    TOK_SYMBOL,     /* identifier / symbol reference */
    TOK_NUMBER,     /* numeric constant */
    TOK_STRING,     /* quoted string */

    /* registers (8080) */
    TOK_REG,        /* A B C D E H L M SP PSW */

    /* operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_COLON,
    TOK_LANGLE,     /* < */
    TOK_RANGLE,     /* > */
    TOK_HASH2,      /* ## external marker */
    TOK_DOLLAR,     /* $ location counter */
    TOK_AMPERSAND,  /* & macro concat */
    TOK_PERCENT,    /* % macro eval */
    TOK_BANG,       /* ! literal escape */

    /* keyword operators */
    TOK_OP_MOD,
    TOK_OP_SHR,
    TOK_OP_SHL,
    TOK_OP_NOT,
    TOK_OP_AND,
    TOK_OP_OR,
    TOK_OP_XOR,
    TOK_OP_EQ,
    TOK_OP_NE,
    TOK_OP_LT,
    TOK_OP_LE,
    TOK_OP_GT,
    TOK_OP_GE,
    TOK_OP_HIGH,
    TOK_OP_LOW,
    TOK_OP_NUL,
    TOK_OP_TYPE,

    /* 8080 opcodes */
    TOK_OPCODE,

    /* Z80 condition codes */
    TOK_CONDITION,

    /* pseudo-ops */
    TOK_PSEUDO,

    /* conditional pseudo-ops */
    TOK_COND,

    /* macro-related */
    TOK_MACRO,      /* MACRO keyword */
    TOK_ENDM,
    TOK_EXITM,
    TOK_LOCAL,
    TOK_REPT,
    TOK_IRP,
    TOK_IRPC,

    /* comment */
    TOK_COMMENT,

    TOK_COUNT       /* number of token types */
} TokenType;

typedef enum {
    ORIG_SOURCE,
    ORIG_INCLUDE,
    ORIG_MACRO
} Origin;

typedef struct {
    TokenType type;
    char value[256];
    char file[256];
    int line;
    int col;
    char source[512];   /* full original source line (LINE token only) */
    Origin origin;
    int suppressed;     /* inside false conditional */
    int is_public;      /* label declared with :: */
} Token;

const char *token_type_name(TokenType t);
const char *origin_name(Origin o);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
