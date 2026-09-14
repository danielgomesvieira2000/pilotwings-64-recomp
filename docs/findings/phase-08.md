# Findings: phase 08 -- high frame rate, and what testing every mode turned up

The working record, including the wrong turns. The reference versions are in
[../PORTING.md](../PORTING.md) and [../GAME-INTERNALS.md](../GAME-INTERNALS.md).

## Which mechanism

The plan left two options open: interpolate between the game's frames, or run
the game itself faster.

- **The game already runs at the VI rate.** Measured from the `_uvScDoneGfx`
  patch: 60 frames per second in flight and in the menus (phase 07). The game's
  timestep is measured, and the scheduler is created with one field, so 60 is
  its ceiling -- the retrace, not the game, sets it.
- Running it above 60 would mean changing the scheduler's field count and the
  runtime's VI rate, and letting a variable-step physics integration take steps
  it was never tuned for (landing scores, thermals, the Jumble Hopper's bounce).
  Nothing was gained by that risk while RT64 can draw the frames in between.
- **So the port interpolates.** The game keeps its 60, and RT64 generates the
  frames a 120 or 144 Hz display needs. On a slower machine that holds the game
  below 60, the same mechanism smooths that too.

## Testing interpolation on a 60 Hz display

The development machine's display is 60 Hz, the same as the game's rate, which
leaves RT64 nothing to generate and interpolation invisible. `PW64_GAME_RATE=20`
(src/patch_host.cpp) sleeps in the frame hook to hold the game to 20 frames a
second. The game measures its own frame time, so it still plays at normal speed,
in bigger steps, and RT64 generates two frames between each pair.

## Tagging

Every 3D transform is drawn inside an RT64 matrix group whose id says what it
is; see `patches/interpolation.c` and PORTING.md, *Frame interpolation*. Two
properties of the engine decided the design:

- **The camera is baked into every matrix.** `uvGfx_802236CC` multiplies the look
  transform into each object's matrix on the CPU; the projection is the frustum
  alone, and the terrain is static objects. A camera cut therefore moves every
  transform at once. Every id includes a *camera generation*, advanced when the
  camera moves more than 60 units or turns by more than about 40 degrees in one
  game frame, or when a frame draws no camera at all.
- **An object's parts are loaded inside one call**, so one group around the call
  covers them, paired in drawing order (`G_EX_ORDER_LINEAR`). The id includes the
  model and the level of detail, so a respawn or a detail switch is a new id.

A run with `PW64_PATCH_DEBUG` printed every generation change: twelve in a whole
boot-to-flight run, all in menu transitions, none in flight.

## Measuring it

Window captures were tried first (`tools/frame_motion.py`: the variation of the
change between consecutive captured frames). They could not settle anything:
the capture keeps 30 to 55 of 60 frames a second with its own timing jitter, and
tagged and untagged runs gave coefficients of 0.24-0.27 against 0.26-0.35 with
the ordering changing between repeats.

So RT64 was made to count (`tools/patch_rt64_pairing.py`, carried over from
Wave Race 64: Recompiled): per generated frame, the world transforms, those left
unpaired, and of those the ones whose matrix appears nowhere in the previous
frame (the ones that visibly step). `PW64_PAIRING=1` prints the rates every two
seconds. All runs below: Hang Glider, Beginner test 1, `PW64_GAME_RATE=20`,
`tools/scripts/flight.txt`, two-second windows over the same stretch of flight.

| Build | Transforms a frame | Unpaired | ...moved or new |
|---|---|---|---|
| no tags (`PW64_NO_INTERP_TAGS=1`), RT64's own matching | 99-172 | 1.5-4.8 | 0.5-3.8 |
| tagged, first count | 99-171 | 15.3-21.9 | 8.0-17.5 |

That looked like tagging made pairing five times worse. **It was the counter.**
RT64 marks transforms in a `G_EX_ID_IGNORE` group unpaired on purpose, and the
tags put both the 2D and the particle effects in such groups. The counter was
split:

| Build | Not interpolated by request | Unpaired | ...moved or new |
|---|---|---|---|
| tagged, effects not interpolated | 11.3-18.4 | 1.4-4.3 | 0.4-3.3 |
| tagged, effects with their own ids | 4.0-5.0 | 1.5-4.1 | 0.5-3.1 |

**Effects had to be interpolated after all.** With the camera baked into their
matrices, an effect left out of interpolation steps at the game's rate against
scenery that glides -- smoke and spray shaking on screen. They now have a group
per effect slot, keyed by the effect's type and texture, with RT64's own
ordering inside it (`G_EX_ORDER_AUTO`) because their pieces come and go. The
four transforms a frame that remain are the HUD and 2D, which must not move.

The unpaired count is no worse than RT64 matching alone, and the tags make the
pairing *correct* where RT64's matching can only be close: an id cannot pair two
different objects, which the counter cannot see and a capture shows only as an
occasional wrong-way slide.

**Known limitation.** Because the camera is baked in, an object's decomposed
interpolation blends camera rotation and object motion together rather than
interpolating the camera separately. At the game's normal 60 the error between
two generated frames is too small to see; at `PW64_GAME_RATE=20` a fast turn
shows the terrain's far edges very slightly off an ideal arc.

## Everything else testing turned up

Testing every mode (the plan's phase 04 and 07 gates) found three problems that
had nothing to do with interpolation.

### A six-second stall on every music change

Entering the file menu took about six seconds, on a stale frame showing a black
box; so did starting any test. The game-rate report read **4 frames per
second** in the file menu. Profiling with `pw64_profile_mark` around the file
menu's loop found the loop itself fast (40 iterations at 22 ms). The time was
before it: **5,856 ms inside `sndSetMusicState`**.

`uvaSeqStop` stops the sequence player and then spins on the clock until the
audio thread reports it stopped, giving up after two seconds. A music change
calls it up to three times (`sndSetMusicState`, `uvaSeqNew`, `uvaSeqPlay`). On
the N64 the audio thread preempts the spinning thread at its next retrace and the
loop ends within a frame. The runtime switches game threads only inside OS calls,
and the loop makes none -- so every call ran into its timeout.

The same kind of loop waits one second on the title screen (the 15 frames per
second dip the report showed there) and a tenth of a second per photograph in
the photo album. All of them read `uvClkGetSec`, which now polls the runtime's
event queue and switches to any higher-priority thread made ready before reading
the clock (`patches/clocks.c`). After the fix:

| | Before | After |
|---|---|---|
| title to file menu (`flight.txt`) | 12.3 s | 8.3 s |
| file menu to vehicle select | 7.0 s | 1.0 s |
| test setup to flight | 6.0 s | 0.1 s |

### A border drawn somewhere else

The vehicle select screen showed the game's black border, the 3D inset inside
it, and the previous screen's picture at the sides. Logging the channel's
viewport showed it full frame, so the 3D was not inset; the border was being
drawn -- by `func_8030F448`, which draws the same four rectangles as
`drawScreenBorder` inline, as `options.c` does too. And those rectangles did more
than cover the 3D: with them in the frame, RT64 did not widen the 3D at all.
The patch moved from `drawScreenBorder` to `uvVtxRect`, dropping exactly those
four rectangles wherever they come from.

`uvVtxRect` emits its texture rectangle with `G_RDPHALF_1`/`_2` one lower than
`gbi.h` defines them (geometry.c redefines them for the game's older Fast3D).
The transcription has to redefine them the same way.

### Dimming that stopped at 4:3

The file menu, vehicle select, test overview and results screens dim the 3D
behind them with a translucent black quad over exactly (0, 0)-(320, 240). It
covered the middle 4:3 of a widened frame. `uvVtxEndPoly` now stretches a
four-vertex polygon at exactly those coordinates over the widened frame under a
wide scissor, and restores the viewport's scissor after.

### Coverage

- **Every vehicle reaches flight** with its HUD at the edges: Hang Glider, Rocket
  Belt and Gyrocopter from input scripts that pick them on the vehicle select
  grid; Cannonball, Sky Diving, Jumble Hopper and Birdman through
  `PW64_TEST_VEHICLE`, which sets the vehicle while the pilot select screen is up
  (a fresh save has not unlocked them). Cannonball's HUD stays centred: its
  power and angle gauges and target bar span the 4:3 screen and belong together.
- **Saves**: the EEPROM file is created with both files' `pw` markers on first
  boot and is not rewritten on later boots; finishing a test and choosing Next
  writes the new record to disk.
- A crash ends in the game's own fade to white, then the results screen.

### Open

- One transition frame, between the file menu and vehicle select, shows the
  dimming as a 4:3 box: a fade drawn with other coordinates.
- The photo album (reached from results with photographs taken) and the
  options screen have not been looked at in widescreen.
- Interpolation has been verified with the game slowed to 20; it has not been
  watched on a real 120 Hz or faster display.
