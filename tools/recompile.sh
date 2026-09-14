#!/usr/bin/env bash
# Phase 02: run N64Recomp and RSPRecomp over the ELF and the dump.
#
# The recompilers run under Linux (WSL on Windows). The Windows build of
# N64Recomp has died with 0xC0000409 -- a stack overflow under Windows' 1 MB
# default stack -- partway through writing its output, leaving funcs.h truncated
# mid-token. Truncated output looks like success until it is compiled, so the
# end marker is checked below.
#
# Run tools/wsl_build_recompiler.sh first, and tools/build_elf.sh before that.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO/lib/N64ModernRuntime/N64Recomp/build-linux"

for tool in N64Recomp RSPRecomp; do
    if [ ! -x "$BIN/$tool" ]; then
        echo "$tool is not built -- run tools/wsl_build_recompiler.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$REPO/pilotwings64.us.elf" ]; then
    echo "pilotwings64.us.elf is missing -- run tools/build_elf.sh <dump> first" >&2
    exit 1
fi

cd "$REPO"
rm -rf RecompiledFuncs
mkdir -p RecompiledFuncs

echo "=== N64Recomp: the game ==="
"$BIN/N64Recomp" recomp/pilotwings64.us.toml 2> "$REPO/RecompiledFuncs/recompile.log" || {
    grep -v '^\[WARN\]' "$REPO/RecompiledFuncs/recompile.log" | tail -20 >&2
    exit 1
}
grep -v '^\[WARN\]' RecompiledFuncs/recompile.log | tail -5 || true

tail -1 RecompiledFuncs/funcs.h | grep -q '#endif' || {
    echo "funcs.h is truncated -- the recompiler died mid-write" >&2
    exit 1
}

echo "=== reference symbols for the patches (dump.toml, data_dump.toml) ==="
# The C patches are recompiled against these: they name every function and
# data symbol in the game, so a patch can call and read the game by name.
mkdir -p RecompiledFuncs/context
(cd RecompiledFuncs/context && "$BIN/N64Recomp" ../../recomp/pilotwings64.us.toml --dump-context > /dev/null)

echo "=== declarations for runtime-provided libultra ==="
python3 tools/gen_reimplemented_decls.py

echo "=== RSPRecomp: the audio microcode ==="
"$BIN/RSPRecomp" recomp/aspMain.us.toml

echo
echo "files        : $(ls RecompiledFuncs/*.c | wc -l)"
echo "declarations : $(grep -c ';' RecompiledFuncs/funcs.h || true)"
