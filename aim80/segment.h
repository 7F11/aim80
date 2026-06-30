#ifndef SEGMENT_H
#define SEGMENT_H
#include "symtab.h"

typedef enum {
    SEG_CODE,       /* CSEG - default */
    SEG_DATA,       /* DSEG */
    SEG_ABS,        /* ASEG */
    SEG_COMMON      /* COMMON /name/ */
} SegType;

typedef struct {
    SegType type;
    int common_id;          /* if SEG_COMMON */

    /* location counters — one per segment */
    int code_loc;
    int data_loc;
    int abs_loc;
    int common_loc[COMMON_MAX];

    /* .PHASE state */
    int in_phase;           /* inside .PHASE block */
    int phase_addr;         /* current phase address */
    int phase_origin;       /* phase origin (from .PHASE arg) */
    int phase_start_loc;    /* real loc counter when .PHASE was entered */
} Segment;

void segment_init(Segment *seg);
int  segment_get_loc(Segment *seg);
void segment_set_loc(Segment *seg, int loc);
void segment_advance(Segment *seg, int bytes);
void segment_set_type(Segment *seg, SegType type, int common_id);

/* get the ValMode corresponding to current segment */
ValMode segment_mode(Segment *seg);

/* make a Value at the current location counter */
Value segment_here(Segment *seg);

/* .PHASE / .DEPHASE */
void segment_phase(Segment *seg, int addr);
void segment_dephase(Segment *seg);

#endif

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
