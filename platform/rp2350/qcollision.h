#ifndef QRP_QCOLLISION_H
#define QRP_QCOLLISION_H
#include "qbsp.h"
enum { QC_TRACE_STACK=64 };
typedef struct {
    float fraction,end[3],normal[3],distance;
    bool startsolid,allsolid,inopen,inwater;
} qc_trace_t;
typedef struct {
    int32_t node,far;
    uint32_t plane;
    unsigned side,state;
    float a[3],b[3],mid[3],af,bf,frac;
} qc_frame_t;
/* Caller-owned scratch, one per concurrent caller; never place on a 4 KiB core
 * stack. Explicit traversal stack replaces Quake's recursive hull walk. */
typedef struct { qc_frame_t frames[QC_TRACE_STACK]; } qc_workspace_t;
/* Quake epsilon-biased swept hull query in model coordinates. Hull 0 is a
 * point; hull 1 is the original standing-player volume (-16,-16,-24)..(16,16,32).
 * Model transforms, dynamic entities and arbitrary bounding boxes are not done
 * here. false = invalid data/coordinates or traversal limit; output fails closed. */
bool qc_trace(qc_workspace_t *work,const qbsp_t *bsp,uint32_t model,unsigned hull,
              const float start[3],const float end[3],qc_trace_t *trace);
/* Bounded four-bump slide against world model hull 1. No gravity, jumping or
 * stair stepping. Input/output is player origin (camera eye is origin + 22 Z).
 * On error or start-solid returns false and leaves position unchanged. */
bool qc_slide(qc_workspace_t *work,const qbsp_t *bsp,float position[3],const float displacement[3]);
#endif
