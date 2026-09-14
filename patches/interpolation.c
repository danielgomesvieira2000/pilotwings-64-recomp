// High frame rate: tagging the game's matrices so RT64 interpolates them right.
//
// RT64 draws the frames between two game frames by pairing each transform with
// the previous frame's and interpolating between them. Left to itself it pairs
// by draw-call signature and nearest position, which is mostly right and
// visibly wrong where several similar objects are close together or appear and
// disappear. The extended GBI lets a game say what each transform is instead: a
// *matrix group* with an id, and with linear ordering RT64 pairs transforms by
// id and by their order within it, and does not interpolate at all where an id
// has no counterpart in the previous frame.
//
// Two facts about this engine shape how that is done:
//
// 1. The camera is baked into every object's matrix on the CPU.
//    uvGfx_802236CC multiplies the look transform into each object's matrix
//    before loading it, the projection is the frustum alone, and the terrain is
//    made of static objects like any other. So a camera cut moves every
//    transform at once, and a cut has to change *every* id or RT64 would sweep
//    the whole picture from the old view to the new one. Every id carries a
//    camera generation, which interpCameraBegin advances when the camera jumps.
//
// 2. An object's parts are matrices multiplied onto its root inside one call,
//    uvDobj_80217B4C for dynamic objects and the uvSobj functions for static
//    ones. One group around the whole call covers all of them: RT64 pairs the
//    transforms of an id in drawing order. The id includes the model and the LOD
//    as well as the object, so a change of either -- a respawn, a detail switch
//    with a different part count -- is not interpolated across.
//
// What is tagged, by where it is drawn:
//
//   channel projection     uvChan_80204FE4 (patches/widescreen.c)  camera id
//   dynamic objects        _uvDobjsDraw, uvChan_80205CE4           object slot
//   near objects           uvDobj_8021771C                         object slot
//   static objects         uvChan_80205CE4                         struct address
//   environment models     _uvEnvDraw                              model index
//   effects                uvChan_80205CE4                         effect slot, auto order
//   everything 2D          uvGfxMtxProjPushF                       not interpolated
//
// The game's frame rate in the port is usually already the display's 60, where
// none of this is visible; it is what makes 120 and 144 Hz displays, and slower
// machines, smooth. PW64_GAME_RATE (src/patch_host.cpp) slows the game down to
// check it on a 60 Hz display.

#include "interpolation.h"

#include "uv_chan.h"
#include "uv_dobj.h"
#include "uv_environment.h"
#include "uv_fx.h"
#include "uv_model.h"
#include "uv_sobj.h"
#include "kernel/code_7150.h"

extern UnkStruct_80204D94 D_80261730[2];
extern s32 gGfxFrameCount;

// ---- ids ------------------------------------------------------------------

static u32 sGeneration = 1;

static u32 mix(u32 h, u32 v) {
    h ^= v;
    h *= 0x01000193u;
    h ^= h >> 15;
    return h;
}

u32 interpId(InterpKind kind, u32 a, u32 b, u32 c) {
    u32 h = 0x811C9DC5u;
    h = mix(h, (u32)kind);
    h = mix(h, a);
    h = mix(h, b);
    h = mix(h, c);
    h = mix(h, sGeneration);
    if (h == G_EX_ID_IGNORE || h == G_EX_ID_AUTO) {
        h = 0x5057u;
    }
    return h;
}

// Tagging can be switched off (PW64_NO_INTERP_TAGS) to measure RT64 without it.
static s32 tagsOn(void) {
    return pw64_interp_tags_enabled();
}

// ---- camera ---------------------------------------------------------------

// A camera further than this from where it was a game frame ago, or turned by
// more than about 40 degrees, has cut rather than moved. The fastest vehicles
// cover a few units a frame even at 20 frames per second.
#define CUT_DISTANCE 60.0f
#define CUT_MIN_FORWARD_DOT 0.75f

static struct {
    s32 frame;
    s32 valid;
    Mtx4F camera;
} sCameraHistory[2];

