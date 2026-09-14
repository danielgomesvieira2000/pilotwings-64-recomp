# Changelog

Versions follow [semantic versioning](https://semver.org) loosely: while the
project is below 1.0, the minor number moves when something a player would
notice changes.

## 0.1.0 -- First release

The game, recompiled and running natively, with the first two enhancements.

- **The whole game runs**: title and attract demo, file select, every vehicle
  (Hang Glider, Rocket Belt, Gyrocopter) and bonus mode (Cannonball, Sky Diving,
  Jumble Hopper, Birdman), results, and EEPROM saves in the settings folder.
- **Widescreen.** The 3D view fills the display with the cartridge's horizontal
  field of view; the overscan border is removed; culling and clipping are
  widened so the edges are drawn; the sky and sea reach the sides. Each
  vehicle's HUD is anchored to the frame's edges, following the HUD Placement
  setting (Full, 16:9, Original). Menu and results dimming and the fades between
  screens cover the widened frame.
- **High frame rate.** Every 3D transform is tagged with a matrix group so RT64
  interpolates it by identity: cameras, dynamic and static objects, the terrain,
  the environment and effects. Camera cuts start a new generation of ids, so
  nothing is interpolated across a cut. The HUD is not interpolated.
- **Instant screen changes.** The game's busy waits let the audio thread run, as
  preemption did on the console: entering the file menu, the vehicle select and
  a test no longer stalls for up to six seconds.
- **Launcher and menus** from RecompFrontend: dump picker, Graphics, Sound,
  Controls and Mods tabs, a keyboard layout, controllers assigned as connected.
- **Testing tools**: scripted input with a game-state transcript, per-vehicle
  scripts, `PW64_TEST_VEHICLE`, frame capture, RT64 interpolation pairing
  counters, a game-rate throttle for checking interpolation on a 60 Hz display,
  and a profiling hook for patches. See [docs/PORTING.md](docs/PORTING.md#testing-and-diagnostics).

Known issues:

- The photo album has not been checked in widescreen.
- Built and tested on Windows only.
