#include <ctype.h>
#define _GNU_SOURCE
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <libgen.h>
#include "lines.h"

void ls_init(LineSource *ls) {
    memset(ls, 0, sizeof(*ls));
}

int ls_open(LineSource *ls, const char *filename) {
    if (ls->file_depth >= FILE_STACK) return -1;

    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    FileEntry *fe = &ls->files[ls->file_depth++];
    fe->fp = fp;
    snprintf(fe->filename, sizeof(fe->filename), "%s", filename);
    fe->line = 0;
    /* set basedir from first file */
    if (ls->file_depth == 1) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s", filename);
        snprintf(ls->basedir, sizeof(ls->basedir), "%s", dirname(tmp));
    }
    return 0;
}

void ls_close(LineSource *ls) {
    while (ls->file_depth > 0) {
        ls->file_depth--;
        if (ls->files[ls->file_depth].fp) fclose(ls->files[ls->file_depth].fp);
    }
    /* free any remaining line stack entries */
    for (int i = 0; i < ls->lstack_top; i++) free(ls->lstack[i]);

    ls->lstack_top = 0;
}

/* get next line: from line stack first, then from file */
int ls_getline(LineSource *ls, SrcLine *sl) {
    memset(sl, 0, sizeof(*sl));

    /* check line stack (LIFO) */
    if (ls->lstack_top > 0) {
        char *line = ls->lstack[--ls->lstack_top];
        snprintf(sl->raw, LINE_MAX, "%s", line);
        free(line);
        sl->origin = ls->lstack_origin;
        if (ls->file_depth > 0) {
            snprintf(sl->file, sizeof(sl->file), "%s",
                     ls->files[ls->file_depth - 1].filename);
            sl->line = ls->files[ls->file_depth - 1].line;
        }
        return 0;
    }
    /* read from current file */
    while (ls->file_depth > 0) {
        FileEntry *fe = &ls->files[ls->file_depth - 1];
        if (fgets(sl->raw, LINE_MAX, fe->fp)) {
            fe->line++;
            /* strip trailing newline */
            int len = (int)strlen(sl->raw);
            if (len > 0 && sl->raw[len - 1] == '\n') sl->raw[--len] = '\0';
            if (len > 0 && sl->raw[len - 1] == '\r') sl->raw[--len] = '\0';

            snprintf(sl->file, sizeof(sl->file), "%s", fe->filename);
            sl->line = fe->line;
            sl->origin = (ls->file_depth > 1) ? ORIG_INCLUDE : ORIG_SOURCE;
            return 0;
        }
        /* EOF — pop file */
        fclose(fe->fp);
        fe->fp = NULL;
        ls->file_depth--;
    }
    return -1; /* no more input */
}

/* push multi-line text onto line stack (split by \n, pushed in reverse for LIFO) */
void ls_push_lines(LineSource *ls, const char *text, Origin origin) {
    ls->lstack_origin = origin;
    /* count lines first */
    const char *p = text;
    int count = 0;
    const char *starts[LINE_STACK];
    int lens[LINE_STACK];
    while (*p && count < LINE_STACK) {
        starts[count] = p;
        const char *eol = strchr(p, '\n');
        if (eol) {
            lens[count] = (int)(eol - p);
            p = eol + 1;
        }
        else {
            lens[count] = (int)strlen(p);
            p += lens[count];
        }
        count++;
    }
    /* push in reverse order so first line comes out first */
    for (int i = count - 1; i >= 0 && ls->lstack_top < LINE_STACK; i--) {
        if (lens[i] == 0) continue;        /* skip empty lines */
        char *line = malloc(lens[i] + 1);
        memcpy(line, starts[i], lens[i]);
        line[lens[i]] = '\0';
        ls->lstack[ls->lstack_top++] = line;
    }
}

int ls_include(LineSource *ls, const char *filename) {
    if (ls->file_depth > 1) return -2;     /* nested include */
    char resolved[512];
    if (filename[0] == '/')
        snprintf(resolved, sizeof(resolved), "%s", filename);
    else
        snprintf(resolved, sizeof(resolved), "%s/%s", ls->basedir, filename);

    return ls_open(ls, resolved);
}