void interpCameraBegin(s32 channel, UnkStruct_80204D94* chan) {
    Mtx4F* cur = &chan->unk110;

    if (sCameraHistory[channel].frame != gGfxFrameCount) {
        if (sCameraHistory[channel].valid) {
            Mtx4F* prev = &sCameraHistory[channel].camera;
            f32 dx = cur->m[3][0] - prev->m[3][0];
            f32 dy = cur->m[3][1] - prev->m[3][1];
            f32 dz = cur->m[3][2] - prev->m[3][2];
            f32 forwardDot = cur->m[1][0] * prev->m[1][0] + cur->m[1][1] * prev->m[1][1] + cur->m[1][2] * prev->m[1][2];
            if ((dx * dx + dy * dy + dz * dz) > (CUT_DISTANCE * CUT_DISTANCE) || forwardDot < CUT_MIN_FORWARD_DOT) {
                sGeneration++;
                pw64_debug(1, (s32)((dx * dx + dy * dy + dz * dz)), (s32)(forwardDot * 1000.0f), (s32)sGeneration);
            }
        }
        // A frame with no camera drawn at all (a menu, a load) also breaks the
        // chain: the next camera starts fresh.
        if (sCameraHistory[channel].frame != gGfxFrameCount - 1) {
            sGeneration++;
            pw64_debug(2, sCameraHistory[channel].frame, gGfxFrameCount, (s32)sGeneration);
        }
        uvMat4Copy(&sCameraHistory[channel].camera, cur);
        sCameraHistory[channel].valid = TRUE;
        sCameraHistory[channel].frame = gGfxFrameCount;
    }

    if (!tagsOn()) {
        return;
    }
    gEXMatrixGroupSimple(gGfxDisplayListHead++, interpId(INTERP_KIND_CAMERA, channel, 0, 0), G_EX_PUSH, G_MTX_PROJECTION,
                         G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                         G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                         G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
}

void interpCameraEnd(void) {
    if (!tagsOn()) {
        return;
    }
    gEXPopMatrixGroup(gGfxDisplayListHead++, G_MTX_PROJECTION);
}

// ---- groups ---------------------------------------------------------------

void interpModelBegin(u32 id) {
    if (!tagsOn()) {
        return;
    }
    gEXMatrixGroupDecomposed(gGfxDisplayListHead++, id, G_EX_PUSH, G_MTX_MODELVIEW,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
}

void interpModelBeginAutoOrder(u32 id) {
    if (!tagsOn()) {
        return;
    }
    gEXMatrixGroupDecomposed(gGfxDisplayListHead++, id, G_EX_PUSH, G_MTX_MODELVIEW,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_ORDER_AUTO, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
}

void interpModelBeginIgnore(void) {
    if (!tagsOn()) {
        return;
    }
    gEXMatrixGroupNoInterpolate(gGfxDisplayListHead++, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
}

void interpModelEnd(void) {
    if (!tagsOn()) {
        return;
    }
    gEXPopMatrixGroup(gGfxDisplayListHead++, G_MTX_MODELVIEW);
}

void interpProjectionBegin(u32 id) {
    if (!tagsOn()) {
        return;
    }
    gEXMatrixGroupSimple(gGfxDisplayListHead++, id, G_EX_PUSH, G_MTX_PROJECTION,
                         G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                         G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
                         G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP);
}

void interpProjectionEnd(void) {
    if (!tagsOn()) {
        return;
    }
    gEXPopMatrixGroup(gGfxDisplayListHead++, G_MTX_PROJECTION);
}

static u32 dobjId(Unk80263780* obj, s32 lodId) {
    return interpId(INTERP_KIND_DOBJ, (u32)(obj - D_80263780), obj->modelId, (u32)lodId);
}

// ---- 2D -------------------------------------------------------------------

// The decompilation's uvGfxMtxProjPushF (src/kernel/graphics.c). The game loads
// every orthographic projection -- the HUD, the menus, text, fades -- through
// it and nothing else, so it is where 2D is marked as never interpolated. The
// groups are set in place rather than pushed, because nothing pops them: they
// stay the default for whatever else this display list draws untagged, and RT64
// resets them at the end of the list.
RECOMP_PATCH void uvGfxMtxProjPushF(Mtx4F* arg0) {
    Mtx sp48;

    if (tagsOn()) {
        gEXMatrixGroupNoInterpolate(gGfxDisplayListHead++, G_EX_NOPUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE);
        gEXMatrixGroupNoInterpolate(gGfxDisplayListHead++, G_EX_NOPUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    }
    uvMat4CopyF2L(&sp48, arg0);
    uvGfxMtxProj(sp48);
}

// ---- dynamic objects ------------------------------------------------------

extern Unk80263780 D_80263780[100];
extern Mtx4F D_80265080[300];
extern u16 D_80269CB0[100];
extern u16 D_80269F08;
extern u16 D_80269F0A;

#undef gSPPerspNormalize
#define gSPPerspNormalize(pkt, s)                   \
    {                                               \
        Gfx* _g = (Gfx*)(pkt);                      \
                                                    \
        _g->words.w0 = _SHIFTL(G_RDPHALF_1, 24, 8); \
        _g->words.w1 = (s);                         \
    }

// The decompilation's _uvDobjsDraw (src/kernel/dobj.c), with the objects it
// draws directly in a group each. The ones it sorts are drawn, and tagged, in
// uvChan_80205CE4.
RECOMP_PATCH void _uvDobjsDraw(UnkStruct_80204D94* arg0, s32 arg1) {
    Unk80263780* var_s2;
    s32 i;
    f32 temp_fv0_2;
    f32 temp_fv1;
    s32 lodId;
    s32 var_a0;
    u8 spBF;
    f32 temp_fv0;
    f32 temp_fs5;
    f32 temp_fs1;
    f32 temp_fs2;
    f32 temp_fs0;
    f32 spA0;
    f32 sp9C;
    f32 sp98;
    ParsedUVMD* uvmd;
    Mtx4F* temp_v0_2;

    D_80269F08 = 0;
    spA0 = arg0->unk110.m[3][0];
    sp9C = arg0->unk110.m[3][1];
    sp98 = arg0->unk110.m[3][2];

    for (i = 0; i < 100; i++) {
        var_s2 = &D_80263780[i];
        if (var_s2->modelId == 0xFFFF) {
            continue;
        }
        if (!(var_s2->unk34 & 2) || (var_s2->unk34 & 4)) {
            continue;
        }
        uvmd = gGfxUnkPtrs->models[var_s2->modelId];
        if (uvmd == NULL) {
            _uvDebugPrintf("_uvDobjsDraw: model %d not in level\n", var_s2->modelId);
            continue;
        }
        uvModelGetProps(var_s2->modelId, MODEL_PROP_UNK5(&spBF), MODEL_PROP_END);
        if (spBF - arg1 != 0) {
            continue;
        }
        temp_v0_2 = &D_80265080[var_s2->unk2[0]];
        temp_fv0 = temp_v0_2->m[3][0];
        temp_fs5 = temp_v0_2->m[3][1];

        temp_fs1 = temp_v0_2->m[3][0] - spA0;
        temp_fs2 = temp_v0_2->m[3][1] - sp9C;
        temp_fs0 = temp_v0_2->m[3][2] - sp98;
        temp_fv0_2 = uvSqrtF(SQ(temp_fs1) + SQ(temp_fs2) + SQ(temp_fs0));
        temp_fv1 = var_s2->unk38;
        if ((temp_fv0_2 < (arg0->unk1FC + temp_fv1)) && (func_80206F64(arg0->unk2E0, temp_fs1, temp_fs2, temp_fs0, temp_fv1) != 0)) {
            if (arg0->unk204 == 1.0f) {
                lodId = uvDobjGetLODIndex(uvmd, temp_fv0_2);
            } else {
                lodId = uvDobjGetLODIndex(uvmd, arg0->unk204 * temp_fv0_2);
            }
            if (lodId == 0xFF) {
                continue;
            }
            if (arg1 != 0) {
                interpModelBegin(dobjId(var_s2, lodId));
                uvDobj_80217B4C(var_s2, uvmd, lodId);
                interpModelEnd();
            } else if (uvmd->lodTable[lodId].billboard != FALSE) {
                if (uvmd->attrs & UVMD_ATTR_TRANSPARENT) {
                    var_a0 = 3;
                } else {
                    var_a0 = -3;
                }
                _uvSortAdd(var_a0, temp_fv0_2, var_s2, arg0, temp_fv0, temp_fs5, lodId, temp_fs1, temp_fs2);
            } else {
                if (uvmd->attrs & UVMD_ATTR_TRANSPARENT) {
                    var_a0 = 2;
                } else {
                    var_a0 = -2;
                }
                _uvSortAdd(var_a0, temp_fv0_2, var_s2, arg0, temp_fv0, temp_fs5, lodId);
            }
        }
    }
}

// The decompilation's uvDobj_8021771C (src/kernel/dobj.c): objects drawn with a
// projection of their own, fitted to their distance so they are not clipped by
// the near plane. Each projection gets the object's own group, and so does the
// object.
RECOMP_PATCH void uvDobj_8021771C(UnkStruct_80204D94* arg0) {
    ParsedUVMD* uvmd;
    Unk80263780* temp_s1;
    s32 i;
    Mtx4F sp128;
    Mtx spE8;
    f32 temp_fs2;
    f32 temp_fs3;
    f32 temp_fs4;
    f32 temp_fv0;
    f32 temp_fv0_2;
    f32 temp_fa0;
    f32 var_fs0;
    f32 temp_fv1_x;
    f32 temp_fa0_x;
    f32 temp_fa1_x;
    f32 temp_ft4_x;
    u8 lodId;

    if (D_80269F0A == 0) {
        return;
    }
    uvGfx_80222A98();
    uvGfxEnableZBuffer(TRUE);
    for (i = 0; i < D_80269F0A; i++) {
        temp_s1 = &D_80263780[D_80269CB0[i]];
        if (temp_s1->modelId == 0xFFFF) {
            continue;
        }
        if (!(temp_s1->unk34 & 2)) {
            continue;
        }
        uvmd = gGfxUnkPtrs->models[temp_s1->modelId];

        temp_fs3 = D_80265080[temp_s1->unk2[0]].m[3][0] - arg0->unk110.m[3][0];
        temp_fs4 = D_80265080[temp_s1->unk2[0]].m[3][1] - arg0->unk110.m[3][1];
        temp_fv0 = D_80265080[temp_s1->unk2[0]].m[3][2] - arg0->unk110.m[3][2];
        temp_fv0_2 = uvSqrtF(SQ(temp_fs3) + SQ(temp_fs4) + SQ(temp_fv0));

        temp_fa0 = temp_fv0_2 - temp_s1->unk38;
        temp_fs2 = temp_s1->unk38 + temp_fv0_2;
        if (temp_fa0 > 0.1f) {
            var_fs0 = temp_fa0;
        } else {
            var_fs0 = 0.1f;
        }
        temp_fv1_x = arg0->unk1E8 * (var_fs0 / arg0->unk1F8);
        temp_fa0_x = arg0->unk1EC * (var_fs0 / arg0->unk1F8);
        temp_ft4_x = arg0->unk1F4 * (var_fs0 / arg0->unk1F8);
        temp_fa1_x = arg0->unk1F0 * (var_fs0 / arg0->unk1F8);
        uvMat4SetFrustrum(&sp128, temp_fv1_x, temp_fa0_x, temp_fa1_x, temp_ft4_x, var_fs0, temp_fs2);
        uvMat4CopyF2L(&spE8, &sp128);
        interpProjectionBegin(interpId(INTERP_KIND_NEAR_PROJECTION, (u32)(temp_s1 - D_80263780), 0, 0));
        uvGfxMtxProj(spE8);
        gSPPerspNormalize(gGfxDisplayListHead++, (s16)(131072.0f / (temp_fs2 + var_fs0)));

        if (arg0->unk204 == 1.0f) {
            lodId = uvDobjGetLODIndex(uvmd, temp_fv0_2);
        } else {
            lodId = uvDobjGetLODIndex(uvmd, arg0->unk204 * temp_fv0_2);
        }

        if (lodId == 0xFF) {
            interpProjectionEnd();
            continue;
        }

        interpModelBegin(dobjId(temp_s1, lodId));
        if (uvmd->lodTable[lodId].billboard != FALSE) {
            uvDobj_80217E24(temp_s1, uvmd, lodId, temp_fs3, temp_fs4);
        } else {
            uvDobj_80217B4C(temp_s1, uvmd, lodId);
        }
        interpModelEnd();
        gSPPopMatrix(gGfxDisplayListHead++, G_MTX_PROJECTION);
        interpProjectionEnd();
    }
}

// ---- sorted draws ---------------------------------------------------------

extern UnkSortAdd D_80261ED8[100];
extern UnkSortAdd D_802629C8[60];
extern u8 D_80261E70[100];
extern s32 D_80263058;
extern s32 D_8026305C;
extern u8 D_80263060[];
extern UnkFxStruct D_8028B400[120];

// The decompilation's uvChan_80205CE4 (src/kernel/chan.c), which draws the
// sorted lists: dynamic objects, billboards, effects and the static objects the
// terrain is made of. Each gets its group.
RECOMP_PATCH void uvChan_80205CE4(UnkStruct_80204D94* arg0, s32 arg1, f32 arg2, f32 arg3) {
    ParsedUVMD* uvmd;
    UnkSortAdd* var_s1;
    s32 i;
    s32 var_s6;

    if (arg1 != 0) {
        var_s6 = D_80263058;
    } else {
        var_s6 = D_8026305C;
    }

    for (i = 0; i < var_s6; i++) {
        if (arg1 != 0) {
            var_s1 = &D_80261ED8[D_80261E70[i]];
        } else {
            var_s1 = &D_802629C8[i];
        }

        if (var_s1->unk14 == 0xFFFF) {
            if ((var_s1->unk4 < arg2)) {
                continue;
            }
            if (arg3 <= var_s1->unk4) {
                continue;
            }
        } else if ((arg2 != -1.0f)) {
            if (((arg2 == 0.0f) && (D_80263060[var_s1->unk14] == 1))) {
                continue;
            }
            if (((arg2 != 0.0f) && (D_80263060[var_s1->unk14] == 0))) {
                continue;
            }
        }

        switch (var_s1->unk0) {
        case 2:
            uvmd = gGfxUnkPtrs->models[((Unk80263780*)(var_s1->unk10))->modelId];
            interpModelBegin(dobjId((Unk80263780*)var_s1->unk10, var_s1->unk1));
            uvDobj_80217B4C((Unk80263780*)var_s1->unk10, uvmd, var_s1->unk1);
            interpModelEnd();
            break;
        case 3:
            interpModelBegin(dobjId((Unk80263780*)var_s1->unk10, var_s1->unk1));
            uvDobj_80217E24((Unk80263780*)var_s1->unk10, gGfxUnkPtrs->models[((Unk80263780*)(var_s1->unk10))->modelId], var_s1->unk1, var_s1->unk8,
                            var_s1->unkC);
            interpModelEnd();
            break;
        case 1:
            // Effects are in the world like anything else, so with the camera
            // baked in they have to be interpolated or they shake against the
            // scenery. Their pieces come and go, so RT64 orders them itself.
            interpModelBeginAutoOrder(interpId(INTERP_KIND_FX, (u32)((UnkFxStruct*)var_s1->unk10 - D_8028B400),
                                               ((UnkFxStruct*)var_s1->unk10)->type,
                                               ((UnkFxStruct*)var_s1->unk10)->textureId));
            uvGfxStatePush();
            uvGfxSetFlags(GFX_STATE_XLU | GFX_STATE_AA | GFX_STATE_ZBUFFER | GFX_STATE_GOURAUD | GFX_STATE_TEXTURE_NONE);
            uvGfxClearFlags(GFX_STATE_FOG | GFX_STATE_10000000 | GFX_STATE_LIGHTING | GFX_STATE_DECAL | GFX_STATE_CULL_BACK | GFX_STATE_40000);
            _uvFxDraw((UnkFxStruct*)var_s1->unk10 - D_8028B400, arg0);
            uvGfxStatePop();
            interpModelEnd();
            break;
        case 4:
            uvmd = gGfxUnkPtrs->models[((UnkSobjDraw*)(var_s1->unk10))->modelId];
            interpModelBegin(interpId(INTERP_KIND_SOBJ, (u32)var_s1->unk10, ((UnkSobjDraw*)(var_s1->unk10))->modelId, var_s1->unk1));
            uvGfx_802236CC(var_s1->unk18);
            if (uvmd->lodTable[var_s1->unk1].billboard != FALSE) {
                uvSobj_8022CC28((UnkSobjDraw*)var_s1->unk10, uvmd, var_s1->unk1, var_s1->unk8, var_s1->unkC);
            } else {
                uvSobj_8022C8D0((UnkSobjDraw*)var_s1->unk10, uvmd, var_s1->unk1, var_s1->unk18);
            }
            uvGfxMtxViewPop();
            interpModelEnd();
            break;
        }
    }
}

// ---- environment ----------------------------------------------------------

extern Mtx D_80269F10;
extern Mtx4F D_80248DE0;

// The decompilation's _uvEnvDraw (src/kernel/env.c): the sky and the sea around
// an island, drawn around the camera. Each model gets its group.
RECOMP_PATCH void _uvEnvDraw(s32 arg0, s32 arg1) {
    uvModelLOD* currLod;
    f32 fogfact;
    s32 temp_a0_2;
    u8 modelFlag;
    uvModelPart* currPart;
    ParsedUVEN* uven;
    u32 i;
    u32 j;
    ParsedUVMD* uvmd;
    UnkStruct_80204D94* var_v0;

    if (arg1 == 0xFFFF) {
        return;
    }

    uven = gGfxUnkPtrs->environments[arg1];
    if (uven == NULL) {
        uvGfxClearScreen(0x00, 0x00, 0x00, 0xFF);
        return;
    }
    if (uven->fogEnabled != FALSE) {
        fogfact = uven->fogMin / uven->fogMax;
    } else {
        fogfact = 0.0f;
    }
    uvGfxSetFogFactor(fogfact);

    if (uven->clearEnabled != FALSE) {
        uvGfxClearScreen(uven->screenR, uven->screenG, uven->screenB, uven->screenA);
    }

    var_v0 = &D_80261730[arg0];
    for (i = 0; i < uven->modelCount; i++) {
        uvmd = gGfxUnkPtrs->models[uven->modelTable[i].modelId];
        if (uvmd == NULL) {
            _uvDebugPrintf("_uvEnvDraw: model %d not in level\n", uven->modelTable[i].modelId);
            return;
        }
        modelFlag = uven->modelTable[i].flag;
        currLod = uvmd->lodTable;
        currPart = currLod->partTable;

        if (modelFlag & 8) {
            D_80248DE0.m[3][0] = var_v0->unk110.m[3][0];
            D_80248DE0.m[3][1] = var_v0->unk110.m[3][1];
        } else {
            D_80248DE0.m[3][0] = D_80248DE0.m[3][1] = 0.0f;
        }

        gDPSetFogColor(gGfxDisplayListHead++, uven->fogR, uven->fogG, uven->fogB, 255);
        if (modelFlag & 4) {
            uvGfxSetFogFactor(fogfact);
        } else {
            uvGfxSetFogFactor(0.0f);
        }

        interpModelBegin(interpId(INTERP_KIND_ENV, (u32)arg0, i, uven->modelTable[i].modelId));
        if (modelFlag & 2) {
            uvGfxMtxProj(var_v0->unkD0);
            uvGfxMtxView(var_v0->unk150);
        }
        uvGfx_802236CC(&D_80248DE0);

        for (j = 0; j < currPart->stateCount; j++) {
            temp_a0_2 = currPart->stateTable[j].state;
            if (!(modelFlag & 1)) {
                currPart->stateTable[j].state &= ~0x200000;
            }
            uvGfxStateDraw(&currPart->stateTable[j]);
            currPart->stateTable[j].state = temp_a0_2;
        }
        if (modelFlag & 2) {
            uvGfxMtxProj(var_v0->unk50);
            uvGfxMtxView(D_80269F10);
        }
        uvGfxMtxViewPop();
        interpModelEnd();
    }

    uvGfxSetFogFactor(fogfact);
    if (uven->callback != NULL) {
        uven->callback();
    }
}
