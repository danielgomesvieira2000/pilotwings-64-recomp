# Building

The procedure, from a clean machine to the game running. Why each step is the
way it is lives in [PORTING.md](PORTING.md).

The port builds for **Windows** and **Linux** from this one tree. macOS is not
supported. [Linux](#linux) has a script that does everything; Windows is step by
step, below.

You need your own dump of **Pilotwings 64 (USA)**:
SHA-1 `ec771aedf54ee1b214c25404fb4ec51cfd43191a`. `.z64`, `.n64`, `.v64` or a ZIP
holding one of them. Nothing else works, and nothing derived from the dump is
ever committed.

## Linux

Tested on Ubuntu 26.04 (x86-64) under WSL2, with Clang 21 and Mesa's software Vulkan (llvmpipe): builds from a clean clone, and plays through the menus into a flight at 20-25 frames per second, a limit of software rendering. Not yet run on Linux hardware with a GPU.

```sh
git clone --recurse-submodules https://github.com/danielgomesvieira2000/pilotwings-64-recomp
cd pilotwings-64-recomp
bash tools/setup_linux.sh            # prints the packages that are missing
bash tools/setup_linux.sh --install  # installs them, with sudo
bash tools/build_linux.sh "/path/to/Pilotwings 64 (USA).z64"
./build-linux/Pilotwings64Recomp
```

| Package | Needed by |
|---|---|
| `clang`, `lld` | the port (not GCC, which has miscompiled recompiled code) and the MIPS patches. A versioned `clang-21` counts; `build_linux.sh` picks the newest, and `PW64_CC`/`PW64_CXX` override it |
| `cmake`, `ninja-build`, `pkg-config`, `git` | the build |
| `gcc`, `make`, `rsync`, `python3`, `python3-venv`, `binutils-mips-linux-gnu` | the decompilation's build (IDO 5.3, splat) |
| `libsdl2-dev`, `libfreetype-dev`, `libgtk-3-dev` | the window, input and audio; the menus' fonts; the dump picker |
| `libvulkan-dev`, `mesa-vulkan-drivers`, `vulkan-tools` | RT64 renders through Vulkan on Linux. Skip the Mesa drivers if the NVIDIA or AMD proprietary stack is installed |

The dump is needed on the first build only; after that `bash tools/build_linux.sh`
rebuilds from source. Pass the dump again after changing
`recomp/pilotwings64.us.toml`, and run `python3 tools/build_patches.py` after
changing a patch.

Settings and saves live in `$XDG_DATA_HOME/Pilotwings64Recomp`, or
`~/.local/share/Pilotwings64Recomp`; a `portable.txt` beside the executable
keeps them there instead.

`python3 tools/package_release.py --version 0.1.0` makes the release archive
(see *Packaging*).

## Windows

### Requirements

| Tool | Why |
|---|---|
| Git | the repository and its submodules |
| CMake 3.20+ and Ninja | the build |
| LLVM (clang-cl) | compiling the port; recompiled code is validated against Clang, and **modern GCC is known to miscompile it** |
| Visual Studio 2022 Build Tools (C++ workload) | the Windows SDK and C runtime clang-cl links against |
| Python 3.10+ | the tools in `tools/` |
| WSL with Ubuntu | the decompilation's compiler, both recompilers, the MIPS Clang |

```powershell
winget install --id Git.Git
winget install --id Kitware.CMake
winget install --id Ninja-build.Ninja
winget install --id LLVM.LLVM
winget install --id Python.Python.3.12
winget install --id Microsoft.VisualStudio.2022.BuildTools `
    --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
wsl --install -d Ubuntu
```

`pwsh -File tools/check_toolchain.ps1` reports what is present.

### Inside WSL (Ubuntu)

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git rsync \
    binutils-mips-linux-gnu python3 python3-venv python3-pip \
    clang lld
```

- `build-essential cmake ninja-build`: building N64Recomp and RSPRecomp.
- `binutils-mips-linux-gnu python3-venv rsync`: the decompilation's build
  (it creates its own Python environment for splat).
- `clang lld`: compiling the C patches for MIPS. Any version from 15 on; the
  newest installed is used.

### 1. Clone

```powershell
git clone --recurse-submodules https://github.com/danielgomesvieira2000/pilotwings-64-recomp
cd pilotwings-64-recomp
```

An existing clone without submodules: `git submodule update --init --recursive`.

### 2. Patch the submodules

```powershell
python tools/patch_all.py
```

Idempotent; rerun after any submodule update. Each script says at its top what
it changes and why.

### 3. Build the recompilers

```powershell
wsl -d Ubuntu -- bash tools/wsl_build_recompiler.sh
```

Builds N64Recomp and RSPRecomp into `lib/N64ModernRuntime/N64Recomp/build-linux`.

### 4. Generate the game

```powershell
python tools/generate_game.py "C:\path\to\Pilotwings 64 (USA).z64"
```

This verifies the dump and runs, in order:

1. `tools/build_elf.sh`: builds the decompilation (IDO 5.3, the first time also
   its toolchain) into `pilotwings64.us.elf`, refusing it unless it matches the
   dump. On WSL the build runs in a mirror under `~/.cache`. The first run takes
   several minutes.
2. `tools/recompile.sh`: N64Recomp into `RecompiledFuncs/`, the reference
   symbols, and RSPRecomp's audio microcode.
3. `tools/build_patches.py`: the C patches into `RecompiledPatches/`.

The steps can be run on their own. After changing a patch only step 3 is
needed; after changing `recomp/pilotwings64.us.toml` (for example adding a
function to `ignored`), steps 2 and 3:

```powershell
wsl -d Ubuntu -- bash tools/recompile.sh
python tools/build_patches.py
```

### 5. Configure and build

```powershell
cmake -B build -G Ninja "-DCMAKE_C_COMPILER=clang-cl" "-DCMAKE_CXX_COMPILER=clang-cl" `
    "-DCMAKE_BUILD_TYPE=RelWithDebInfo" "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
    "-DPW64_WITH_RUNTIME=ON" "-DPW64_WITH_RECOMPILED=ON" "-DPW64_WITH_FRONTEND=ON"
cmake --build build --target Pilotwings64Recomp
```

clang-cl finds the Build Tools' SDK and runtime on its own; an ordinary
PowerShell will do. Quote the `-D` arguments: PowerShell otherwise splits `=3.5`
at the dot.

Build **RelWithDebInfo**. A Debug build of a recompiled game is too slow to keep
audio in time.

The `PW64_WITH_*` switches exist so the tree builds at every stage: with all of
them off, CMake builds only the skeleton executable (`--identify`).

### 6. Run

```powershell
build\Pilotwings64Recomp.exe
```

Pick the dump in the launcher. It is copied into the settings directory
(`%LOCALAPPDATA%\Pilotwings64Recomp`), with saves and settings; a dump can also
be given on the command line.

`build\Pilotwings64Recomp.exe --identify <dump>` checks a dump without starting
the game.

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File tools/package_release.ps1 -BuildDir build -Version 0.1.0
```

```sh
python3 tools/package_release.py --version 0.1.0    # Linux, from build-linux
```

On Linux the archive is a `.tar.gz` with the executable, a launcher script and
a note on the distribution packages it needs at run time, with the debug info
split into a second archive. On Windows it stages the executable, the three DLLs it needs, `assets/`, the license texts
listed in `tools/third_party_licenses.txt`, and the notices into `dist/`, zips
them, and zips the debug symbols separately. It refuses to package anything that
looks like a dump or a save. The executable contains the game's code, recompiled;
see the README's *Licensing* before distributing one.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `Illegal macro parameter name` from IDO | CRLF checkout of the decompilation; `build_elf.sh` fixes it, so run the script rather than `make` directly |
| `funcs.h` ends mid-line; compile errors in `RecompiledFuncs` | a Windows build of N64Recomp; use the WSL one (step 3) |
| `No available targets are compatible with triple "mips"` | Windows LLVM; the patches must build through WSL (`build_patches.py` does) |
| `duplicate symbol` for a game function at link | a `RECOMP_PATCH` missing from `ignored`; `build_patches.py` normally stops first |
| `error: undefined symbol` naming a function a patch calls | `RecompiledFuncs` is older than the config; rerun `recompile.sh` then `build_patches.py` |
| Lookup miss at startup | a stale `RecompiledFuncs/`; regenerate (step 4) |
