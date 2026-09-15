# Porting reference

How this port is put together, and why each piece is the way it is: the
pipeline from a dump to an executable, the runtime harness, the C patches, and
the two enhancements. Facts about the game itself are in
[GAME-INTERNALS.md](GAME-INTERNALS.md); the step-by-step build is
[BUILDING.md](BUILDING.md); the record of how each fact was found, wrong turns
included, is [findings/](findings).

The port reuses the harness and frontend of
[Wave Race 64: Recompiled](https://github.com/danielgomesvieira2000/wave-race-64-recomp),
whose PORTING.md covers those shared parts (audio output, the frontend's
wiring, crash handling) in more depth. What is different here starts from one
fact: Pilotwings 64 has a complete, matching, MIT-licensed decompilation.

## Contents

1. [The pipeline](#the-pipeline)
2. [The ELF](#the-elf)
3. [Recompiling](#recompiling)
4. [The harness](#the-harness)
5. [C patches](#c-patches)
6. [Widescreen](#widescreen)
7. [Frame interpolation](#frame-interpolation)
8. [Threads that never yield](#threads-that-never-yield)
9. [Submodule patches](#submodule-patches)
10. [Testing and diagnostics](#testing-and-diagnostics)

## The pipeline

```
dump.z64 ──build_elf.sh──▶ pilotwings64.us.elf            (decompilation, IDO 5.3, RECOMP_BUILD=1)
                                  │
                   recompile.sh ──┼─▶ RecompiledFuncs/     (N64Recomp, ELF mode, mdebug)
                                  ├─▶ RecompiledFuncs/context/*.toml  (reference symbols)
                                  └─▶ RecompiledFuncs/aspMain.cpp     (RSPRecomp)
patches/*.c ──build_patches.py──▶ RecompiledPatches/       (Clang MIPS → lld → N64Recomp)
                                  │
                  CMake ──────────┴─▶ Pilotwings64Recomp.exe
```

`tools/generate_game.py` runs the first three steps. Everything to the right of
an arrow is derived from the dump and is never committed.

The Linux steps (the decompilation's compiler, both recompilers, the MIPS Clang)
run under WSL on Windows. Two of those are forced:

- **The Windows build of N64Recomp dies partway through writing its output**
  with `0xC0000409`, a detected stack overflow under Windows' 1 MB default stack,
  leaving `funcs.h` truncated mid-token. Truncated output looks like success
  until it is compiled, so `recompile.sh` checks the end marker.
- **The LLVM that winget installs has no MIPS backend**: `No available targets
  are compatible with triple "mips"`, preceded by `-mno-check-zero-division`
  rejected as an unknown option -- the tell that the target is not registered at
  all. Every Linux distribution's Clang has it.

## The ELF

`tools/build_elf.sh <dump>` builds the decompilation's own matching ROM with
`make RECOMP_BUILD=1` and copies out `build/pilotwings64.us.elf`, refusing it
unless the rebuilt ROM's SHA-1 matches.

- `RECOMP_BUILD=1` removes `static` from the game's file-local functions and data
  (`STATIC_FUNC`, `STATIC_DATA`), so they are in the symbol table. It does not
  touch libultra's statics; those come from `.mdebug` (below).
- **CRLF breaks IDO.** Git for Windows checks the submodule out with CRLF, and
  IDO's `cfe` then fails on `gbi.h` with `Illegal macro parameter name` (the `\r`
  before each line continuation). The script re-checks the decompilation and its
  own submodules out with `core.autocrlf=false` (`git checkout-index --force
  --all`). The usual `git rm --cached -r .` refuses in a repository with
  submodules.
- On WSL the tree is mirrored to `~/.cache/pilotwings64recomp/decomp`
  (`PW64_DECOMP_WORKDIR`), because a build under `/mnt/c` crosses the 9P boundary
  on every file operation. Only generated directories are excluded from the
  mirror: IDO's distribution ships `crt1.o` and friends, and a blanket `*.o`
  exclude fails with `No rule to make target 'build/5.3/out/crt1.o'`.

## Recompiling

`recomp/pilotwings64.us.toml` is the committed config;
`tools/gen_mdebug_mappings.py` writes `pilotwings64.us.full.toml` from it, which
is what N64Recomp reads.

- **ELF input mode.** Every function has its real name, address and size. The
  first recompile produced 3,130 functions with no unresolved call targets and no
  manual boundaries.
- **`use_mdebug = true`.** Without it the first run failed on a lookup miss at
  `0x80235A0C` from `alAudioFrame`: `__CSPVoiceHandler`, a `static` libultra
  function reached only through a pointer, which has no ELF symbol and so was
  never recompiled. IDO's `.mdebug` names every static function.
- **`mdebug_file_mappings`.** `use_mdebug` then failed with `Couldn't determine
  elf section of mdebug info for file src/libultra/os/sendmesg.c`: the parser
  finds a file's section through one of its global functions, and `osSendMesg` is
  reimplemented by the runtime and removed. The generator maps all 265 source
  files explicitly (libultra and kernel to `.kernel`, app to `.app`); the list is
  kept out of the committed config for readability.
- **Instruction patches** replace the three instructions that touch hardware the
  runtime does not emulate (GAME-INTERNALS.md, *Memory layout*). They were found
  by gcsmith's Pilotwings64Recomp and checked against the ELF here.
- **`ignored`** lists every function a C patch replaces (see *C patches*).
- `--dump-context` writes the reference symbols the patches link against, and
  `tools/gen_reimplemented_decls.py` declares the functions the runtime
  reimplements, which the generated code calls without prototypes (an error from
  C99 on).
- **RSPRecomp** (`recomp/aspMain.us.toml`): the audio microcode is Wave Race 64's
  byte for byte, so its text offset, `0x1080` load address and sixteen extra
  indirect branch targets carried over unchanged.

## The harness

`src/` is Wave Race 64: Recompiled's harness, renamed and trimmed of that game's
water, buoys, display-list rewriter and haptics.

| File | Does |
|---|---|
| `main.cpp` | game registration (`Pilot Wings64`, EEPROM 4K, entry point), `--identify`, settings directory, `on_init` |
| `rom.cpp`, `include/pw64/rom.h` | the pinned identity (SHA-1, XXH3, CRC) |
| `sections.cpp` | registers the recompiled sections, and moves any librecomp placed wrongly |
| `register_patches.cpp` | hands the patch binary and symbols to librecomp; registers patched functions at their original addresses |
| `patch_host.cpp` | the C++ side of the patches (host functions) |
| `callbacks.cpp`, `resample.cpp`, `audiodiag.cpp` | audio output with the port's own resampler, input, RSP dispatch, window aspect |
| `libultra_stubs.cpp` | libultra functions nothing else implements |
| `frontend.cpp` | the RecompFrontend launcher, menus and default controls |
| `renderer.cpp` | the RT64 context |
| `crash_handler.cpp` | a report with the recompiled call stack |
| `testdrive.cpp` | scripted input and the game-state transcript |

What this game needed that Wave Race did not:

- **The app segment was registered at the wrong address.** librecomp registers
  the boot megabyte as though ROM `0x1000` onwards were loaded contiguously at
  the entry point. That holds for the kernel and not for the app segment, which
  follows `0x79A80` bytes of bss and is copied into place by the game. Symptom:
  a lookup miss at `0x802EBC94` from `gameUpdate`'s state table.
  `place_resident_sections` moves every such section to its linked address from
  `on_init`.
- **`osPiReadIo` and `osPiRawReadIo` are implemented nowhere**: librecomp
  ignores them and defines neither, so the link fails. Both read the dump.
- **The audio command-list scratch moved.** Wave Race's port copies each audio
  command list to the top of the reported 8 MB. This game's heap grows to
  `osMemSize`, so the copy lives at `0x80F00000`, above the patch data
  (`0x80801000`) and below librecomp's mod region (`0x81000000`).
- **Presentation** is `PresentEarly`, which interpolation needs.
- **Input is single-player**, as in [Rayman 2: Recompiled](https://github.com/danielgomesvieira2000/rayman-2-the-great-escape-recomp).
  `recompinput::players::set_single_player_mode(true)` gives the Controls tab one
  set of keyboard and controller bindings to edit, and `get_n64_input` merges the
  keyboard with every connected pad, so nothing is assigned before either plays.
  The bindings are written to `controls.json` on first run and on exit, because
  the frontend otherwise saves them only when the Controls tab itself is closed.
- **Linux** uses the same sources; `tools/build_linux.sh` runs every step
  natively (the scripts that call WSL on Windows run the Linux tools directly),
  and RT64 renders through Vulkan.

## C patches

A patch is C written against the decompilation's headers, compiled for MIPS,
recompiled by N64Recomp, and linked into the executable. A function marked
`RECOMP_PATCH` replaces the game's function of the same name.

```
patches/*.c ─clang -target mips─▶ *.o ─ld.lld (patches.ld, syms.ld)─▶ patches.elf
    ─N64Recomp (recomp/patches.toml, reference symbols)─▶ RecompiledPatches/patches.c, patches.bin
```

`tools/build_patches.py` does all of it and writes `patched_addresses.inl` and
`patches_bin.c`, which CMake compiles into the executable.

- **Link address.** `patches/patches.ld` places patch code and data at
  `0x80801000`, above the game's 8 MB.
- **Game symbols** are left undefined and resolved by name against the
  reference symbols from `--dump-context` (`strict_patch_mode`).
- **A replaced function must be `ignored` in the game's config.** N64Recomp emits
  both the original and the patch as `RECOMP_FUNC`, which under Clang is a weak
  definition; under clang-cl a weak definition is a COMDAT, and a second
  definition collides with it (`lld-link: error: duplicate symbol`) rather than
  overriding it. So the original is not emitted. `build_patches.py` refuses to
  build when a `RECOMP_PATCH` is missing from the list. `ignored` does not remove
  the name from the reference symbols (`--dump-context` returns before the list
  is applied), so the patch still resolves.
- **Indirect calls to a replaced function.** An ignored function is also absent
  from the section table, so a call through a pointer to its original address
  would miss. Each patch is registered at the original address from `on_init`.
- **Host functions** are C++ functions a patch calls. Each is a zero-sized
  symbol between `0x8F000000` and `0x90000000` in `patches/syms.ld`, which
  N64Recomp turns into a direct call with the recompiled signature
  `void name(uint8_t* rdram, recomp_context* ctx)`; arguments arrive in `a0`-`a3`
  (`ctx->r4`..`r7`), results go in `v0` or `f0`. libultra functions the runtime
  reimplements are reached the same way as `<name>_recomp`, with `#define`s in
  `patches/patches.h`.
- **Transcribing a function.** A patch that replaces a function starts from the
  decompilation's C for it and says so above it. Two things to carry over from
  the original file, because they change the emitted commands: local `#define`
  overrides (chan.c's `gSPPerspNormalize`, geometry.c's `G_RDPHALF_1/2`), and
  `static` data the function shares with its file (declare it `extern`; with
  `RECOMP_BUILD=1` it is exported).

| Patch file | Replaces |
|---|---|
| `framerate.c` | `_uvScDoneGfx` (frame hook) |
| `gfx.c` | `uvGfxBegin` (enables the extended GBI), `uvGfxClearScreen` |
| `clocks.c` | `uvClkGetSec` |
| `widescreen.c` | `uvVtxRect`, `uvVtxEndPoly`, `uvChan_80204D94`, `uvChan_80204C94`, `uvChan_80204FE4` |
| `hud.c` | the seven `hudDraw*` vehicle functions except Cannonball, `hudDrawCamera`, `hudDrawBox`, `uvFontGenDlist` |
| `interpolation.c` | `uvGfxMtxProjPushF`, `_uvDobjsDraw`, `uvDobj_8021771C`, `uvChan_80205CE4`, `_uvEnvDraw` |

## Widescreen

RT64's **Expand** aspect widens a 3D pass to the display. The work is making the
game draw a frame that RT64 can widen, and anchoring the 2D.

### The 3D

1. **The inset viewport becomes the whole frame** (`uvChan_80204D94`): a
   channel given exactly the `SUBSCREEN` rect gets (0, 0)-(320, 240).
2. **The frustum is reshaped** (`uvChan_80204C94`). All the game's frusta have
   the inset's 300:214 aspect; on a 4:3 viewport that is a 5% horizontal squash.
   The vertical extent is scaled to 4:3 and the horizontal kept, so the field of
   view is the cartridge's. It is keyed on the frustum's shape, not a flag,
   because `uvChan_80204FE4` feeds the stored extents back in for its fog pass
   and a flag would compound every frame.
3. **Culling planes are widened.** They are built from the same extents
   (`func_802061A0`), so the patch widens the X extents by the display's aspect
   over 4:3 around the call. The projection matrix stays 4:3 -- widening it is
   RT64's job.
4. **The clip ratio is raised** (`uvChan_80204FE4`). The environment is drawn
   under clip ratio 1, which clips its triangles at the 4:3 edges: the terrain
   reached the sides and the sky and sea stopped short. Both passes get at least
   the widening, rounded up. *Symptom first suspected as the clear, which it was
   not.*
5. **The border is not drawn.** Its four rectangles are dropped in `uvVtxRect`,
   because the vehicle select and options screens draw them inline rather than
   through `drawScreenBorder`. With those rectangles in a frame, **RT64 did not
   widen the 3D at all**, and the previous screen showed at the sides.
6. **The clear reaches both edges** (`uvGfxClearScreen`): a plain fill rectangle
   stays 4:3 in RT64, so a full-frame clear is an extended fill rectangle
   anchored LEFT to RIGHT.

### RT64 facts the 2D depends on

- **Under an extended origin, a coordinate is measured from that edge of the
  320-wide screen**: `G_EX_ORIGIN_RIGHT` adds 320 pixels (`movedFromOrigin`). A
  scissor over the widened frame is LEFT 0 to RIGHT **0**
  (`gEXSetScissorWideFrame`), not `SCREEN_WIDTH`.
- **RT64 merges a frame's scissors, and treats the frame as the game's 4:3
  picture only if the merged shape is 4:3** (`adjustRatio` in
  `rt64_framebuffer_renderer.cpp`). One scissor reaching 640 pixels -- the
  mistake above -- switched that off, and every rectangle in the frame was
  stretched across the widened frame. The border rectangles broke the widening
  the same way from the other side.
- libultra's sprite library clips rectangles in software to 0-320, and a Fast3D
  texture rectangle cannot have a negative coordinate, so a rectangle cannot be
  moved past the 4:3 edge in game coordinates.

### The HUD

Each vehicle's HUD function is replaced by one that draws its left-edge and
right-edge elements in groups. A group moves by one margin, computed on the
host (`pw64_hud_margin`) the way RT64 turns **HUD Placement** into its extended
origin percentage: all of the extra width at Full, the part up to 16:9 at
Clamp16x9, none at Original.

- **Rectangles** (sprites, text, outlines) move by RT64's rect offset
  (`gEXSetRectAlign` with an offset), applied after decoding and so free of the
  software clip.
- **Triangles** (bars, the radar, the throttle) move by shifting the HUD's
  orthographic projection. A model-view translation would be lost: the radar and
  throttle load their own.
- **Text is flushed per group**, which exposed that `uvFontGenDlist`'s cursor is
  reset once per frame and advanced by message count: a second flush overwrote
  the first one's display lists and **digits vanished**. The patched function
  continues the cursor across flushes.
- **Full-screen quads** -- the menus' and results' dimming, the fades between
  screens and on a crash, the Sky Diving cloud fade, `hudDrawBox`, the camera
  shutter -- are stretched over the widened frame under a wide scissor. Most are
  caught generically in `uvVtxEndPoly`: a four-vertex polygon covering the
  screen, the inset viewport, or a pixel more than the inset. **It also sets the
  clip ratio**: a fade frame draws no channel, and at the default ratio of 1 the
  stretched quad is clipped straight back to 4:3.
- Cannonball's HUD is left centred: its gauges and target bar span the 4:3
  screen and read as one instrument.

## Frame interpolation

The game runs at 60, the VI rate (GAME-INTERNALS.md, *Timing*). RT64 draws the
frames a faster display needs by pairing each transform with the previous
frame's and interpolating. The patches tell RT64 what each transform *is*
through matrix groups, instead of leaving it to match by position.

### Matrix groups, briefly

`gEXMatrixGroup*(id, push, projection|modelview, components..., ordering)`
starts a group; `gEXPopMatrixGroup` ends it. Transforms loaded inside are paired
with the previous frame's transforms of the same id:

- `G_EX_ORDER_LINEAR`: by order within the id. Right when the same object loads
  the same parts in the same order.
- `G_EX_ORDER_AUTO`: by RT64's matching within the id. Right for things whose
  pieces come and go.
- `G_EX_ID_IGNORE` (`gEXMatrixGroupNoInterpolate`): not interpolated.
- An id with no counterpart in the previous frame is not interpolated -- which
  is how a cut, a spawn or a level-of-detail switch avoids a sweep.

### What is tagged

| Drawn by | Group | Id from |
|---|---|---|
| `uvChan_80204FE4` | projection, simple | channel + camera generation |
| `_uvDobjsDraw`, `uvChan_80205CE4` cases 2/3 | model-view, decomposed, linear | object slot, model, LOD |
| `uvDobj_8021771C` | its own projection group plus model-view | object slot |
| `uvChan_80205CE4` case 4 (static objects, terrain) | model-view, decomposed, linear | struct address, model, LOD |
| `_uvEnvDraw` | model-view, linear | environment model index |
| `uvChan_80205CE4` case 1 (effects) | model-view, decomposed, **auto** | effect slot, type, texture |
| `uvGfxMtxProjPushF` (all 2D) | projection and model-view, no-interpolate | -- |

Every id is hashed with the **camera generation**. Because the camera is baked
into every matrix on the CPU, a camera cut moves every transform at once, so a
cut must change every id. `interpCameraBegin` advances the generation when the
camera moves more than 60 units or its forward vector turns by more than about
40 degrees in one game frame, or when frames pass with no camera drawn.

### Measured

`tools/patch_rt64_pairing.py` makes RT64 count, per generated frame, the world
transforms, those not interpolated by request, those left unpaired, and the
unpaired whose matrix appears nowhere in the previous frame. In a scripted Hang
Glider flight held to 20 frames a second (`PW64_GAME_RATE=20`):

| | Transforms | Not interpolated by request | Unpaired (moved or new) |
|---|---|---|---|
| no tags | 99-172 | 0 | 1.5-4.8 (0.5-3.8) |
| tagged | 99-171 | 4-5 (the 2D) | 1.5-4.1 (0.5-3.1) |

**Effects must be interpolated.** Left out (`G_EX_ID_IGNORE`), they stepped at the
game's rate against gliding scenery, because their matrices carry the camera
too; that was 7 to 13 transforms a frame. A first count also read five times as
many unpaired transforms with tags as without, until the counter separated the
transforms RT64 skips on request.

**Verifying on a 60 Hz display.** With the game at the display's rate RT64 has
no frames to generate. `PW64_GAME_RATE=N` holds the game to N frames a second;
it measures its own frame time, so it plays at normal speed in bigger steps.

**Known limitation.** Decomposed interpolation of camera-baked matrices blends
the camera's rotation into each object's; the error is invisible at 60 and just
visible in fast turns at 20.

## Threads that never yield

**Symptom:** a screen change takes seconds on a stale frame; the game-rate
report reads single digits; the audio stops.

ultramodern runs each game thread on its own host thread but lets only one run
at a time, switching at OS calls (`osRecvMesg`, `osSendMesg`, ...) in priority
order -- the N64's scheduling, minus preemption. A loop that waits by spinning
on the clock never reaches an OS call, so a higher-priority thread made ready by
an interrupt never runs. Pilotwings 64 has three such loops (GAME-INTERNALS.md,
*Busy waits*); the worst waits for the audio thread to stop the music and
always ran into its two-second timeout, up to three times per screen change.

`uvClkGetSec` is what every one of them reads, so its patch first calls
`pw64_poll_threads`: `ultramodern::wait_for_external_message_timed(rdram, 0)`
delivers one pending event, if any, and `check_running_queue` switches to a
higher-priority ready thread. That is exactly what preemption at that instant
would have done; with nothing pending it is a queue check.

To find such a stall: `pw64_profile_mark(tag)` around the suspect code (time
since the previous mark, averaged per tag, printed under `PW64_PATCH_DEBUG`).

## Submodule patches

Submodules are never edited by hand -- an update reverts a hand edit silently.
Each change is an idempotent script; `python tools/patch_all.py` runs them all.

| Script | Changes | Why |
|---|---|---|
| `patch_rsprecomp.py` | RSPRecomp | indirect jumps ignore the low two bits of the target |
| `patch_librecomp.py` | librecomp | a failed function lookup reports its caller |
| `patch_runtime_shutdown.py` | ultramodern | join the workers before freeing RDRAM on exit |
| `patch_rt64_eventfilter.py` | RT64 | take RT64's SDL event filter back off so the frontend sees input |
| `patch_rt64_pairing.py` | RT64 | interpolation pairing counters (`PW64_PAIRING`) |
| `patch_macos.py` | RT64's hlsl++ | `<stdlib.h>` for `labs` on macOS |

## Testing and diagnostics

Everything a verification run needs is driven from the command line, so a run
can be repeated exactly.

### Scripted runs

- **`PW64_INPUT_SCRIPT=<file>`** replaces the controller with a timed script
  (`tools/scripts/*.txt`: `<start> <end> <buttons> [stick_x stick_y]`). The port
  prints a transcript of game states on stderr (`[pw64] t= 17.36 state:
  TEST_UPDATE (vehicle HANG_GLIDER, pilot 0, map 1)`), which is how a run is
  checked.
- **`PW64_TEST_VEHICLE=<0-6>`** sets the vehicle while the pilot select screen is
  up, so `flight.txt` reaches any vehicle's first test, bonus ones included.
- `tools/test_run.ps1 -Script ... -Count N -IntervalSeconds S` runs a script,
  photographs the window and prints the transcript from `pw64.log`.
- `tools/capture_frames.py OUT FROM TO --env K=V` records every presented frame
  through Windows Graphics Capture, even with the window covered, with stderr to
  `OUT/game.log`; `tools/contact_sheet.py` tiles them.

### Environment variables

| Variable | Effect |
|---|---|
| `PW64_INPUT_SCRIPT` | scripted input (above) |
| `PW64_TEST_VEHICLE` | vehicle override at pilot select |
| `PW64_TEST_OPEN_SETTINGS=tab@seconds` | opens the settings menu on a tab, for captures |
| `PW64_GAME_RATE=N` | holds the game to N frames a second |
| `PW64_FRAME_STATS=1` | prints the game and presentation rates every two seconds |
| `PW64_PAIRING=1` | prints RT64's interpolation pairing counts |
| `PW64_NO_INTERP_TAGS=1` | patches emit no matrix groups |
| `PW64_PATCH_DEBUG=1` | enables `pw64_debug` and `pw64_profile_mark` output |
| `PW64_PRESENT_MODE=console\|skip\|early` | RT64 presentation mode |
| `PW64_AUDIO_STATS`, `PW64_AUDIO_DUMP` | audio queue statistics; WAV dump of the output |
| `PW64_AUDIO_HEADROOM_MS`, `PW64_AUDIO_PERIOD`, `PW64_AUDIO_NO_RESAMPLE` | audio queue depth, device period, SDL resampling |

### Traps

- **A capture is not a measurement of motion.** A window capture keeps 30 to 55
  of 60 frames a second with jitter larger than an interpolation defect; count
  inside RT64 instead.
- **A counter that includes what was skipped on purpose** reads as a regression.
  RT64 leaves `G_EX_ID_IGNORE` transforms unpaired by design.
- **Interpolation is invisible when the game runs at the display's rate.** Slow
  the game down before judging it.
- PowerShell splits `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` at the dot unless the
  argument is quoted.
- `wsl -- bash -c "<script>"` re-parses its arguments through a shell and expands
  `$`; `wsl -e` does not.
