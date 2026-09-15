#!/usr/bin/env bash
# Build the Linux port. On the first build, pass your dump.
#
#   bash tools/setup_linux.sh
#   bash tools/build_linux.sh "/path/to/Pilotwings 64 (USA).z64"
#   ./build-linux/Pilotwings64Recomp
#
# Afterwards, source-only rebuilds need no argument: the generated sources are
# already there and only change when the dump or recomp/pilotwings64.us.toml
# does. After changing a patch in patches/, run python3 tools/build_patches.py
# before this.
#
# Environment:
#   PW64_BUILD_DIR   where to build (default build-linux)
#   PW64_JOBS        parallelism (default: the number of processors)
#   PW64_CC, PW64_CXX   the compiler, instead of the newest Clang found
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

if [ "$(uname -s)" != Linux ]; then
    echo "This script is for Linux. On Windows, see docs/BUILDING.md." >&2
    exit 1
fi

BUILD_DIR="${PW64_BUILD_DIR:-build-linux}"
JOBS="${PW64_JOBS:-$(nproc)}"

for tool in cmake ninja python3; do
    command -v "$tool" > /dev/null || {
        echo "$tool is not on PATH. Run: bash tools/setup_linux.sh" >&2
        exit 1
    }
done

# Which Clang. Debian and Ubuntu install it as clang-21, clang-18 and so on,
# and the unversioned clang/clang++ names come from a separate metapackage that
# is often absent. Take an explicit choice first, then the plain names, then the
# highest version number present.
if [ -n "${PW64_CC:-}" ] && [ -n "${PW64_CXX:-}" ]; then
    CC="$PW64_CC"
    CXX="$PW64_CXX"
elif command -v clang > /dev/null && command -v clang++ > /dev/null; then
    CC=clang
    CXX=clang++
else
    CC=""
    for candidate in $(ls /usr/bin/clang-[0-9]* 2> /dev/null | sort -V -r); do
        version="${candidate##*/clang-}"
        case "$version" in
            *[!0-9]*) continue ;;   # clang-format-21, clang-tidy-21, and friends
        esac
        if [ -x "/usr/bin/clang++-$version" ]; then
            CC="$candidate"
            CXX="/usr/bin/clang++-$version"
            break
        fi
    done
    if [ -z "$CC" ]; then
        echo "No Clang found. Install one -- apt install clang -- or set PW64_CC" >&2
        echo "and PW64_CXX to the compiler you want to use." >&2
        exit 1
    fi
fi
echo "compiler: $CC / $CXX"

# --------------------------------------------------------------- patches ----
# Idempotent, and re-applied on every build: they patch submodules, and a
# submodule update reverts them silently.
echo "=== submodule patches ==="
python3 tools/patch_all.py

# ------------------------------------------------------- generated sources ----
if [ $# -gt 0 ]; then
    echo
    echo "=== recompilers ==="
    bash tools/wsl_build_recompiler.sh
    echo
    python3 tools/generate_game.py "$1"
elif [ ! -f RecompiledFuncs/funcs.h ] || [ ! -f RecompiledPatches/patches.c ]; then
    echo "First build: pass your dump." >&2
    echo "    bash tools/build_linux.sh \"/path/to/Pilotwings 64 (USA).z64\"" >&2
    exit 1
fi

# ------------------------------------------------------------ configure ----
# CMAKE_POLICY_VERSION_MINIMUM is for CMake 4, which refuses the
# cmake_minimum_required(VERSION <3.5) still declared under lib/RT64/src/contrib.
echo
echo "=== configure ==="
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DPW64_WITH_RUNTIME=ON -DPW64_WITH_RECOMPILED=ON -DPW64_WITH_FRONTEND=ON

echo
echo "=== build ==="
cmake --build "$BUILD_DIR" --target Pilotwings64Recomp -j "$JOBS"

echo
echo "Built: $BUILD_DIR/Pilotwings64Recomp"
echo "Settings and saves live in \${XDG_DATA_HOME:-~/.local/share}/Pilotwings64Recomp."
