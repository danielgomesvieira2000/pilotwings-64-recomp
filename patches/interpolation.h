#ifndef PW64_INTERPOLATION_H
#define PW64_INTERPOLATION_H

// Matrix groups for RT64's frame interpolation. See patches/interpolation.c.

#include "patches.h"

// What a transform belongs to, which is folded into its group id.
typedef enum {
    INTERP_KIND_CAMERA = 1,
    INTERP_KIND_NEAR_PROJECTION,
    INTERP_KIND_DOBJ,
    INTERP_KIND_SOBJ,
    INTERP_KIND_ENV,
    INTERP_KIND_FX,
} InterpKind;

// A group id for (kind, a, b, c) in the current camera generation. Never
// G_EX_ID_IGNORE or G_EX_ID_AUTO.
u32 interpId(InterpKind kind, u32 a, u32 b, u32 c);

// Called as a channel starts drawing: notices camera cuts (which start a new
// generation, so nothing from before the cut is interpolated into it) and pushes
// the channel's projection group. interpCameraEnd pops it.
void interpCameraBegin(s32 channel, UnkStruct_80204D94* chan);
void interpCameraEnd(void);

// A model-view group for one object's whole draw: all the matrices it loads,
// its parts included, paired with the previous frame's in drawing order.
void interpModelBegin(u32 id);
// The same, but RT64 pairs the group's transforms with the previous frame's by
// its own matching (nearest similar draw) rather than in drawing order: for
// effects, whose pieces come and go.
void interpModelBeginAutoOrder(u32 id);
// A model-view group that is not interpolated at all.
void interpModelBeginIgnore(void);
void interpModelEnd(void);

// A projection group of its own, for the per-object projections of the near
// object pass.
void interpProjectionBegin(u32 id);
void interpProjectionEnd(void);

#endif
