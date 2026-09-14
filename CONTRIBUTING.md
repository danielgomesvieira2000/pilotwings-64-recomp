# Contributing

## Never commit game data

No dump, no asset, and nothing derived from the dump goes into this repository:
not the ELF, not `RecompiledFuncs/` or `RecompiledPatches/`, not the
decompilation's extracted files, not saves, not screenshots of copyrighted
screens used as test fixtures. `.gitignore` refuses the usual names; check
`git status` before committing anyway.

## Rules the tree is built on

- **Generated code is never edited.** A problem in `RecompiledFuncs/` is fixed in
  `recomp/*.toml`, in a C patch, or in a tool.
- **Submodules are never edited by hand.** A change to one is an idempotent
  script in `tools/`, added to `tools/patch_all.py`; a submodule update would
  otherwise revert it silently.
- **A replaced game function goes in `ignored`** in
  `recomp/pilotwings64.us.toml`. `tools/build_patches.py` enforces it.
- **Build RelWithDebInfo**, and build after every change.
- **Run the game after every behavioural change and look at it.** The input
  scripts in `tools/scripts/` and `tools/test_run.ps1` make a run repeatable; the
  state transcript says what happened.
- **A change that discovers a fact about the game or the toolchain updates
  [docs/GAME-INTERNALS.md](docs/GAME-INTERNALS.md) or
  [docs/PORTING.md](docs/PORTING.md)** in the same commit. Wrong turns worth
  remembering go in `docs/findings/`.

## Style

Match the surrounding code. C patches that replace a game function start from
the decompilation's C and say so in the comment above; comments explain why, in
full sentences.
