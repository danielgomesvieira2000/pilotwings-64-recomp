#ifndef PW64_PATCHES_H
#define PW64_PATCHES_H

// Common header for the port's C patches.
//
// A patch is C written against the decompilation's own headers, compiled for
// MIPS by Clang, and recompiled by N64Recomp exactly as the game was. A function
// marked RECOMP_PATCH replaces the game's function of the same name; anything
// else here is new code the patches can call. See docs/PORTING.md, "C patches".

// Section markers N64Recomp understands (see N64Recomp's context.h).
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH __attribute__((section(".recomp_force_patch")))
#define RECOMP_EXPORT __attribute__((section(".recomp_export")))

// libultra functions the runtime reimplements have no MIPS code to call. The
// recompiled game calls them as <name>_recomp, and patches reach the same host
// functions through the addresses patches/syms.ld gives those names.
#define osSendMesg osSendMesg_recomp
#define osRecvMesg osRecvMesg_recomp
#define osViSwapBuffer osViSwapBuffer_recomp
#define osGetTime osGetTime_recomp
#define osWritebackDCache osWritebackDCache_recomp
#define osInvalDCache osInvalDCache_recomp
#define osVirtualToPhysical osVirtualToPhysical_recomp

#include "common.h"
#include "global.h"

#include "rt64_extended_gbi.h"

// A fill rectangle whose left and right edges are each placed relative to an
// origin of the widened frame. RT64 implements the command (G_EX_FILLRECT_V1,
// fillrectV1 in src/gbi/rt64_gbi_extended.cpp) but the header at this revision
// defines no macro for it. Coordinates are whole pixels on the 320x240 screen.
#ifndef gEXFillRectangle
#define gEXFillRectangle(cmd, lorigin, rorigin, ulx, uly, lrx, lry) \
    G_EX_COMMAND2(cmd, \
        PARAM(RT64_EXTENDED_OPCODE, 8, 24) | PARAM(G_EX_FILLRECT_V1, 24, 0), \
        PARAM(lorigin, 12, 0) | PARAM(rorigin, 12, 12), \
        \
        PARAM((ulx) * 4, 16, 16) | PARAM((uly) * 4, 16, 0), \
        PARAM((lrx) * 4, 16, 16) | PARAM((lry) * 4, 16, 0) \
    )
#endif

// ---- Host functions: implemented by the port in C++ (src/patch_host.cpp) ----
//
// Each has an address in patches/syms.ld between 0x8F000000 and 0x90000000,
// which N64Recomp turns into a direct call to the C++ function of that name.
// Arguments and results follow the MIPS O32 convention: the host reads a0-a3
// and writes v0 (or f0 for a float).

// Called once per frame the game presents, from the scheduler.
void pw64_frame_presented(void);

// How much wider than 4:3 RT64 is drawing the 3D view: the window's aspect over
// 4:3 when Aspect Ratio is Expand, and 1 otherwise (or for a window narrower
// than 4:3, where RT64 does not narrow the view).
f32 pw64_widescreen_factor(void);

#endif
