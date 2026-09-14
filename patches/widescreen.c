// Widescreen, part one: the 3D view fills the frame, and what is at its edges
// is drawn.
//
// Pilotwings 64 draws its world into an inset viewport, (10, 18)-(310, 232)
// of the 320x240 screen, and paints a black border around it in 2D -- the
// region a CRT's overscan would have hidden. RT64 can widen a 3D pass to the
// display's shape, but only one that covers the frame, and a border drawn in
// 2D stays a 4:3 box in the middle of the picture however wide the pass
// behind it is. So:
//
// 1. The border is not drawn (drawScreenBorder).
// 2. A channel given the inset viewport gets the whole frame instead
//    (uvChan_80204D94), which is what lets RT64 widen it.
// 3. Every frustum shaped for the inset viewport -- the game uses one aspect,
//    300:214, for flight, the title and the menus alike -- is reshaped for the
//    frame it now fills (uvChan_80204C94): the horizontal extent is kept, so the
//    field of view is the cartridge's, and the vertical extent grows by the
//    5% the border used to cover. The world is neither stretched nor zoomed.
// 4. The game culls objects and terrain against planes built from that same
//    frustum, which would still be 4:3 while RT64 draws a wider view -- the
//    edges of a 16:9 picture would be empty. The planes are built with the
//    horizontal extent widened by the display's aspect over 4:3; the
//    projection matrix is left at 4:3, because widening it is RT64's job.
//
// 5. The clip ratio (uvChan_80204FE4). The game draws its environment -- the sky
//    dome, the sea around an island -- with a clip ratio of 1, which clips every
//    triangle at the edges of the 4:3 frame, and only its terrain and objects
//    with a ratio of 2. So the terrain reached the sides of a widened picture and
//    the sky above it stopped at the 4:3 edges. Both ratios are raised to at
//    least the widening, rounded up.
//
// All the game's frusta and viewports go through those kernel functions, so
// nothing per-screen is needed here.

#include "patches.h"

#include "uv_chan.h"
#include "uv_dobj.h"
#include "uv_environment.h"
#include "uv_fx.h"
#include "uv_sprite.h"
#include "uv_terrain.h"
#include "kernel/code_7150.h"

extern UnkStruct_80204D94 D_80261730[2];
extern s32 D_80263058;
extern s32 D_8026305C;
extern u8 D_80263060[1040];
extern f32 gGfxFogFactor;

// The decompilation's chan.c redefines this for the older Fast3D microcode the
// game uses; the patch has to emit the same command.
#undef gSPPerspNormalize
#define gSPPerspNormalize(pkt, s)                   \
    {                                               \
        Gfx* _g = (Gfx*)(pkt);                      \
                                                    \
        _g->words.w0 = _SHIFTL(G_RDPHALF_1, 24, 8); \
        _g->words.w1 = (s);                         \
    }

// gSPClipRatio with the ratio as a value rather than a FRUSTRATIO_n token:
// FR_NEG_FRUSTRATIO_n is n and FR_POS_FRUSTRATIO_n is 0x10000 - n.
static void clipRatio(s32 ratio) {
    gMoveWd(gGfxDisplayListHead++, G_MW_CLIP, G_MWO_CLIP_RNX, ratio);
    gMoveWd(gGfxDisplayListHead++, G_MW_CLIP, G_MWO_CLIP_RNY, ratio);
    gMoveWd(gGfxDisplayListHead++, G_MW_CLIP, G_MWO_CLIP_RPX, 0x10000 - ratio);
    gMoveWd(gGfxDisplayListHead++, G_MW_CLIP, G_MWO_CLIP_RPY, 0x10000 - ratio);
}

// The smallest clip ratio that lets geometry reach the edges of a frame widened
// by pw64_widescreen_factor(), and never less than the game asked for.
static s32 widenedClipRatio(s32 ratio) {
    f32 widen = pw64_widescreen_factor();
    s32 needed = (s32)widen;
    if ((f32)needed < widen) {
        needed++;
    }
    if (needed > 6) {
        needed = 6;
    }
    return needed > ratio ? needed : ratio;
}

// The aspect ratio of every 3D frustum the game builds for its inset viewport:
// 0.4906542 / 0.35 == 0.7009346 / 0.5 == 300 / 214.
#define INSET_ASPECT ((f32)(SUBSCREEN_X1 - SUBSCREEN_X0) / (f32)(SUBSCREEN_Y1 - SUBSCREEN_Y0))
#define FRAME_ASPECT ((f32)SCREEN_WIDTH / (f32)SCREEN_HEIGHT)

