// Widescreen, part two: the flight HUD at the edges of the frame.
//
// RT64 keeps 2D at 4:3 in the middle of a widened frame. The game draws its HUD
// from texture rectangles (sprites and text, through libultra's sprite library)
// and triangles (the gauges, the radar, the bars), and an element moved to an
// edge has to have both moved by the same distance; alignBegin below says how.
//
// The game's HUD is assembled per vehicle from the same few elements at fixed
// positions: speed, fuel, throttle and the timer down the left, sea level, the
// altimeter and the radar down the right, the photo count and messages in the
// middle. Each vehicle's HUD function is replaced here by one that draws the
// same elements in groups, each under the alignment for its side. Text is queued
// by uvFontPrintStr and only turned into draw commands by uvFontGenDlist, so each
// group flushes its text before the alignment changes.
//
// The Cannonball HUD is left as the game draws it, centred: its target bar spans
// nearly the whole 4:3 screen and belongs to neither side.
//
// The HUD's full-screen fades (hudDrawBox, and Sky Diving's cloud transition)
// and the camera's shutter are made to cover the widened frame rather than the
// 4:3 middle of it.

#include "patches.h"

#include "uv_font.h"
#include "uv_geometry.h"
#include "uv_sprite.h"
#include "app/hud.h"
#include "app/game.h"
#include "app/snd.h"

extern uvGfxViewport_t gGfxViewports[4];
extern s32 D_8034F914;
extern HUDState sHudState;

void hudDrawPhotoCount(void);
void hudDrawSpeed(s32 x, s32 y, s32 speed, s32 highlightLowSpeed);
void hudDrawFuel(s32 x, s32 y, f32 fuel);
void hudDrawThrottle(s32 x, s32 y, f32 power);
void hudDrawAltimeter(s32 x, s32 y, s32 altitude);
void hudSeaLevel(s32 x, s32 y, s32 alt);
void hudDrawTimer(s32 x, s32 y, f32 timeSecF);
void hudDrawAimReticle(s32 x, s32 y, s32 flag);
void hudDrawRadar(s32 x, s32 y, f32 xOff, f32 yOff, f32 heading, f32 pitch, HUDRadar* radar);
void hudDrawCamera(HUDState* hud);

#define CAMERA_SHUTTER_FRAMES 3

// The HUD's own orthographic projection (hudMainRender), shifted sideways.
static void hudProjection(f32 shift) {
    Mtx4F ortho;

    uvMat4SetOrtho(&ortho, -0.5f - shift, SCREEN_WIDTH - 0.5f - shift, -0.5f, SCREEN_HEIGHT - 0.5f);
    uvGfxMtxProjPushF(&ortho);
}

// Moves everything drawn until alignEnd() by the HUD margin, towards the given
// side: -1 for the left edge, +1 for the right.
//
// Rectangles and triangles have to move by exactly the same distance, or an
// element made of both comes apart. RT64 anchors the two differently -- a
// rectangle with plain arithmetic, a viewport-aligned triangle through the
// viewport's extent scaled about its origin (convertViewportRect) -- and the
// first attempt, with origins, left the altimeter's box and bar beside its own
// digits. (That attempt also had the wide scissor wrong, see
// gEXSetScissorWideFrame, so how much of the drift was which is not known.) So the
// distance is worked out once, on the host (pw64_hud_margin, which follows the
// Aspect Ratio and HUD Placement settings the way RT64 does), and applied to
// both: to rectangles as a plain rect offset with no origin, and to triangles by
// shifting the HUD's orthographic projection. The projection is the one matrix
// none of the gauges load for themselves -- the radar and the throttle load
// their own model-view matrices.
static void alignBegin(s32 side) {
    // Whole pixels, so that rectangles and triangles round the same way.
    f32 shift = pw64_hud_margin() * side;
    s32 pixels = (s32)(shift + (shift < 0.0f ? -0.5f : 0.5f));

    // Elements moved outside the 4:3 middle must not be scissored away. (The clip
    // ratio is left as the 3D pass set it, 2 or more, which already keeps
    // triangles there.)
    gEXSetScissorWideFrame(gGfxDisplayListHead++);
    // Rectangles: the offset is added after RT64 has decoded the rectangle, in
    // quarter pixels. Their own coordinates cannot carry the shift -- libultra's
    // sprite library clips them to the 320-wide screen in software, and a Fast3D
    // texture rectangle has no negative coordinates.
    gEXSetRectAlign(gGfxDisplayListHead++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, pixels * 4, 0, pixels * 4, 0);
    // Triangles.
    hudProjection((f32)pixels);
}

