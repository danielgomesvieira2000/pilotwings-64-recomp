# Build plan

The plan this project is executed against. It is written before the work and
kept as written, so the reasoning behind each phase survives; where the work
overturns something here, a note is added at the top rather than the text being
rewritten. What was actually found, phase by phase, goes in
[findings/](findings), and the reference someone else can use without reading
the findings goes in [PORTING.md](PORTING.md) (port facts) and
[GAME-INTERNALS.md](GAME-INTERNALS.md) (game facts).

> **Where the work departed from this plan** (added at 0.1.0):
>
> - *Phase 08* chose interpolation without trying to run the game faster: the
>   game already runs at the VI rate, 60, so the only gain would have come from
>   changing the runtime's VI rate under a physics step never tuned for it.
>   Effects are interpolated (with RT64's own ordering in a group per effect)
>   rather than ignored: with the camera baked into their matrices, ignoring
>   them made them shake. The gate's "at 120+ Hz" was verified with the game
>   slowed to 20 on a 60 Hz display, not on a faster display.
> - *Phases 04, 05 and 07*: testing every mode found that the game's busy waits
>   stall for seconds under the runtime's scheduling (fixed in `uvClkGetSec`),
>   and that two screens draw the border inline (fixed in `uvVtxRect`). See
>   [findings/phase-08.md](findings/phase-08.md).
> - *Phases 05 and 06* have no findings document of their own: the harness,
>   audio path and frontend came from Wave Race 64: Recompiled and worked on the
>   first run; what changed is in [findings/phase-00-04.md](findings/phase-00-04.md).
> - The pause menu, the photo album and the options screen have not been checked
>   in widescreen.

## Goal

A native PC port of **Pilotwings 64 (USA)** built by static recompilation with
[N64Recomp](https://github.com/N64Recomp/N64Recomp) (Mr-Wiseguy), running on
N64ModernRuntime and RT64, with the RecompFrontend launcher and menus, using the
same harness and frontend as
[Wave Race 64: Recompiled](https://github.com/danielgomesvieira2000/wave-race-64-recomp).

Enhancements for the first release:

- **High frame rate**, with each object's transform interpolated between the
  game's frames.
- **Widescreen**: the 3D view widened to the display, the HUD anchored to the
  frame's edges, and the game's black overscan border removed.

## Target

| | |
|---|---|
| Game | Pilotwings 64 (USA), `NPWE`, revision 0 |
| SHA-1 | `ec771aedf54ee1b214c25404fb4ec51cfd43191a` |
| Header CRC | `0xC851961C 0x78FCAAFA` |
| Entry point | `0x80200050` |

This is the one dump the decompilation matches, so it is the only target.

## The decision this project is built on

Wave Race 64 had no usable decompilation: it had to assemble an ELF out of
splat output, and every enhancement was made from outside the game by a
display-list rewriter that inferred what each draw was. Pilotwings 64 is in a
different position, and the plan exploits it.

**[gcsmith/Pilotwings64Decomp](https://github.com/gcsmith/Pilotwings64Decomp)
is 100% decompiled, matching, and MIT licensed.** It even has a
`RECOMP_BUILD` switch that exports the file-local functions and data a
recompilation needs to see. So:

1. **The ELF is the decompilation's own matching build**, made with
   `RECOMP_BUILD=1` and verified by SHA-1 against the dump. Every function has
   its real name and size; nothing is guessed. No splat padding tricks, no JAL
   scan.
2. **N64Recomp runs in ELF input mode**, which unlocks the reference-symbol and
   single-file patch workflows.
3. **Enhancements are C patches, not display-list inference.** A patch is
   ordinary C written against the decompilation's headers, compiled for MIPS
   with Clang, recompiled by N64Recomp into `RecompiledPatches/`, and replaces
   the game's function by name (`RECOMP_PATCH`). This is how Zelda 64:
   Recompiled does its interpolation, and it is the reliable way: a patch
   *knows* which object a matrix belongs to instead of guessing it from
   positions. It is also why this port should not need Wave Race's pairing
   heuristics, camera regions, jump limits or HUD classifier.
4. **The harness and frontend come from Wave Race 64: Recompiled** --
   `main.cpp`, the platform callbacks, the audio path with its own resampler,
   the RecompFrontend wiring, the crash handler, the patch scripts for the
   submodules and the testing tools -- renamed and trimmed of what is
   Wave-Race-specific (water, buoys, the display-list rewriter, haptics).

The decompilation is a submodule (`lib/Pilotwings64Decomp`), pinned, because
patches compile against its headers. Nothing derived from the ROM is committed:
the ELF, `RecompiledFuncs/` and `RecompiledPatches/` are generated at build time.

An independent WIP recompilation exists
([gcsmith/Pilotwings64Recomp](https://github.com/gcsmith/Pilotwings64Recomp),
GPL-3.0). This repository is MIT, like Wave Race's, so no code is taken from it.
Facts learned from it -- such as which three instructions touch hardware
registers the runtime does not emulate -- are credited where they are used.

## What is already known about the game

Measured during planning, from the decompilation:

- **Two contiguous code segments** (`kernel` at `0x802000A0`, `app` at
  `0x802CA900`), loaded once by the boot code. **No overlays, no compressed
  code.** The overlay-dispatch problems that dominated Wave Race's bring-up
  do not exist here.
- **Variable timestep.** `uvGfxEnd` measures each frame with a clock
  (`gGfxFrameTime`), and animation, effects and menus advance by
  `uvGfxGetFrameTime()`. The scheduler is created with `numFields = 1`, so the
  game presents at up to 60 Hz and slows down when the frame is expensive.
  This matters for high frame rate: see phase 08.
- **The game reads `osMemSize`** to size its heap (`uvLevelInit`). The upper
  4 MB of an 8 MB runtime are therefore *not* guaranteed free the way they
  were for Wave Race; any scratch memory the port uses must live outside the
  game's RAM (the patch sections) or be proven unused.
- **Every matrix goes through a handful of kernel functions**
  (`uvGfxMtxViewLoad`, `uvGfxMtxViewMul`, `uvGfxMtxView`, `uvGfxMtxProj`,
  `uvGfx_802236CC`, ...) into a double-buffered matrix stack. Those are the
  natural places to tag matrix groups.
- The game draws a **screen border** in 2D (`drawScreenBorder`, the
  `SUBSCREEN_*` constants) and sets camera viewports inside it.
- **Saves**: EEPROM through `osEepromLongRead/Write`.
- The **audio microcode** (`aspMain`) is a textbin in the kernel segment; the
  graphics microcode is Fast3D.

## Phases

Each gate is the entry condition for the next phase. Every phase ends with the
tree building, and from phase 03 on, with the game run and looked at.

### 00 -- Skeleton
Repository, submodules (N64ModernRuntime, RT64 and RecompFrontend pinned at the
revisions Wave Race 64 ships on; the decompilation), a `.gitignore` that refuses
game data, phase-gated CMake, `--identify`.
**Gate:** the tree configures and builds; `--identify` accepts the dump.

### 01 -- The ELF
`tools/build_elf.sh`: build the decompilation's toolchain, drop the dump in as
`baserom.us.z64`, `make RECOMP_BUILD=1`, check the SHA-1, copy the ELF out.
**Gate:** `pilotwings64.us.elf` whose ROM image matches the dump byte for byte.

### 02 -- First recompile
`recomp/pilotwings64.us.toml` in ELF mode; `recomp/aspMain.us.toml` for
RSPRecomp. Instruction patches for the hardware-register accesses the runtime
cannot serve. Generated code is never hand-edited: fixes go in the config or in
a script.
**Gate:** all of `RecompiledFuncs/` compiles into a static library.

### 03 -- Runtime harness
Port Wave Race's harness: `main.cpp`, callbacks (input, audio with the port's
resampler, RSP dispatch to `aspMain`), the renderer context, crash handler,
per-user settings directory, EEPROM save, submodule patch scripts.
**Gate:** the executable reaches `recomp_entrypoint` and runs the first thread.

### 04 -- Boot bring-up
Title screen, attract demo, file select, pilot select, and a flight in each
vehicle (Hang Glider, Rocket Belt, Gyrocopter) plus the bonus modes (Cannonball,
Sky Diving, Jumble Hopper, Birdman). Diagnose with the crash handler and
function tracing, not a debugger on generated C.
**Gate:** every mode is reachable and renders recognisably.

### 05 -- Audio and correctness
Music and effects through the recompiled microcode, correct pitch and stereo.
Saves survive a restart. No crackle.
**Gate:** a test flight to results with correct sound, and a save that loads.

### 06 -- Frontend
RecompFrontend's launcher, ROM picker, Graphics/Sound/Controls/Mods tabs,
default keyboard layout for this game's controls, controller auto-assignment,
fullscreen at the display's size on first run.
**Gate:** a stranger can pick their dump in the launcher and play with a pad or
the keyboard.

### 07 -- Patches pipeline and widescreen
Stand up the C patch pipeline (`patches/`, Clang for MIPS, N64Recomp in
single-file patch mode, reference symbols dumped from the ELF). Then:

1. *Border removal.* The game's 2D border and the camera viewports inset inside
   it become full-frame.
2. *Widened 3D.* RT64's Expand aspect, with the game's frustum and culling
   widened to match so objects at the edges are not culled early.
3. *HUD.* 2D elements anchored to the frame edges with RT64's extended GBI
   (`gEXSetViewportAlign` / `gEXSetRectAlign`) from inside the functions that
   draw them, not by classification; full-screen fades and menus stretched or
   centred as each needs.

**Gate:** title, menus, pilot select, every vehicle's HUD, results and the pause
menu look right at 16:9 and 21:9 and in a 4:3 window. Nothing cut off, nothing
doubled, nothing stretched that should not be.

### 08 -- High frame rate
Two mechanisms are available, and the plan is to measure before choosing:

- **Interpolation.** The game keeps its own rate and RT64 draws the frames in
  between by interpolating each transform. Safe for gameplay by construction.
- **Running the game itself faster.** The game already uses a measured frame
  time, so it might simply run correctly at higher rates. It might also not:
  physics integrated with a variable step can change behaviour (landing
  scoring, thermals, wind) and the game may assume a minimum step.

Order of work:

1. Measure the rate the game runs at in the port, per mode, and whether it is
   already the VI rate.
2. Tag matrix groups in patches: projection and view per camera, each object
   by its dobj/sobj identity, vehicle and pilot parts, the terrain and sky, and
   particles and effects (`G_EX_ID_IGNORE` or vertex interpolation, whichever
   holds up). 2D and HUD are drawn without interpolation.
3. Handle camera cuts and object spawns: skip interpolation on the frame a
   camera or object teleports.
4. Verify with frame captures at the display's rate: no smearing at cuts, no
   objects lagging their shadows, no judder on the vehicle.

**Gate:** a flight in each vehicle at 120+ Hz shows smooth motion with no
interpolation artefacts, and results/scores are identical to the game's own
rate for a scripted run.

### 09 -- Release
Documentation (README, BUILDING, PORTING, GAME-INTERNALS, findings),
third-party notices, packaging script, GitHub repository, and a first tagged
release containing no game data.
**Gate:** a clean clone builds with the documented commands.

## Testing throughout

- Build after every change; the tree is never left not building.
- From phase 03, run the game after every behavioural change and look at it:
  `tools/capture_window.ps1` for screenshots, timed input scripts
  (`PW64_INPUT_SCRIPT`) to reach a scene reproducibly, and the port's own state
  transcript on stderr.
- A regression found is written down in the findings before it is fixed.

## Standing constraints

- No ROM, asset, or ROM-derived file is ever committed.
- Generated code is never hand-edited.
- Submodules are never hand-edited: every change to one is an idempotent script
  in `tools/`, because a submodule update reverts a hand edit silently.
- Build RelWithDebInfo (optimized); a Debug build of a recompiled port breaks
  audio timing.
- Any change that discovers a fact about the game or the toolchain updates
  GAME-INTERNALS.md or PORTING.md in the same commit.
