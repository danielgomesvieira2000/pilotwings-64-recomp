// The frame's display list: RT64's extended GBI, and a clear that reaches the
// edges of a widened frame.

#include "patches.h"

#include "uv_audio.h"
#include "uv_fx.h"
#include "uv_geometry.h"
#include "uv_sprite.h"

extern s32 gGfxBeginFlag;
extern void* gGfxFbCurrPtr;
extern s32 gGfxNumVtxTransforms[2];
extern s32 gGfxNumTriangles[2];
extern s32 gGfxNumTxtLoads[2];
extern s32 gGfxNumMtxLoads[2];
extern s32 gGfxNumMtxLoadMults[2];
extern s32 gGfxNumLooks;
extern s16 gGfxViewX0;
extern s16 gGfxViewX1;
extern s16 gGfxViewY0;
extern s16 gGfxViewY1;

// uvGfxBegin starts every frame's display list, so it is where the extended GBI
// is switched on: RT64 honours its commands only in a list that has enabled
// them, and forgets them at the end of each list. The rest is the
// decompilation's uvGfxBegin (src/kernel/graphics.c) unchanged.
RECOMP_PATCH void uvGfxBegin(void) {
    if (gGfxBeginFlag == TRUE) {
        _uvDebugPrintf("uvGfxBegin: 2 calls in a row.  Must call uvGfxEnd first\n");
        return;
    }
    gGfxBeginFlag = TRUE;
    uvEventPost(0, 0);
    gEXEnable(gGfxDisplayListHead++);
    gSPSegment(gGfxDisplayListHead++, 0x00, 0x00000000);
    gDPSetColorImage(gGfxDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, osVirtualToPhysical(gGfxFbCurrPtr));

    uvGfxResetState();
    uvSeqUpdateAll();
    uvSprt_802301A4();
    uvVtxReset(1);
    uvFx_8021A298();

    gGfxNumVtxTransforms[gGfxFbIndex] = 0;
    gGfxNumTriangles[gGfxFbIndex] = 0;
    gGfxNumTxtLoads[gGfxFbIndex] = 0;
    gGfxNumMtxLoads[gGfxFbIndex] = 0;
    gGfxNumMtxLoadMults[gGfxFbIndex] = 0;
    gGfxNumLooks = 0;
}

// The screen clear is a fill rectangle over the current viewport, and RT64 keeps
// a plain fill rectangle at 4:3 in the middle of a widened frame -- which left
// the sides of every widescreen frame uncleared, showing whatever the previous
// frames had drawn there. When the viewport is the whole screen (which
// patches/widescreen.c makes of the game's inset viewport), the clear is
// anchored to both edges of the frame instead, under a scissor widened the same
// way.
RECOMP_PATCH void uvGfxClearScreen(u8 r, u8 g, u8 b, u8 a) {
    s32 fullFrame = (gGfxViewX0 == 0) && (gGfxViewX1 == SCREEN_WIDTH) && (gGfxViewY0 == 0) && (gGfxViewY1 == SCREEN_HEIGHT);

    gDPPipeSync(gGfxDisplayListHead++);
    gDPSetCycleType(gGfxDisplayListHead++, G_CYC_FILL);
    gDPSetColorImage(gGfxDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, osVirtualToPhysical(gGfxFbCurrPtr));
    gDPSetFillColor(gGfxDisplayListHead++, GPACK_RGBA5551(r, g, b, a) << 16 | GPACK_RGBA5551(r, g, b, a));
    if (fullFrame) {
        // Both measured from their own edge: 0 under RIGHT is the right edge
        // (see gEXSetScissorWideFrame in patches.h).
        gEXSetScissorWideFrame(gGfxDisplayListHead++);
        gEXFillRectangle(gGfxDisplayListHead++, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, SCREEN_HEIGHT - 1);
    } else {
        gDPFillRectangle(gGfxDisplayListHead++, gGfxViewX0, (SCREEN_HEIGHT - gGfxViewY1), (gGfxViewX1 - 1), (SCREEN_HEIGHT - 1 - gGfxViewY0));
    }
    gDPPipeSync(gGfxDisplayListHead++);
    gDPSetCycleType(gGfxDisplayListHead++, G_CYC_2CYCLE);
}
