# Findings: phase 07 -- the C patch pipeline and widescreen

The working record, including the wrong turns. The reference versions are in
[../PORTING.md](../PORTING.md) and [../GAME-INTERNALS.md](../GAME-INTERNALS.md).

## The patch pipeline

- **Windows LLVM has no MIPS backend.** The LLVM 22 that winget installs fails
  with `No available targets are compatible with triple "mips"`, and before that
  rejects `-mno-check-zero-division` and friends as unknown LLVM options -- the
  tell that the target is not registered at all. WSL's Clang 21 has it. The
  patches are compiled through WSL, linked with `ld.lld-18` from
  `/usr/lib/llvm-18/bin` (lld-21 is not installed there, and any lld links a MIPS
  ELF).
- `wsl -- bash -c "<script>"` re-parses the arguments through a shell and expands
  the `$` in them; `wsl -e` runs the command directly.
- **The decompilation's headers compile under Clang for MIPS as they are.** The
  only error on the first patch was my own duplicate prototype.
- **A patch cannot override the game's function by weak linkage on Windows.**
  N64Recomp emits both the game's function and the patch with `RECOMP_FUNC`,
  which under Clang is `extern inline __attribute__((weak))`. Made strong through
  the patch config's `recomp_include`, the patch still collided:
  `lld-link: error: duplicate symbol: _uvScDoneGfx`. Under clang-cl the weak
  definition is a COMDAT, which a second definition collides with rather than
  replaces. The game's copy is now not emitted (`ignored`), and
  `tools/build_patches.py` refuses to build when a `RECOMP_PATCH` is missing from
  that list.
- `ignored` does not remove a function from `--dump-context`'s reference symbols
  (the dump returns before the list is applied), so `strict_patch_mode` still
  finds the name.
- An `ignored` function is also absent from the section table, so a call through
  a function pointer to its original address would fail. The build script writes
  `patched_addresses.inl`, and each patch is registered at the original address
  from `on_init`.
- Patch output calls game functions that the patches' own `funcs.h` does not
  declare; `recomp_include` pulls in the game's `funcs.h` and
  `reimplemented_decls.h`.
- libultra functions the runtime reimplements (`osViSwapBuffer`,
  `osVirtualToPhysical`, ...) are reached from patches as manual symbols at
  `0x8F000000+`, named `<name>_recomp`, with `#define`s in `patches.h`.
- The generated `mdebug_file_mappings` (265 entries) made the committed
  recompiler config unreadable; it now lives in a generated
  `pilotwings64.us.full.toml`.

## The game's frame rate

The first patch, `_uvScDoneGfx` with a call to the host after the buffer swap,
measured **60 frames per second in flight**, 55-60 in the menus, with dips to
0-15 during loads. The cartridge ran at 20 to 30. The game uses a measured frame
time, and in the port nothing throttles it except the 60 Hz VI.

## Widescreen, 3D

1. **Border and inset viewport.** The world is drawn into (10, 18)-(310, 232)
   with a 2D border around it. Replacing the viewport with the whole frame
   (`uvChan_80204D94`) and skipping `drawScreenBorder` widened the terrain and
   objects to the window edges straight away.
2. **The frustum.** All 3D frusta in the game have the inset's aspect,
   0.4906542/0.35 = 0.7009346/0.5 = 300/214. On a 4:3 viewport that is a 5%
   horizontal squash, so the vertical extent is scaled to 4:3 in
   `uvChan_80204C94`. It has to be idempotent: `uvChan_80204FE4` feeds the stored
   extents back into the same setter for its fog pass, and a scale-by-flag would
   compound every frame. Keying on the frustum's own shape makes a second pass a
   no-op.
3. **Culling.** The culling planes are built from the same extents
   (`func_802061A0`), so they are built with the X extent widened by the display
   aspect over 4:3, while the projection matrix is left at 4:3 for RT64 to widen.
