#include <string.h>
#include "segment.h"

void segment_init(Segment *seg) {
    memset(seg, 0, sizeof(*seg));
    seg->type = SEG_CODE;   /* M80 default is CSEG */
}

int segment_get_loc(Segment *seg) {
    switch (seg->type) {
    case SEG_CODE:   return seg->code_loc;
    case SEG_DATA:   return seg->data_loc;
    case SEG_ABS:    return seg->abs_loc;
    case SEG_COMMON: return seg->common_loc[seg->common_id];
    }
    return 0;
}

void segment_set_loc(Segment *seg, int loc) {
    switch (seg->type) {
    case SEG_CODE:   seg->code_loc                   = loc; break;
    case SEG_DATA:   seg->data_loc                   = loc; break;
    case SEG_ABS:    seg->abs_loc                    = loc; break;
    case SEG_COMMON: seg->common_loc[seg->common_id] = loc; break;
    }
}

void segment_advance(Segment *seg, int bytes) {
    segment_set_loc(seg, segment_get_loc(seg) + bytes);
    if (seg->in_phase)
        seg->phase_addr += bytes;
}

void segment_set_type(Segment *seg, SegType type, int common_id) {
    seg->type = type;
    seg->common_id = common_id;
}

ValMode segment_mode(Segment *seg) {
    if (seg->in_phase) return MODE_ABSOLUTE;
    switch (seg->type) {
    case SEG_CODE:   return MODE_CODE_REL;
    case SEG_DATA:   return MODE_DATA_REL;
    case SEG_ABS:    return MODE_ABSOLUTE;
    case SEG_COMMON: return MODE_COMMON;
    }
    return MODE_ABSOLUTE;
}

Value segment_here(Segment *seg) {
    if (seg->in_phase) return val_absolute(seg->phase_addr);
    Value v = {0};
    v.num = segment_get_loc(seg);
    v.mode = segment_mode(seg);
    if (seg->type == SEG_COMMON) v.common_id = seg->common_id;
    return v;
}

void segment_phase(Segment *seg, int addr) {
    seg->in_phase        = 1;
    seg->phase_origin    = addr;
    seg->phase_addr      = addr;
    seg->phase_start_loc = segment_get_loc(seg);
}

void segment_dephase(Segment *seg) {
    seg->in_phase = 0;
}

// vim: tabstop=4 shiftwidth=4 softtabstop=4 autoindent expandtab
