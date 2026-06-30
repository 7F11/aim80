#ifndef LINES_H
#define LINES_H
#include <stdio.h>
#include "token.h"

#define LINE_MAX    512
#define LINE_STACK  4096        /* max pending lines from macro expansion */
#define FILE_STACK  8           /* include nesting */

/* A parsed source line */
typedef struct {
    char raw[LINE_MAX];         /* original text */
    char *label;                /* pointer into raw, or NULL */
    int label_public;           /* :: detected */
    int label_colon;            /* label had : (not a macro call) */
    char *op;                   /* operator (opcode/pseudo/macro name) */
    char *args;                 /* operand field */
    char *comment;              /* comment (after ;) */
    char file[256];
    int line;
    Origin origin;
} SrcLine;

/* File stack entry */
typedef struct {
    FILE *fp;
    char filename[256];
    int line;
} FileEntry;

/* Line source: reads from files and macro expansion stack */
typedef struct {
    FileEntry files[FILE_STACK];
    int file_depth;

    /* macro expansion line stack */
    char *lstack[LINE_STACK];
    int lstack_top;             /* next free slot */
    Origin lstack_origin;

    /* current file's include directory */
    char basedir[256];
} LineSource;

void   ls_init(LineSource *ls);
int    ls_open(LineSource *ls, const char *filename);
void   ls_close(LineSource *ls);
int    ls_getline(LineSource *ls, SrcLine *sl);
void   ls_push_lines(LineSource *ls, const char *text, Origin origin);
int    ls_include(LineSource *ls, const char *filename);

/* Split a raw line into fields */
void   line_split(SrcLine *sl);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
