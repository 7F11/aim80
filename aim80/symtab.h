#ifndef SYMTAB_H
#define SYMTAB_H
#define SYMTAB_SIZE     1024
#define SYM_NAME_BUF    33      /* max buffer: 32 chars + null */
#define SYM_NAME_DEFAULT 16     /* M80 uses up to 16 significant chars */
#define SYM_NAME_MAXLEN  32     /* max configurable length */
#define MACRO_BODY_MAX  8192
#define COMMON_MAX      64      /* max number of COMMON blocks */

/* value modes — matches M80 TYPE operator encoding */
typedef enum {
    MODE_ABSOLUTE   = 0,
    MODE_CODE_REL   = 1,
    MODE_DATA_REL   = 2,
    MODE_COMMON     = 3,        /* + common_id to distinguish blocks */
    MODE_EXTERNAL   = 5         /* for listing: external symbol marker */
} ValMode;

/* a typed value: number + relocation mode */
typedef struct {
    int num;
    ValMode mode;
    int common_id;          /* which COMMON block, if mode == MODE_COMMON */
    int external;           /* references an external symbol */
    char ext_name[SYM_NAME_BUF];    /* name of external, if external */
    int byte_op;            /* 0=none, 1=LOW, 2=HIGH */
    int complex_reloc;      /* expression involves ext-reloc or other complex combo */
} Value;

typedef enum {
    SYM_UNDEF,
    SYM_LABEL,
    SYM_EQU,
    SYM_SET,
    SYM_MACRO,
    SYM_EXTERNAL,
    SYM_PUBLIC,
    SYM_COMMON          /* COMMON block name */
} SymType;

typedef struct {
    char params[32][SYM_NAME_BUF];
    int param_count;
    char body[MACRO_BODY_MAX];
    int body_len;
} MacroDef;

typedef struct SymEntry {
    char name[SYM_NAME_BUF];
    SymType type;
    Value val;
    int defined;            /* known on current pass */
    int pass_defined;       /* which pass it was defined on */
    int is_public;
    int def_count;          /* number of label definitions (for M vs P error) */
    int multi_def;          /* symbol is multiply defined (for D error on reference) */
    int chain_head;         /* external ref chain: address of last reference (0=end) */
    int chain_mode;         /* mode of the chain addresses */
    int ref_count;          /* number of external references */
    int order;              /* declaration order (for deterministic output) */
    MacroDef *macro;
    struct SymEntry *next;
} SymEntry;

/* COMMON block info */
typedef struct {
    char name[SYM_NAME_BUF];    /* empty string = blank common */
    int size;                   /* size in bytes */
} CommonBlock;

typedef struct {
    SymEntry    *buckets[SYMTAB_SIZE];
    int local_counter;              /* for LOCAL ..0001-..FFFF */
    int sig_len;                    /* significant chars in symbols (default 6) */
    CommonBlock commons[COMMON_MAX];
    int common_count;
    int next_order;             /* next declaration order number */
} SymTab;

void      symtab_init(SymTab *st);
void      symtab_free(SymTab *st);
SymEntry *symtab_lookup(SymTab *st, const char *name);
SymEntry *symtab_lookup_sym(SymTab *st, const char *name);
SymEntry *symtab_define(SymTab *st, const char *name, SymType type, Value val);
SymEntry *symtab_define_macro(SymTab *st, const char *name, MacroDef *def);
int       symtab_next_local(SymTab *st, char *buf, int bufsize);
int       symtab_get_common(SymTab *st, const char *name);
void      symtab_set_siglen(SymTab *st, int len); /* returns common_id, creates if new */

/* value helpers */
Value val_absolute(int num);
Value val_relative(int num, ValMode mode);
Value val_common(int num, int common_id);
Value val_external(const char *name);
int   val_is_absolute(Value v);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
