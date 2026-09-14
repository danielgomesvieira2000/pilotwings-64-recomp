# Documentation

## Start here

| | |
|---|---|
| [BUILDING.md](BUILDING.md) | From an empty Windows machine to the game running: the toolchain, WSL, the dump, the decompilation, N64Recomp, the patches, CMake, packaging. |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | The rules, chiefly about keeping the game's data out of the repository. |
| [../CHANGELOG.md](../CHANGELOG.md) | What changed in each release. |

## The technical reference

Kept current as the port changes: a change that discovers a fact about the game
or the toolchain updates one of these in the same commit.

| | |
|---|---|
| [PORTING.md](PORTING.md) | **The port.** The pipeline from a dump to an executable, building the ELF from the decompilation, N64Recomp in ELF mode with mdebug, the harness and what this game needed from it, C patches and host functions, widescreen (the 3D, RT64's scissor and origin rules, the HUD), frame interpolation with matrix groups and how it was measured, threads that never yield, the submodule patches, and the testing tools and environment variables. |
| [GAME-INTERNALS.md](GAME-INTERNALS.md) | **The game.** Identity, memory layout, the game states and the vehicle select grid, timing and busy waits, channels, the inset viewport, frusta, how matrices carry the camera, 2D and the HUD, audio and the save format. |

## The working record

| | |
|---|---|
| [PLAN.md](PLAN.md) | The build plan the project was executed against, phase by phase, with its standing constraints. |
| [findings/phase-00-04.md](findings/phase-00-04.md) | From a dump to the title screen: the ELF, the first recompile, the harness, the lookup misses at boot. |
| [findings/phase-07.md](findings/phase-07.md) | The C patch pipeline, and widescreen for the 3D and the HUD, including the scissor that stretched every rectangle. |
| [findings/phase-08.md](findings/phase-08.md) | Interpolation: choosing it, tagging, measuring pairing inside RT64. Then what testing every mode found: the six-second music stall, the inline border, the dimming. |
