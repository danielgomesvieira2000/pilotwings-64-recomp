# Changelog

Versions follow [semantic versioning](https://semver.org) loosely: while the
project is below 1.0, the minor number moves when something a player would
notice changes.

## 0.1.1 -- Photo album in widescreen

Fixed:

- The photo album in widescreen: each photo was drawn about twice the size of
  its slot, overlapping the others

Known bugs:

- Changing the aspect ratio while the photo album is open blanks it until the
  album is reopened

## 0.1.0 -- First release

Added:

- The whole game running natively on Windows and Linux: every vehicle and bonus
  mode, menus, results and saves
- Widescreen: the 3D fills the display, the overscan border is removed, and the
  HUD sits at the screen edges (following the HUD Placement setting)
- High frame rate through matrix interpolation of every 3D object, with camera
  cuts detected
- Instant screen changes, where the original stalls for up to six seconds
- Launcher with graphics, sound and single-player controls settings, remapping
  and mod support
- Scripted-input testing tools, frame capture and interpolation counters

Known bugs:

- Decomposed interpolation blends the camera's rotation into each object's; at
  low game frame rates, fast turns are very slightly off
- Cannonball's aiming HUD stays centred rather than moving to the edges

Not tested:

- The photo album in widescreen
- Interpolation on a real 120 Hz or faster display (verified with the game
  slowed down on a 60 Hz one)
- Linux on real hardware: tested under WSL2 with software Vulkan
- macOS (not supported)