static void alignEnd(void) {
    // Text queued by the group is emitted under the group's offset.
    uvFontGenDlist();
    gEXSetRectAlign(gGfxDisplayListHead++, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    hudProjection(0.0f);
}

#define ALIGN_LEFT -1
#define ALIGN_RIGHT 1

// uvFontGenDlist turns the queued text into sprite display lists, written into
// sFontDList from a cursor it resets once per frame. The game calls it once per
// frame, and the cursor advances by the number of *messages* rather than by the
// commands written, which is harmless when there is one call and destructive
// when there are several: each later call wrote over the display lists the
// earlier one had already put in the frame, and the HUD's digits vanished. The
// HUD groups above flush the text several times a frame, so the cursor is kept
// where spDraw left it instead.
// As declared in the decompilation's src/kernel/font.c, which keeps it private.
typedef struct {
    s32 x;
    s32 y;
    f32 scaleX;
    f32 scaleY;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    s16 str16[44];
    ParsedUVFT* font;
} FontMessage; // size = 0x70

extern s32 sFontMsgCount;
extern FontMessage sFontMessages[30];
extern Sprite sFontSprite;
extern s32 gGfxFrameCount;
void uvFontMsgGenDlist(FontMessage* msg);

static s32 sFontCursorFrame = -1;
static Gfx* sFontCursor;

RECOMP_PATCH void uvFontGenDlist(void) {
    s32 i;

    if (sFontMsgCount == 0) {
        return;
    }

    if (sFontCursorFrame != gGfxFrameCount) {
        sFontCursorFrame = gGfxFrameCount;
        sFontCursor = &sFontSprite.rsp_dl[gGfxFbIndex * 7944];
    }
    spInit(&gGfxDisplayListHead);
    sFontSprite.rsp_dl_next = sFontCursor;

    for (i = 0; i < sFontMsgCount; i++) {
        uvFontMsgGenDlist(&sFontMessages[i]);
    }
    spFinish(&gGfxDisplayListHead);
    gGfxDisplayListHead--;
    sFontCursor = sFontSprite.rsp_dl_next;
    sFontMsgCount = 0;
}

// A quad over the whole widened frame, in the HUD's 320x240 orthographic space.
// Vertices outside the 4:3 screen are kept by the clip ratio the channel draw
// set (patches/widescreen.c) and drawn under a widened scissor.
static void fullFrameQuad(u8 r, u8 g, u8 b, u8 a) {
    s32 x0 = -(SCREEN_WIDTH / 2) + 10;
    s32 x1 = SCREEN_WIDTH + (SCREEN_WIDTH / 2) - 10;

    gEXSetScissorWideFrame(gGfxDisplayListHead++);
    uvVtxBeginPoly();
    uvVtx(x0, 0, 0, 0, 0, r, g, b, a);
    uvVtx(x1, 0, 0, 0, 0, r, g, b, a);
    uvVtx(x1, SCREEN_HEIGHT, 0, 0, 0, r, g, b, a);
    uvVtx(x0, SCREEN_HEIGHT, 0, 0, 0, r, g, b, a);
    uvVtxEndPoly();
}

RECOMP_PATCH void hudDrawHangGlider(HUDState* hud) {
    alignBegin(ALIGN_LEFT);
    hudDrawSpeed(27, 37, (s32)hud->speed, 1);
    hudDrawTimer(27, 222, hud->elapsedTime);
    alignEnd();

    alignBegin(ALIGN_RIGHT);
    hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
    hudDrawAltimeter(250, 129, (s32)hud->altitude);
    hudDrawRadar(215, 222, hud->att.x, hud->att.y, hud->att.heading, hud->att.pitch, &hud->radar);
    alignEnd();

    hudDrawPhotoCount();
    hudDrawCamera(hud);
}

RECOMP_PATCH void hudDrawRocketPack(HUDState* hud) {
    alignBegin(ALIGN_RIGHT);
    hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
    hudDrawAltimeter(250, 129, (s32)hud->altitude);
    hudDrawRadar(215, 222, hud->att.x, hud->att.y, hud->att.heading, hud->att.pitch, &hud->radar);
    alignEnd();

    alignBegin(ALIGN_LEFT);
    hudDrawSpeed(27, 37, (s32)hud->speed, 0);
    hudDrawFuel(98, 37, hud->fuel);
    hudDrawTimer(27, 222, hud->elapsedTime);
    alignEnd();
}

RECOMP_PATCH void hudDrawGyrocopter(HUDState* hud) {
    alignBegin(ALIGN_LEFT);
    hudDrawSpeed(27, 37, (s32)hud->speed, 0);
    hudDrawFuel(98, 37, hud->fuel);
    hudDrawThrottle(27, 82, hud->power);
    hudDrawTimer(27, 222, hud->elapsedTime);
    alignEnd();

    alignBegin(ALIGN_RIGHT);
    hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
    hudDrawAltimeter(250, 129, (s32)hud->altitude);
    hudDrawRadar(215, 222, hud->att.x, hud->att.y, hud->att.heading, hud->att.pitch, &hud->radar);
    alignEnd();

    // The reticle marks a point in the 3D view, which RT64 widens about its
    // centre, so it stays in the centre's frame of reference.
    if (hud->renderFlags & HUD_RENDER_RETICLE) {
        hudDrawAimReticle((s32)hud->reticleX, (s32)hud->reticleY, 0);
    }
}

RECOMP_PATCH void hudDrawSkyDiving(HUDState* hud) {
    // For sky diving, cloudFade is the alpha of the clouds that appear during the
    // transition from the formations to the landing. It goes 00 -> FF -> 00.
    if (hud->cloudFade != 0) {
        uvGfxStatePush();
        uvGfxSetFlags(GFX_STATE_XLU | GFX_STATE_TEXTURE_NONE);
        fullFrameQuad(0xFF, 0xFF, 0xFF, hud->cloudFade);
        uvGfxStatePop();
    } else {
        alignBegin(ALIGN_RIGHT);
        hudDrawAltimeter(250, 129, (s32)hud->altitude);
        hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
        hudDrawRadar(215, 222, hud->att.x, hud->att.y, hud->att.heading, hud->att.pitch, &hud->radar);
        alignEnd();

        alignBegin(ALIGN_LEFT);
        hudDrawTimer(27, 222, hud->elapsedTime);
        alignEnd();
    }
}

RECOMP_PATCH void hudDrawJumbleHopper(HUDState* hud) {
    alignBegin(ALIGN_LEFT);
    hudDrawSpeed(27, 37, (s32)hud->speed, 0);
    hudDrawTimer(27, 222, hud->elapsedTime);
    alignEnd();

    alignBegin(ALIGN_RIGHT);
    hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
    hudDrawAltimeter(250, 129, (s32)hud->altitude);
    hudDrawRadar(215, 222, hud->att.x, hud->att.y, hud->att.heading, hud->att.pitch, &hud->radar);
    alignEnd();
}

RECOMP_PATCH void hudDrawBirdman(HUDState* hud) {
    if (D_80362690->unkC[D_80362690->unk9C].unk7B == 0) {
        hudDrawPhotoCount();
    }

    alignBegin(ALIGN_RIGHT);
    hudSeaLevel(235, 37, (s32)hud->altSeaLevel);
    hudDrawAltimeter(250, 129, (s32)hud->altitude);
    alignEnd();

    alignBegin(ALIGN_LEFT);
    hudDrawSpeed(27, 37, (s32)hud->speed, 0);
    alignEnd();

    if (D_80362690->unkC[D_80362690->unk9C].unk7B == 0) {
        hudDrawCamera(hud);
    }
}

// The camera's shutter closes in from both sides of the screen. Each half is
// anchored to its own edge and reaches past it, so the widened frame closes too.
RECOMP_PATCH void hudDrawCamera(HUDState* hud) {
    s32 x;

    if (hud->cameraState & HUD_CAM_RENDER_RETICLE) {
        // clang-format off
        uvSprtProps(9,
            SPRT_PROP_POS(95, 175),
            SPRT_PROP_COLOR(0xFF, 0x00, 0x00, 0x78),
            SPRT_PROP_END
        );
        // clang-format on
        uvSprtDraw(9);
        if (D_8034F914 == 0) {
            sndPlaySfx(0x43);
            D_8034F914 = 1;
        }
    } else if (hud->cameraState & HUD_CAM_RENDER_SHUTTER) {
        hud->cameraState = CAMERA_SHUTTER_FRAMES; // state now means # frames to render shutter
    } else if (hud->cameraState != 0) {
        x = (CAMERA_SHUTTER_FRAMES - hud->cameraState) * (240 / CAMERA_SHUTTER_FRAMES);
        uvGfxBindTexture(GFX_STATE_TEXTURE_NONE);

        alignBegin(ALIGN_LEFT);
        uvVtxRect(-SCREEN_WIDTH / 2, SCREEN_HEIGHT - 1, x, 0);
        alignEnd();

        alignBegin(ALIGN_RIGHT);
        uvVtxRect(SCREEN_WIDTH - 1 - x, SCREEN_HEIGHT - 1, SCREEN_WIDTH + SCREEN_WIDTH / 2, 0);
        alignEnd();

        hud->cameraState--;
        D_8034F914 = 0;
    }
}

// The HUD's fade to or from a colour, over the whole widened frame. The
// decompilation's hudDrawBox covers the inset viewport the border used to frame.
RECOMP_PATCH void hudDrawBox(HUDState* hud) {
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    f32 tmp;
    f32 alphaF;
    s32 invAlpha;

    switch (hud->unkC60) {
    case 1:
        invAlpha = 1, r = g = b = 0xFF;
        break;
    case 2:
        invAlpha = 0, r = g = b = 0xFF;
        break;
    case 3:
        invAlpha = 1, r = g = b = 0x00;
        break;
    case 4:
        invAlpha = 0, r = g = b = 0x00;
        break;
    default:
        return;
    }

    if (hud->unkC64 == 0.0f) {
        alphaF = 1.0f;
    } else {
        tmp = (sHudState.unk14 - hud->unkC68) / hud->unkC64;
        alphaF = tmp * tmp * tmp;
        if (alphaF < 0.0f) {
            alphaF = 0.0f;
        } else if (alphaF > 1.0f) {
            alphaF = 1.0f;
        }
    }
    if (invAlpha) {
        a = (u8)(255.0f * alphaF);
    } else {
        a = (u8)((1.0f - alphaF) * 255.0f);
    }
    uvGfxStatePush();
    uvGfxSetFlags(GFX_STATE_XLU | GFX_STATE_AA);
    uvGfxClearFlags(GFX_STATE_ZBUFFER);
    uvGfxBindTexture(GFX_STATE_TEXTURE_NONE);
    fullFrameQuad(r, g, b, a);
    uvGfxStatePop();
}
