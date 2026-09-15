# Pilotwings 64: Recompiled

A native PC port of Pilotwings 64 for Windows and Linux, made by statically
recompiling the game with [N64Recomp](https://github.com/N64Recomp/N64Recomp).

Unofficial and not affiliated with Nintendo. **No game data is included**: you
need your own dump of Pilotwings 64 (USA).

## Features

- Widescreen at your display's aspect ratio, with the HUD at the screen edges
- High frame rate through matrix interpolation, up to your display's refresh rate
- No black overscan border
- Instant screen changes (the original stalls up to six seconds on some menus)
- Launcher with graphics, sound and controls settings, remapping and mod support
- Keyboard and controller support

## Getting started

1. Download the build for your system from [Releases](../../releases).
2. Extract it and run `Pilotwings64Recomp.exe` (Windows) or `Pilotwings64Recomp.sh` (Linux).
3. Pick your dump in the launcher.

The dump must be **Pilotwings 64 (USA)**, SHA-1
`ec771aedf54ee1b214c25404fb4ec51cfd43191a`, as `.z64`, `.n64`, `.v64` or a ZIP.

On Linux the port needs a working Vulkan driver and the distribution's SDL2,
GTK 3 and FreeType; see `README-LINUX.txt` in the download.

### Default keyboard controls

| N64 | Key | N64 | Key |
|---|---|---|---|
| Stick | Arrow keys | Start | Enter |
| A | X | Z | Z |
| B | C | L / R | A / S |
| C buttons | I J K L | D-pad | T F G H |

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md). In short:

```sh
# Linux
git clone --recurse-submodules https://github.com/danielgomesvieira2000/pilotwings-64-recomp
cd pilotwings-64-recomp
bash tools/setup_linux.sh --install
bash tools/build_linux.sh "/path/to/Pilotwings 64 (USA).z64"
```

Windows builds the same way through WSL; the commands are in BUILDING.md.

## Documentation

- [Building](docs/BUILDING.md)
- [How the port works](docs/PORTING.md)
- [Game internals](docs/GAME-INTERNALS.md)
- [Changelog](CHANGELOG.md)

## Credits

- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy and contributors
- [RT64](https://github.com/rt64/rt64) by Dario and contributors
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp contributors
- [Pilotwings 64 decompilation](https://github.com/gcsmith/Pilotwings64Decomp) by Garrett Smith (gcsmith) and contributors, which this port is built from
- [Pilotwings64Recomp](https://github.com/gcsmith/Pilotwings64Recomp) by gcsmith, for identifying the hardware accesses the runtime cannot serve
- [ido-static-recomp](https://github.com/decompals/ido-static-recomp) and [splat](https://github.com/ethteck/splat), used by the decompilation's build
- [PromptFont](https://github.com/Shinmera/promptfont) by Yukari "Shinmera" Hafner, for the controller glyphs
- The runtime harness and launcher come from [Wave Race 64: Recompiled](https://github.com/danielgomesvieira2000/wave-race-64-recomp), and the single-player controls setup from [Rayman 2: Recompiled](https://github.com/danielgomesvieira2000/rayman-2-the-great-escape-recomp)

Every third-party component and its license is listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## AI use

This project was made with AI. Its code, patches, tools, documentation and
artwork were written by Claude (Anthropic) in Claude Code, directed and tested
by Daniel Gomes Vieira. The libraries and the decompilation it builds on are the
work of the people credited above.

## License

The project's own code is [MIT](LICENSE). A built executable links
N64ModernRuntime (GPL-3.0), so a distributed binary is a GPL-3.0 combined work
whose source is this repository. The executable contains the game's code,
recompiled from a dump; Pilotwings 64 is the property of Nintendo.