// Which channels were given the whole frame in place of the inset viewport.
static u8 sChannelExpanded[2];

RECOMP_PATCH void drawScreenBorder(void) {
    // The border existed to be hidden by a CRT; the picture now reaches the edges.
}

RECOMP_PATCH void uvChan_80204D94(s32 vpId, s32 x0, s32 x1, s32 y0, s32 y1) {
    UnkStruct_80204D94* chan = &D_80261730[vpId];

    if (x0 == SUBSCREEN_X0 && x1 == SUBSCREEN_X1 && y0 == SUBSCREEN_Y0 && y1 == SUBSCREEN_Y1) {
        x0 = 0;
        x1 = SCREEN_WIDTH;
        y0 = 0;
        y1 = SCREEN_HEIGHT;
        sChannelExpanded[vpId] = TRUE;
    } else {
        sChannelExpanded[vpId] = FALSE;
    }

    chan->viewX0 = x0;
    chan->viewX1 = x1;
    chan->viewY0 = y0;
    chan->viewY1 = y1;
    uvGfxClipViewport(vpId, x0, x1, y0, y1);
}

RECOMP_PATCH void uvChan_80204C94(s32 arg0, f32 x0, f32 x1, f32 y0, f32 y1, f32 near, f32 far) {
    UnkStruct_80204D94* chan = &D_80261730[arg0];
    f32 widen;
    f32 aspect;
    f32 saved_x0;
    f32 saved_x1;

    // Reshape a frustum made for the inset viewport to the frame it now fills.
    // Keyed on the frustum's own shape, so it is idempotent: the game feeds a
    // frustum it has already set back in (uvChan_80204FE4 rescales the stored
    // extents for its fog pass), and a frustum already at 4:3 is left alone.
    if (sChannelExpanded[arg0] && (y1 - y0) != 0.0f) {
        aspect = (x1 - x0) / (y1 - y0);
        if (aspect > INSET_ASPECT * 0.99f && aspect < INSET_ASPECT * 1.01f) {
            y0 *= aspect / FRAME_ASPECT;
            y1 *= aspect / FRAME_ASPECT;
        }
    }

    chan->unk1E8 = x0;
    chan->unk1EC = x1;
    chan->unk1F0 = y0;
    chan->unk1F4 = y1;
    chan->unk1F8 = near;
    chan->unk1FC = far;
    uvMat4SetFrustrum(&chan->unk10, x0, x1, y0, y1, near, far);
    uvMat4CopyF2L(&chan->unk50, &chan->unk10);
    uvMat4SetFrustrum(&chan->unk90, x0, x1, y0, y1, near, 27000.0f);
    uvMat4CopyF2L(&chan->unkD0, &chan->unk90);

    // The culling planes, built from the horizontal extent RT64 will actually
    // show. func_802061A0 reads the extents from the channel, so they are
    // widened around the call and put back.
    widen = pw64_widescreen_factor();
    saved_x0 = chan->unk1E8;
    saved_x1 = chan->unk1EC;
    chan->unk1E8 = saved_x0 * widen;
    chan->unk1EC = saved_x1 * widen;
    func_802061A0(chan);
    chan->unk1E8 = saved_x0;
    chan->unk1EC = saved_x1;
}

