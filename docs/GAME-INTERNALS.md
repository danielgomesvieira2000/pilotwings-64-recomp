# Pilotwings 64 internals

What the game turned out to be, for anyone working on this port, on the
decompilation, or on another port of a Paradigm "UltraVision" game. Function and
variable names are the [decompilation's](https://github.com/gcsmith/Pilotwings64Decomp)
at the pinned revision, including its placeholder `func_XXXXXXXX`/`D_XXXXXXXX`
names, which are also addresses. How the port deals with each fact is in
[PORTING.md](PORTING.md); how it was found is in [findings/](findings).

## Identity

| | |
|---|---|
| Title | Pilotwings 64 (USA), game code `NPWE`, revision 0 |
| Internal name | `Pilot Wings64` |
| SHA-1 | `ec771aedf54ee1b214c25404fb4ec51cfd43191a` |
| XXH3-64 | `0x887AB02583C90111` (what librecomp checks) |
| Header CRC | `0xC851961C 0x78FCAAFA` |
| Entry point | `0x80200050` |
| Save type | EEPROM 4 Kbit (512 bytes) |
| Graphics microcode | Fast3D |
| Audio microcode | `aspMain`, ROM `0x48E10`, `0xE20` bytes, loaded at `0x04001080` |

This is the only dump the decompilation matches, and so the only one the port
accepts.

## Memory layout

| Region | Address | Notes |
|---|---|---|
| Boot | ROM `0x1000` → `0x80200050` | `entrypoint`, 0x50 bytes |
| `kernel` segment | ROM `0x1050` → `0x802000A0` | Paradigm's UltraVision kernel and libultra; followed in RAM by `0x79A80` bytes of bss |
| `app` segment | ROM `0x51E30` → `0x802CA900` | the game; **copied into place by the boot code itself** (`_uvMediaCopy(app_TEXT_START, app_ROM_START, ...)`) |
| Framebuffers | `0x800DA800`, `0x80100000` | `gGfxFbPtrs`; `gGfxFbIndex` alternates |
| Depth buffer | `0x803DA800` | `D_80299278`; also used as a scratch colour image by `uvGfxStateDrawDL` |
| Heap | up to `osMemSize` | `uvLevelInit` sets its end to `osMemSize + 0x80000000` |

**No overlays and no compressed code.** Both segments are contiguous and loaded
once. Everything a level needs (models, textures, terrain, paths, text) is
loaded from the cartridge filesystem into the heap by `uvLevelInit` and
`uvLevelAppend`.

Because the heap grows to `osMemSize`, **nothing above the game's image is
free** in the way the top of an 8 MB RDRAM is for many games.

Three instructions touch hardware the runtime does not emulate:
`_uvScDlistRecover` writes `SP_STATUS_REG` (`0x8022B678`), and `func_80231A10`
reads and reloads CP0 Count (`0x80231A10`, `0x80231A18`; the function has no
caller). `bootproc` reads 64 bytes of cartridge address `0xFFB000` through
`osPiReadIo`, looking for a development cartridge's marker.

## The main loop and the game states

`gameUpdate` calls one function per state from `sGameStateUpdateFuncs` and
stores what it returns as the next state. Most state functions contain their
own loop of frames (`uvGfxBegin`, draw, `uvGfxEnd`, update) and only return when
the screen is left.

The game's block is `D_80362698` (`Unk80362690`): `s32 state` at +0, `u16 map`
at +4, and from +0xC the player's record, starting with `u16 pilot` and
`u16 veh` (both 16-bit halves of a word: in the port's host-order RDRAM a u16 is
at its address XOR 2).

| # | State | # | State |
|---|---|---|---|
| 0 | `TITLE` | 8 | `DEMO_PILOT` (attract) |
| 1 | `STATE_1` | 9 | `DEMO_TEST_SETUP` |
| 2 | `TEST_DETAILS` (briefing) | 10 | `FILE_MENU` |
| 3 | `PILOT_SELECT` | 11 | `VEHICLE_CLASS_SELECT` |
| 4 | `TEST_SETUP` | 12 | `TEST_OVERVIEW` |
| 5 | `TEST_UPDATE` (flying) | 13 | `RESULTS_CB` |
| 6 | `RESULTS` | 14 | `CONGRATULATIONS` |
| 7 | `OPTIONS` | 15 | `CREDITS` |

Vehicles (`VehicleId`): 0 Hang Glider, 1 Rocket Belt, 2 Gyrocopter, 3 Cannonball,
4 Sky Diving, 5 Jumble Hopper, 6 Birdman. Classes: Beginner, A, B, Pilot.

### The vehicle select grid

`func_8030EA54` drives it. The main grid is three vehicle columns
(`D_8034F7A0`) by four class rows (`D_8034F7A4`); moving right past the
Gyrocopter into the Extra Games column switches to the bonus grid
(`D_8034F7B0`), whose rows are Cannonball, Sky Diving and Jumble Hopper, with
Birdman on a row of its own (`D_8034F7B8`). A stick push counts once until the
stick returns under 0.75. What is unlocked comes from the save
(`func_8030D9C8`); `D_8034F7BC` unlocks everything and is cleared whenever the
screen is entered.

## Timing

- **Variable timestep.** `uvGfxEnd` measures each frame (`gGfxFrameTime`, clock
  `UV_CLKID_GFX`), and animation, effects, menus and physics advance by
  `uvGfxGetFrameTime()`.
- The scheduler has one field, so the game presents at most once per retrace:
  60 frames per second. On the cartridge it ran at 20 to 30; in the port it runs
  at 60 in flight and in the menus.
- `uvClkGetSec(id)` reads CP0 Count through `osGetCount` (`uvClkUpdate`) against
  a per-clock start set by `uvClkReset`. Clock ids: 3 graphics, 4 app, 5 texture
  loading, 6 scheduler, 7 music.

### Busy waits

The game waits by spinning on a clock, without an OS call, in a few places:

| Where | Waits for |
|---|---|
| `uvaSeqStop` (kernel/audio_seq.c) | the sequence player to report stopped, or 2 s |
| `func_803434E8` (app/title_screen.c) | 1 s |
| app/snap.c, the photo album | 0.1 s per photograph |

`uvaSeqStop` is reached up to three times per music change
(`sndSetMusicState` → `uvaSeqStop`, `uvaSeqNew`, `uvaSeqPlay`), which happens on
entering most screens. On the N64 the audio thread preempts the loop at the next
retrace and the stop completes within a frame; code that cannot be preempted
there waits the full two seconds each time.

## Rendering

### Channels, the inset viewport and the border

The world is drawn through up to two *channels* (`D_80261730[2]`,
`UnkStruct_80204D94`), each a camera with a viewport, a frustum and draw
callbacks. `uvChan_80204FE4` draws one: projection, look transform,
environment (sky dome, sea), terrain, dynamic objects, effects, then the
channel's 2D callbacks.

- **Viewport.** Every camera's viewport is the inset
  `SUBSCREEN_X0..X1, Y0..Y1` = (10, 18)-(310, 232), in y-up coordinates, set
  through `uvChan_80204D94` (`cameraSetViewport`, or the channel init). The
  photo album is the exception, with small viewports per photograph.
- **Border.** The region outside the inset is painted black in 2D with four
  `uvVtxRect` calls: `drawScreenBorder` (app/code_D2B10.c), called from the
  shared 2D callback `func_8034B6F8`, and **the same four rectangles inline in
  `func_8030F448`** (vehicle select) **and in options.c**.
- **Frustum.** Every 3D frustum has the inset's aspect, 300:214:
  `(-0.7009346, 0.7009346, -0.5, 0.5)` for flight and the title,
  `(-0.4906542, 0.4906542, -0.35, 0.35)` for the menus' 3D. `uvChan_80204C94`
  stores the extents (`unk1E8`..`unk1F4`) and builds the matrices, and
  `func_802061A0` builds the culling planes from the stored extents.
  `uvChan_80204FE4` feeds the stored extents back into `uvChan_80204C94` for its
  fog pass.
- **Clip ratio.** The environment is drawn under `FRUSTRATIO_1` and the
  terrain and objects under `FRUSTRATIO_2`.
- **Clear.** `uvGfxClearScreen` is a fill rectangle over the current viewport.

### Matrices

- **The camera is baked in on the CPU.** `uvGfx_802236CC` multiplies the look
  transform (`gGfxLookTransform`) into an object's matrix and loads the result as
  the model-view; the projection loaded by `uvChan_80204FE4` is the frustum alone.
  So every transform changes when the camera moves.
- Matrices live in a double-buffered stack (`gGfxMstack[gGfxFbIndex]`,
  `uvGfxMstackPushF`/`PushL`), one per framebuffer.
- The camera's own matrix is the channel's `unk110`; its row 1 is the forward
  direction and row 3 the position.
- Dynamic objects are `D_80263780[100]`, drawn by `_uvDobjsDraw` directly or,
  when they need sorting, through the channel's sorted list in
  `uvChan_80205CE4` (`case 2/3`). Near objects get their own projection in
  `uvDobj_8021771C`. An object's parts are multiplied onto its root inside
  `uvDobj_80217B4C`.
- Static objects (including the terrain's pieces) are drawn by the uvSobj
  functions from `uvChan_80205CE4` (`case 4`); the environment's models by
  `_uvEnvDraw`.
- Effects are `D_8028B400[120]` (`UnkFxStruct`), drawn by `_uvFxDraw` from
  `uvChan_80205CE4` (`case 1`): billboards, trails and debris whose pieces come
  and go.

### 2D

- `uvGfxMtxProjPushF` loads every orthographic projection: the HUD's
  (`hudMainRender`, -0.5..319.5), `func_80314154`'s for menus (0..319), and so on.
- **Rectangles.** Sprites and text go through libultra's sprite library
  (`uvSprtDraw`, `spDraw`), which clips in software to 0-320. `uvVtxRect` emits
  a Fast3D texture rectangle in y-up coordinates, with `G_RDPHALF_1` and
  `G_RDPHALF_2` redefined one lower than `gbi.h` (geometry.c).
- **Polygons.** `uvVtxBeginPoly`/`uvVtx`/`uvVtxEndPoly` build fans from the
  vertex buffer `gGeomVertexPtrs`. The menus and results dim the screen with one
  translucent black quad at exactly (0, 0)-(320, 240).
- **Text.** `uvFontPrintStr`/`uvFontPrintStr16` queue messages;
  `uvFontGenDlist` turns the queue into sprite display lists once per frame. Its
  display-list cursor is reset once per frame and advanced by the message count,
  so calling it twice in a frame overwrites the first call's lists.
- **HUD.** `hudMainRender` calls one function per vehicle --
  `hudDrawHangGlider`, `hudDrawRocketPack`, `hudDrawGyrocopter`,
  `hudDrawCannonball`, `hudDrawSkyDiving`, `hudDrawJumbleHopper`,
  `hudDrawBirdman` -- built from shared elements (`hudDrawSpeed`,
  `hudDrawTimer`, `hudSeaLevel`, `hudDrawAltimeter`, `hudDrawRadar`, ...). The
  radar and throttle load model-view matrices of their own. `hudDrawCamera`
  draws the photo shutter; `hudDrawBox` a full-screen box.

## Audio

- The audio microcode is **byte-for-byte Wave Race 64's**, including the
  sixteen-entry command table, and loads at `0x1080`.
- The game mixes at 22,050 Hz.
- Music is a compressed-MIDI sequence player (`gSeqPlayer`, `alCSeq`) driven
  by `sndSetMusic`/`sndSetMusicState`; `__CSPVoiceHandler` is a `static`
  function in libultra's csplayer.c reached only through a pointer stored in the
  player.

## Saves

EEPROM, read once at boot by `saveModuleInit` and written through
`uvFileWrite` (`osEepromLongWrite`, offsets in 8-byte blocks, at most `0x208`
bytes).

| Offset | Contents |
|---|---|
| `0x000` | file 1: `PW` (or `pw` when newly initialised), then scores bit-packed 7 bits per test (`saveBitScramble`), then 0x408 bits of flags; byte `0xFF` is the low byte of the sum of bytes `0x00`-`0xFE` |
| `0x100` | file 2, the same layout |

`saveFileWrite` compares a file with its mirror and writes only when it changed.
