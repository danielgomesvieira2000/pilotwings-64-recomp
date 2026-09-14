# Findings: phases 00 to 04 -- from a dump to the title screen

The working record, kept as it happened, including what did not work first.
The reference versions of these facts are in [../PORTING.md](../PORTING.md) and
[../GAME-INTERNALS.md](../GAME-INTERNALS.md).

## 00 -- Skeleton

- Submodules are pinned to the revisions Wave Race 64: Recompiled 1.0.2 ships
  on: N64ModernRuntime `cdf5abb`, RT64 `5473732`, RecompFrontend `b1a1477`.
  The decompilation is pinned at `e572023`.
- `git submodule update --init --recursive` after `git submodule add` checks
  each submodule out at the commit *recorded in the index*, which for a freshly
  added submodule is the remote's HEAD, not the commit checked out a moment
  earlier. RT64 came back at upstream `4337374` instead of `5473732`; re-pinned
  and staged explicitly.
- The dump's XXH3-64 is `0x887AB02583C90111`. The function was confirmed by
  hashing Wave Race 64's dump and getting the value that port pins.

## 01 -- The ELF

- The decompilation's `make` with `RECOMP_BUILD=1` rebuilds a byte-identical
  ROM. That switch only removes `static` from the game's own file-local
  functions and data; libultra's statics stay static (see 04).
- **CRLF breaks IDO.** Git for Windows checks the submodule out with CRLF, and
  IDO's `cfe` then fails on `gbi.h` with `Illegal macro parameter name` -- the
  `\r` before each macro continuation. `tools/build_elf.sh` re-checks the
  decompilation and its own submodules out with `core.autocrlf=false`
  (`git checkout-index --force --all`). `git rm --cached -r .` is the usual
  recipe and does not work here: it refuses in a repository with submodules
  ("please stage your changes to .gitmodules").
- **A blanket `*.o` exclude breaks IDO too.** The mirror into the Linux
  filesystem first excluded object files, and IDO's distribution ships
  `crt1.o` and friends that the compiler wrapper links against:
  `No rule to make target 'build/5.3/out/crt1.o'`.
- The build runs in a mirror under `~/.cache` on WSL, because a tree under
  `/mnt/c` crosses the 9P boundary on every file operation.

## 02 -- First recompile

- **The first recompile succeeded outright**: 3,130 functions, no unresolved
  call targets, no manual function boundaries. That is the difference a real
  symbol table makes; Wave Race 64 needed a splat padding pipeline, a call
  target scan and relocation fixes to get here.
- Three instruction patches, found by gcsmith's Pilotwings64Recomp and checked
  against the ELF here: `_uvScDlistRecover` writes `SP_STATUS_REG`
  (`0x8022B678`), and `func_80231A10` reads and reloads CP0 Count
  (`0x80231A10`, `0x80231A18`). The latter has no caller anywhere in the
  decompilation.
- The audio microcode is byte-for-byte Wave Race 64's -- all 0xE20 bytes of
  text and the sixteen-entry command table -- so its load address (0x1080) and
  indirect targets carried over unchanged.

## 03 -- Harness

- **Two libultra functions nobody implements**: `osPiReadIo` and
  `osPiRawReadIo`, both in librecomp's ignored list and neither defined. The
  link fails on them. `bootproc` reads 64 bytes at cartridge offset `0xFFB000`
  looking for a development cartridge's marker; `_uvMediaCopy` finishes a
  misaligned copy with PIO. Both now read from the dump (zero past its end).
- **The audio command-list scratch moved.** Wave Race 64's port copies each
  audio command list to `0x807E0000`, the top of the 8 MB the runtime reports,
  because that game never reads `osMemSize`. Pilotwings 64 does
  (`uvLevelInit` sets its heap end to `osMemSize + 0x80000000`), so that region
  is the game's heap here. The copy lives at `0x80F00000`, below librecomp's mod
  region at `0x81000000` and above the patch data at `0x80801000`.
- The frame-rate probe Wave Race 64 used is registered at `osViSwapBuffer`'s
  address and only works because every call there goes through the function
  lookup. Pilotwings 64's calls are direct, so the probe was removed for now; it
  returns as a C patch.
- PowerShell splits `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` at the dot unless the
  argument is quoted.

## 04 -- Boot

1. **First run: lookup miss at `0x802EBC94`** from `gameUpdate`, which calls
   through a table of state functions. librecomp registers the boot megabyte
   as if ROM `0x1000` onwards were loaded contiguously at the entry point. That
   holds for the kernel segment and not for the app segment: the kernel is
   followed in RAM by `0x79A80` bytes of bss, the boot code copies the app to
   `0x802CA900` itself (`_uvMediaCopy(app_TEXT_START, app_ROM_START, ...)`), and
   librecomp had registered every app function at `0x80250E80`-relative
   addresses. `src/sections.cpp` moves any such section to its linked address
   from the `on_init` hook.
2. **Second run: lookup miss at `0x80235A0C`** from `alAudioFrame`. That is
   `__CSPVoiceHandler`, a `static` function in libultra's `csplayer.c` that is
   only ever reached through a pointer stored in the sequence player. It has
   no ELF symbol, so it was never recompiled. `use_mdebug = true` makes
   N64Recomp read IDO's `.mdebug`, which names every static function: 24 more
   functions (1,885 declarations to 1,909).
3. `use_mdebug` then failed with `Couldn't determine elf section of mdebug info
   for file src/libultra/os/sendmesg.c`. The parser finds a file's section
   through one of its global functions, and `osSendMesg` is reimplemented by
   the runtime and removed from the context. `tools/gen_mdebug_mappings.py`
   maps every source file explicitly (libultra and kernel to `.kernel`, app to
   `.app`).
4. **Third run: the title screen and the attract demo, with sound.** The game
   state transcript reads `TITLE`, then `DEMO_PILOT` with the vehicle and map
   changing as the attract sequence cycles. The 3D is drawn correctly in the
   middle 4:3; RT64's widened frame shows stale framebuffer contents at the
   sides, because the game clears only its own region. That is phase 07's.
