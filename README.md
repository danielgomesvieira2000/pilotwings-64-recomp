# Pilotwings 64: Recompiled

A native PC port of Pilotwings 64, made by statically recompiling the game with
[N64Recomp](https://github.com/N64Recomp/N64Recomp) and running it on
N64ModernRuntime and the RT64 renderer. Unofficial, and not affiliated with
Nintendo.

**You need your own dump of Pilotwings 64 (USA)**
(sha1 `ec771aedf54ee1b214c25404fb4ec51cfd43191a`). No other version works, and
none is included. Build the port (below), run it, and pick your dump in the
launcher.

## Features

- **Widescreen** at your display's shape: the 3D view fills the frame with the
  game's field of view, the black overscan border is gone, objects at the edges
  are not culled early, and the HUD sits at the frame's edges or at 16:9 or 4:3,
  as you choose.
- **High frame rate**: every object, the terrain, the sky and effects are
  interpolated between the game's frames at your display's refresh rate, with
  camera cuts detected so nothing smears across them. The game keeps its own
  timing.
- **Screen changes are instant.** Entering a menu or starting a test no longer
  sits on a frozen frame for up to six seconds (a scheduling difference the
  original hardware hid; see [docs/PORTING.md](docs/PORTING.md#threads-that-never-yield)).
- A launcher with graphics, sound and controls settings, controller remapping,
  keyboard play, and mod support.
- Every vehicle and bonus mode runs; saves are written to your settings folder.

## Building

Full instructions are in [docs/BUILDING.md](docs/BUILDING.md). On Windows, with
Git, CMake, Ninja, LLVM, Python and the Visual Studio Build Tools installed, plus
WSL Ubuntu with `build-essential cmake ninja-build rsync binutils-mips-linux-gnu
python3-venv clang lld`:

```powershell
git clone --recurse-submodules https://github.com/danielgomesvieira2000/pilotwings-64-recomp
cd pilotwings-64-recomp
python tools/patch_all.py
wsl -d Ubuntu -- bash tools/wsl_build_recompiler.sh
python tools/generate_game.py "Pilotwings 64 (USA).z64"
cmake -B build -G Ninja "-DCMAKE_C_COMPILER=clang-cl" "-DCMAKE_CXX_COMPILER=clang-cl" "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" "-DPW64_WITH_RUNTIME=ON" "-DPW64_WITH_RECOMPILED=ON" "-DPW64_WITH_FRONTEND=ON"
cmake --build build --target Pilotwings64Recomp
build\Pilotwings64Recomp.exe
```

## Default keyboard controls

| N64 | Key | N64 | Key |
|---|---|---|---|
| Stick | Arrow keys | Start | Enter |
| A | X | Z | Z |
| B | C | L / R | A / S |
| C buttons | I J K L | D-pad | T F G H |

Controllers are assigned as they are connected. Everything can be remapped in
the launcher's Controls tab.

## Documentation

- [docs/BUILDING.md](docs/BUILDING.md) -- the build, step by step
- [docs/PORTING.md](docs/PORTING.md) -- how the port works: the pipeline, the C
  patches, widescreen, interpolation, and the testing tools
- [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) -- what the game turned out
  to be
- [docs/](docs/README.md) -- the index, the build plan and the findings

## Credits

- **N64Recomp and N64ModernRuntime** by Mr-Wiseguy and contributors
- **RT64** by Dario and contributors
- **RecompFrontend** by the N64Recomp contributors
- **[Pilotwings 64 decompilation](https://github.com/gcsmith/Pilotwings64Decomp)**
  by Garrett Smith and contributors, which this port is built from
- gcsmith's [Pilotwings64Recomp](https://github.com/gcsmith/Pilotwings64Recomp),
  for the hardware-register instructions the runtime cannot serve
- The harness and frontend come from
  [Wave Race 64: Recompiled](https://github.com/danielgomesvieira2000/wave-race-64-recomp)
- Written by Claude (Anthropic) in Claude Code, under the direction of Daniel
  Gomes Vieira

## Licensing

The project's own code is MIT ([LICENSE](LICENSE)). A built executable links
N64ModernRuntime, which is GPL-3.0, so the executable is a GPL-3.0 combined work
whose source is this repository at the commit it was built from. The executable
also contains the game's code, recompiled from the builder's own dump. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for every component.