// The decompilation's uvChan_80204FE4 (src/kernel/chan.c), which draws one
// camera channel, with the two clip ratios raised; see 5 above.
RECOMP_PATCH void uvChan_80204FE4(s32 arg0) {
    UnkStruct_80204D94* temp_s0;
    ParsedUVTR* uvtr;
    s32 var_v0;
    f32 spC0;
    f32 spBC;
    f32 spB8;
    f32 spB4;
    f32 spB0;
    f32 spAC;
    f32 spA8;
    f32 temp_fv1;
    s32 spA0;

    D_80263058 = 0;
    D_8026305C = 0;
    temp_s0 = &D_80261730[arg0];

    if (temp_s0->unk0 == 0) {
        return;
    }
    if (temp_s0->unk398 != NULL) {
        temp_s0->unk398();
    }
    func_80206318(temp_s0);
    uvGfxMtxProj(temp_s0->unk50);
    uvGfxPushMtxUnk(&temp_s0->unk190);
    gSPPerspNormalize(gGfxDisplayListHead++, (s16)(131072.0f / (temp_s0->unk1FC + temp_s0->unk1F8)));
    uvGfxViewport(arg0);
    if (!(temp_s0->unk0 & 4)) {
        uvGfx_80222A98();
    }

    clipRatio(widenedClipRatio(1));

    _uvEnvDraw(arg0, temp_s0->unk2);
    if (!(temp_s0->unk0 & 8)) {
        clipRatio(widenedClipRatio(2));
    }
    gDPPipeSync(gGfxDisplayListHead++);
    gSPSetGeometryMode(gGfxDisplayListHead++, G_ZBUFFER);
    gSPSetGeometryMode(gGfxDisplayListHead++, G_SHADE);
    gDPSetCombineMode(gGfxDisplayListHead++, G_CC_SHADE, G_CC_PASS2);

    if (gGfxFogFactor > 0.0f) {
        gDPSetRenderMode(gGfxDisplayListHead++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);
    } else {
        gDPSetRenderMode(gGfxDisplayListHead++, G_RM_PASS, G_RM_AA_ZB_OPA_SURF2);
    }
    if (temp_s0->unk0 & 2) {
        _uvTerraDraw(temp_s0, temp_s0->unk4);
        uvtr = gGfxUnkPtrs->terras[temp_s0->unk4];
        if (uvtr != NULL) {
            uvMemSet(D_80263060, 0, uvtr->unk18 * uvtr->unk19);
        }
    } else {
        uvtr = NULL;
    }
    uvFx_8021EA38(temp_s0);
    _uvDobjsDraw(temp_s0, 0);

    if (FABS(0.996f - gGfxFogFactor) < 0.0001f) {
        var_v0 = 1;
    } else {
        var_v0 = 0;
    }
    if (var_v0 && (uvtr != NULL)) {
        spBC = temp_s0->unk1E8;
        spB8 = temp_s0->unk1EC;
        spB4 = temp_s0->unk1F0;
        spB0 = temp_s0->unk1F4;
        spAC = temp_s0->unk1F8;
        spA8 = temp_s0->unk1FC;
        temp_fv1 = temp_s0->unk1FC / (spAC * 50.0f);
        spC0 = temp_s0->unk1FC * 0.66667f;
        if (spC0 < uvtr->unk24) {
            spC0 = 1e11f;
        }
        uvChan_80204C94(temp_s0 - D_80261730, spBC * temp_fv1, spB8 * temp_fv1, spB4 * temp_fv1, spB0 * temp_fv1, spAC * temp_fv1, spA8);
        uvGfxMtxProj(temp_s0->unk50);
        uvTerra_8022EE90(temp_s0, uvtr, spC0);
        uvGfxSetFogFactor(0.0f);
        spA0 = D_80263058;
        uvChan_80205BFC();
        uvChan_80205CE4(temp_s0, 0, spC0 - uvtr->unk24, 1e12f);
        uvChan_80205CE4(temp_s0, 1, spC0 - uvtr->unk24, 1e12f);
        uvGfxResetState();
        uvGfx_80222A98();
        uvChan_80204C94(temp_s0 - D_80261730, spBC, spB8, spB4, spB0, spAC, spA8);
        uvGfxMtxProj(temp_s0->unk50);
        uvTerra_8022EFB4(temp_s0, uvtr, spC0);
        if (spA0 != D_80263058) {
            uvChan_80205BFC();
        }
        uvChan_80205CE4(temp_s0, 0, 0.0f, spC0 - uvtr->unk24);
        uvChan_80205CE4(temp_s0, 1, 0.0f, spC0 - uvtr->unk24);
    } else {
        if (uvtr != NULL) {
            uvTerra_8022EE90(temp_s0, uvtr, 0.0f);
        }
        uvChan_80205BFC();
        uvChan_80205CE4(temp_s0, 0, -1.0f, 1e12f);
        uvChan_80205CE4(temp_s0, 1, -1.0f, 1e12f);
    }
    _uvDobjsDraw(temp_s0, 1);
    uvSprtDrawAll();
    uvDobj_8021771C(temp_s0);
    uvGfxResetState();
    if (temp_s0->unk39C != NULL) {
        temp_s0->unk39C();
    }
}
