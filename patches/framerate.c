// Tells the port each time the game presents a frame.
//
// The game's frame rate is the number that decides everything about high frame
// rate: whether RT64 has frames to interpolate between, and how far apart they
// are. The scheduler's graphics-done handler is where a finished frame is
// swapped to the screen, so that is where the port counts them.
//
// Wave Race 64's port measured this by registering a wrapper at
// osViSwapBuffer's address, which only worked because every call in that port
// went through the function lookup. Here calls are direct, so the scheduler
// function itself is replaced -- a transcription of the decompilation's
// _uvScDoneGfx (src/kernel/sched.c) with one call added.

#include "patches.h"

extern OSScTask* D_802B9C60[2];
extern u8 D_802B9C68;
extern u8 D_802B9C6C;
extern u8 D_802B9C6E;
extern u8 gSchedRspStatus;
extern u8 gSchedRdpStatus;
extern s32 gNmiAsserted;


RECOMP_PATCH void _uvScDoneGfx(void) {
    OSScTask* scTask = D_802B9C60[D_802B9C6E];

    if (gNmiAsserted == 0) {
        if (scTask == NULL) {
            _uvDebugPrintf("_uvScDoneGfx -- no gfx task\n");
            _uvScLogIntoRing();
            return;
        }
        if (D_802B9C6C == 0) {
            if ((gSchedRspStatus == 'g') || (gSchedRdpStatus != 0)) {
                _uvDebugPrintf("_uvScDoneGfx:  rsp [%c]    rdp [%c]\n", gSchedRspStatus, gSchedRdpStatus);
            } else {
                uvSc_8022C3C0(0, 0x32);
                osSendMesg(scTask->msgQ, scTask->msg, 1);
                D_802B9C68 = 1;
                osViSwapBuffer(scTask->framebuffer);
                D_802B9C60[D_802B9C6E] = NULL;
                pw64_frame_presented();
            }
        }
    }
}
