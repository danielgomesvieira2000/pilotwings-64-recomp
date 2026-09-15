#!/usr/bin/env bash
# One-time preparation for a Linux build: the distribution packages and the
# submodules.
#
# What each package is for:
#
#   clang, lld            the port (N64Recomp's output is validated against
#                         Clang; GCC's optimizer has miscompiled recompiled
#                         code), and the C patches, compiled for MIPS.
#   cmake, ninja-build, pkg-config   the build.
#   gcc, make, rsync, python3, python3-venv, binutils-mips-linux-gnu
#                         the decompilation's build: its IDO 5.3 toolchain,
#                         splat, and the MIPS assembler and linker.
#   libsdl2-dev           the window, the pad and the audio device.
#   libfreetype-dev       RmlUi's font engine, under recompui.
#   libgtk-3-dev          nativefiledialog-extended, the dump picker.
#   libvulkan-dev         RT64's only renderer on Linux.
#   mesa-vulkan-drivers   a driver to run it. Skip on a machine with the
#                         proprietary NVIDIA or AMD stack already.
#   vulkan-tools          vulkaninfo, for when the renderer will not start.
#
# Nothing is installed without asking: with no arguments the script prints what
# is missing and the command that installs it. Pass --install to run it.
#
#   bash tools/setup_linux.sh [--install]
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PACKAGES=(
    clang lld cmake ninja-build pkg-config git
    gcc make rsync python3 python3-venv binutils-mips-linux-gnu
    libsdl2-dev libfreetype-dev libgtk-3-dev
    libvulkan-dev vulkan-tools mesa-vulkan-drivers
)

install=0
[ "${1:-}" = "--install" ] && install=1

# ------------------------------------------------------------- packages ----
if command -v dpkg-query > /dev/null; then
    missing=()
    for p in "${PACKAGES[@]}"; do
        if dpkg-query -W -f='${Status}' "$p" 2> /dev/null | grep -q "install ok installed"; then
            continue
        fi
        # A versioned toolchain counts. Debian and Ubuntu install clang-21 and
        # friends without the unversioned metapackage, and that is a complete
        # toolchain -- build_linux.sh and build_patches.py find it by version.
        if [ "$p" = clang ] && ls /usr/bin/clang++-[0-9]* > /dev/null 2>&1; then
            continue
        fi
        if [ "$p" = lld ] && ls /usr/bin/ld.lld-[0-9]* /usr/lib/llvm-*/bin/ld.lld > /dev/null 2>&1; then
            continue
        fi
        missing+=("$p")
    done

    if [ ${#missing[@]} -eq 0 ]; then
        echo "packages: all present"
    elif [ "$install" -eq 1 ]; then
        echo "installing: ${missing[*]}"
        sudo apt-get update
        sudo apt-get install -y "${missing[@]}"
    else
        echo "missing packages: ${missing[*]}"
        echo
        echo "Install them with:"
        echo "    sudo apt-get install ${missing[*]}"
        echo "or re-run this script as: bash tools/setup_linux.sh --install"
        echo
    fi
else
    echo "Not a dpkg-based distribution -- install the equivalents of these yourself:"
    printf '    %s\n' "${PACKAGES[@]}"
    echo
fi

# ------------------------------------------------------------ submodules ----
echo "=== submodules ==="
git -C "$REPO" submodule update --init --recursive

echo
echo "Ready. Next:"
echo "    bash tools/build_linux.sh '/path/to/Pilotwings 64 (USA).z64'"
