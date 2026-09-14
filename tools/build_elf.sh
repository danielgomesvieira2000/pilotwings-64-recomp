#!/usr/bin/env bash
# Phase 01: build the ELF N64Recomp reads, from the decompilation.
#
# Pilotwings 64 is fully decompiled and the decompilation builds a byte-matching
# ROM. Its ELF therefore carries every function with its real name, address and
# size, which is exactly what N64Recomp's ELF input mode wants -- no splat
# padding, no call-target scan, nothing inferred.
#
# RECOMP_BUILD=1 is the decompilation's own switch for this: it removes `static`
# from file-local functions and data (include/macros.h, STATIC_FUNC and
# STATIC_DATA), so they appear in the symbol table. The code is unchanged, and
# the build still has to match -- `make` checks the SHA-1 at the end, and this
# script refuses an ELF from a build that did not.
#
# The decompilation compiles with IDO 5.3 through ido-static-recomp, which is a
# Linux program, so this runs under WSL on Windows and natively on Linux and
# macOS. On WSL a tree under /mnt/c builds very slowly (every file operation
# crosses the 9P boundary), so the decompilation is mirrored into the Linux
# filesystem first and built there. PW64_DECOMP_WORKDIR overrides where.
#
#   bash tools/build_elf.sh <dump.z64>
#
# Writes pilotwings64.us.elf in the repository root. Nothing it writes is
# committed: the ELF contains the game.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DECOMP="$REPO/lib/Pilotwings64Decomp"
ROM="${1:-}"
SHA1="ec771aedf54ee1b214c25404fb4ec51cfd43191a"

if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "usage: bash tools/build_elf.sh <Pilotwings 64 (USA).z64>" >&2
    exit 1
fi
if [ ! -f "$DECOMP/Makefile" ]; then
    echo "lib/Pilotwings64Decomp is empty. Run: git submodule update --init --recursive" >&2
    exit 1
fi

actual="$(sha1sum "$ROM" | cut -d' ' -f1)"
if [ "$actual" != "$SHA1" ]; then
    echo "This is not Pilotwings 64 (USA)." >&2
    echo "  sha1 of what you gave: $actual" >&2
    echo "  sha1 required:         $SHA1" >&2
    exit 2
fi

# Line endings. Git for Windows installs with core.autocrlf=true, which checks
# every text file out with CRLF -- and IDO's preprocessor reads the \r before a
# macro's line continuation as part of the macro, failing on gbi.h with
# "Illegal macro parameter name". The checkout is renormalised to LF once, in
# the submodule and its own submodules, with autocrlf turned off there so it
# stays that way. The decompilation's files are never edited, only re-checked-out.
if grep -q $'\r' "$DECOMP/include/libultra/PR/gbi.h"; then
    echo "=== the decompilation is checked out with CRLF line endings; re-checking it out with LF ==="
    for dir in "$DECOMP" $(git -C "$DECOMP" submodule foreach --recursive --quiet 'echo "$toplevel/$sm_path"'); do
        git -C "$dir" config core.autocrlf false
        # Rewrites every tracked file from the index, whose blobs are LF.
        git -C "$dir" checkout-index --force --all
    done
fi

# Where to build. A checkout on the Windows side is mirrored into the Linux
# filesystem; anywhere else the submodule is built in place.
WORK="$DECOMP"
case "$REPO" in
    /mnt/*)
        WORK="${PW64_DECOMP_WORKDIR:-$HOME/.cache/pilotwings64recomp/decomp}"
        mkdir -p "$WORK"
        echo "=== mirroring the decompilation to $WORK ==="
        # The build outputs and the tool binaries stay on the Linux side between
        # runs, so only the first build pays for compiling the toolchain. Only
        # generated directories are excluded: IDO's own distribution under
        # tools/ido-static-recomp/ido ships object files (crt1.o and friends)
        # the compiler wrapper links against, so a blanket *.o exclude breaks it.
        rsync -a --delete \
            --exclude 'build/' --exclude '/asm/' --exclude '/bin/' --exclude '/.venv/' \
            --exclude 'baserom.us.z64' --exclude '/tools/n64crc' \
            "$DECOMP/" "$WORK/"
        ;;
esac

cp "$ROM" "$WORK/baserom.us.z64"
cd "$WORK"

# splat and its friends, pinned by the decompilation's requirements.txt.
if [ ! -x .venv/bin/python3 ]; then
    echo "=== creating the Python environment ==="
    python3 -m venv .venv
fi
# shellcheck disable=SC1091
source .venv/bin/activate
if ! python3 -c "import splat" 2>/dev/null; then
    python3 -m pip install -q -r requirements.txt
fi

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== building the decompilation's tools (IDO 5.3, n64crc) ==="
make -s -C tools -j"$JOBS" > .tools-build.log 2>&1 || { tail -20 .tools-build.log >&2; exit 4; }

echo "=== splitting the dump ==="
if [ ! -d asm ] || [ config/us/pilotwings64.us.yaml -nt asm ]; then
    make -s extract > /dev/null
fi

echo "=== building with RECOMP_BUILD=1 ==="
make -s clean > /dev/null
make -j"$JOBS" RECOMP_BUILD=1 2>&1 | grep -E 'OK$|FAILED|Error|error:' || true

# `make` ends with `sha1sum -c`, but a pipeline swallows its status; check the
# result itself rather than trusting that the build got that far.
built="$(sha1sum build/pilotwings64.us.z64 2>/dev/null | cut -d' ' -f1 || true)"
if [ "$built" != "$SHA1" ]; then
    echo "The decompilation did not rebuild a matching ROM (got '${built:-nothing}')." >&2
    echo "Run make in $WORK to see why." >&2
    exit 3
fi

cp build/pilotwings64.us.elf "$REPO/pilotwings64.us.elf"
echo "=== wrote pilotwings64.us.elf (ROM image matches the dump: $SHA1) ==="
