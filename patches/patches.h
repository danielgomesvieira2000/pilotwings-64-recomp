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

// A scissor over the whole widened frame, from its left edge to its right edge.
//
// Under an extended origin, RT64 measures a coordinate from that origin's edge
// of the game's 320-wide screen: RIGHT adds 320 pixels (RDP::movedFromOrigin). So
// the right edge of the frame is 0 under G_EX_ORIGIN_RIGHT, not SCREEN_WIDTH.
// Getting that wrong is not merely a wider scissor: RT64 merges every scissor of
// a frame and treats the frame as the game's 4:3 picture only if the merged
// shape is 4:3 (rt64_framebuffer_renderer.cpp, adjustRatio), and a scissor
// reaching 640 pixels switches that off for the whole frame -- every 2D
// rectangle is then stretched across the widened frame instead of kept at 4:3.
#define gEXSetScissorWideFrame(cmd) \
    gEXSetScissor(cmd, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, SCREEN_HEIGHT)

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

// How far, in the game's 320x240 pixels, a HUD element at the side of the 4:3
// screen moves towards that edge of the widened frame: half the extra width,
// scaled by the HUD Placement setting (none at Original, all of it at Full,
// part of it for 16:9) the way RT64 scales its own extended origins.
f32 pw64_hud_margin(void);

// Prints a tag and three integers when PW64_PATCH_DEBUG is set. Integers only,
// so all four arguments travel in a0-a3.
void pw64_debug(u32 tag, s32 a, s32 b, s32 c);

// Whether patches/interpolation.c emits its matrix groups: yes unless
// PW64_NO_INTERP_TAGS is set, which is for measuring RT64 without them.
s32 pw64_interp_tags_enabled(void);

// Delivers a pending runtime event, if there is one, and switches to any
// higher-priority game thread that is ready: what preemption would do at this
// point on the N64. See patches/clocks.c.
void pw64_poll_threads(void);

// A profiling mark, when PW64_PATCH_DEBUG is set: the time since the previous
// mark (of any tag) is charged to this tag (0-15), and the average per tag is
// printed every two seconds, or at once on tag 0. For finding where a slow loop
// spends its time.
void pw64_profile_mark(u32 tag);

#endif