4. **Stale sides above the terrain.** After 1-3, the sky stopped at the 4:3 edges
   and the title's sea was 4:3 with black sides. First suspect was the clear:
   `uvGfxClearScreen` is a `gDPFillRectangle`, which RT64 keeps at 4:3, so it was
   replaced with an extended fill rectangle anchored LEFT..RIGHT under a widened
   scissor -- and changed nothing visible. **The cause was the clip ratio.**
   `uvChan_80204FE4` draws the environment (sky dome, sea) under
   `FRUSTRATIO_1`, which clips every triangle at the 4:3 edges, and only then
   raises it to 2 for terrain and objects. Raising both to at least the widening
   fixed the sky and the title in one step. (The clear fix is kept: it is what
   stops the previous frame showing through where nothing is drawn.)
5. The extended GBI is enabled at the top of every frame's list in `uvGfxBegin`.

## Widescreen, HUD

The HUD is texture rectangles (sprites and text, through libultra's sprite
library) and triangles (gauges, radar, bars), with text queued by
`uvFontPrintStr` and emitted once per frame by `uvFontGenDlist`.

1. **Origins, per group.** Each vehicle's HUD function was replaced by one
   drawing its left and right elements in groups, each with
   `gEXSetRectAlign`/`gEXSetViewportAlign` to LEFT or RIGHT and an offset of
   `-origin * 320 * 4 / 1024` to cancel RT64's `movedFromOrigin`. Result: labels
   moved, **digits vanished**, the altimeter came apart.
2. **Vanishing digits.** Flushing the text per group called `uvFontGenDlist`
   several times a frame. Its sprite display-list cursor is reset once per frame
   and advanced by the number of *messages*, not the commands written, so each
   later flush wrote over the display lists an earlier one had already put in the
   frame. The patched version keeps the cursor where `spDraw` left it.
3. **The altimeter apart.** Its box and bar (triangles) landed to the right of
   its digits and outline (rectangles), by more the further right. Setting the
   clip ratio to 1 inside groups (RT64 places viewport-aligned triangles through
   the clip-ratio-scaled viewport) did not fix it.
4. **Common shift instead of origins.** The margin is computed on the host
   (`pw64_hud_margin`, following RT64's `extAspectPercentage` for HUD
   Placement); triangles move by shifting the HUD's orthographic projection
   (the radar and throttle load their own model-view matrices, so a model-view
   translation would be lost). Rectangles by a plain rect offset: they now moved
   **twice** as far as the triangles.
5. Moving rectangles in game coordinates instead (sprite `xpos`, text `x`) gave
   the same doubling -- so the unit was not the problem. Measuring positions in
   the capture showed the moved rectangles following a *stretched* mapping,
   `960 + (x - 160) * 6`, not the squeezed `960 + (x - 160) * 4.5`.
6. **The cause: the wide scissor.** Under `G_EX_ORIGIN_RIGHT` a coordinate is
   measured from the right edge of the 320-wide screen (`movedFromOrigin` adds
   320 pixels), so `gEXSetScissor(LEFT, RIGHT, 0, 0, SCREEN_WIDTH, ...)` reached
   640 pixels. RT64 merges a frame's scissors and treats the frame as the game's
   4:3 picture only if the merged shape is 4:3
   (`adjustRatio` in `rt64_framebuffer_renderer.cpp`); at 640x240 that failed,
   and every rectangle in the frame was stretched. The right edge is 0 under
   RIGHT (`gEXSetScissorWideFrame`). The same mistake was in the clear.
7. With the scissor right, shifting rectangles in game coordinates clipped them
   at the 4:3 edges: libultra's sprite library clips in software to 0-320, and a
   Fast3D texture rectangle cannot have a negative coordinate anyway. So
   rectangles take RT64's rect offset (applied after decoding) and triangles the
   projection shift, both from the one host-computed margin. Speed, timer and
   labels at the left edge, radar, sea level and a whole altimeter at the right.

Open: the Cannonball HUD is left centred (its target bar spans the 4:3 screen);
the viewport-origin approach in 1 was never re-tested with the scissor fixed.