/* split a raw line into label / op / args / comment
   M80 logic: read first identifier.
   - If followed by ':', it's a label. Read next identifier as operator.
   - If followed by EQU/SET/DEFL/MACRO, first word is name, second is op.
   - Otherwise, first word IS the operator. */
void line_split(SrcLine *sl) {
    char *p = sl->raw;
    sl->label        = NULL;
    sl->label_public = 0;
    sl->label_colon  = 0;
    sl->op           = NULL;
    sl->args         = NULL;
    sl->comment      = NULL;

    /* skip leading whitespace including \f */
    while (isspace(*p)) p++;

    if (!*p || *p == ';') {
        if (*p == ';')
            sl->comment = p + 1;

        return;
    }
    /* read first word */
    char *word1 = p;
    while (*p && *p != ':' && !isspace(*p) && *p != ';') p++;

    if (*p == ':') {
        /* it's a label */
        *p++ = '\0';
        sl->label_colon = 1;
        sl->label = word1;
        if (*p == ':') {
            sl->label_public = 1;
            *p++ = '\0';
        }
        while (isspace(*p)) p++;

        if (!*p || *p == ';') {
            if (*p == ';') sl->comment = p + 1;
            return;
        }
        /* implicit DB: operator starts with quote */
        if (*p == '"' || *p == '\'') {
            sl->op = "DB";
            sl->args = p;
            return;
        }
        /* read operator after label */
        sl->op = p;
        while (*p && !isspace(*p) && *p != ';') p++;
    }
    else {
        /* no colon — word1 might be label (if next word is EQU/SET/DEFL/MACRO) or operator */
        char saved = *p;
        if (*p) *p++ = '\0';
        while (isspace(*p)) p++;

        /* peek at second word */
        char *word2 = p;
        char *w2end = p;
        while (*w2end && *w2end != ' ' && *w2end != '\t' && *w2end != ';' && *w2end != ',') w2end++;

        int w2len = (int)(w2end - word2);
        if (w2len >= 2 && w2len <= 5 &&
            ((w2len == 3 && strncasecmp(word2, "EQU", 3) == 0) ||
             (w2len == 3 && strncasecmp(word2, "SET", 3) == 0) ||
             (w2len == 4 && strncasecmp(word2, "DEFL", 4) == 0) ||
             (w2len == 5 && strncasecmp(word2, "MACRO", 5) == 0))) {
            /* word1 is the name, word2 is the operation */
            sl->label = word1;
            sl->op = word2;
            if (*w2end) {
                *w2end = '\0';
                p = w2end + 1;
            }
            else
                p = w2end;
        }
        else {
            /* word1 is the operator */
            sl->op = word1;
            /* if saved was space/tab, p is already at args position.
               if saved was null, word1 was the whole line */
            if (!saved) return;
            /* p already advanced past the separator */
        }
    }
    /* separator between op and args */
    if (*p && *p != ';') {
        if (isspace(*p)) {
            *p++ = '\0';
            while (isspace(*p)) p++;
        }
    }
    else if (*p == ';') {
        *p++ = '\0';
        sl->comment = p;
        return;
    }
    if (!*p || *p == ';') {
        if (*p == ';') sl->comment = p + 1;
        return;
    }
    /* arguments — everything until unquoted semicolon */
    sl->args = p;
    int in_quote = 0;
    while (*p) {
        if (*p == '\'' || *p == '"') {
            if (!in_quote && !((*p == '\'') && p > sl->args && isalnum((unsigned char)p[-1])))
                in_quote = *p;
            else if (*p == in_quote)
                in_quote = 0;
        }
        else if (*p == ';' && !in_quote) {
            *p++ = '\0';
            sl->comment = p;
            break;
        }
        p++;
    }
    /* trim trailing whitespace from args */
    if (sl->args) {
        int len = (int)strlen(sl->args);
        while (len > 0 && (isspace(sl->args[len - 1]))) sl->args[--len] = '\0';
    }
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
