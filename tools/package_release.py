#!/usr/bin/env python3
"""Stage a built Linux tree into a release archive.

The Windows equivalent is tools/package_release.ps1, and this makes the same
promises. What goes in: the executable, the assets the menus draw from, a
launcher, a note on the distribution packages it needs, this project's LICENSE,
the third-party notices, the README and the license texts listed in
tools/third_party_licenses.txt. The libraries it needs (SDL2, Vulkan, GTK3,
FreeType) are the distribution's, which is why the archive says so rather than
shipping copies that would go stale.

What it is not: a ROM. The executable contains the game's code, statically
recompiled, and none of its assets, which are read from the player's own dump.
The script refuses to continue if it finds a dump or a save in the staging
directory, and refuses to overwrite an archive that already exists -- a
published checksum should never quietly start describing different bytes.

    python3 tools/package_release.py --version 0.1.0 [--build-dir build-linux]
"""

import argparse
import hashlib
import platform
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = "Pilotwings64Recomp"

# Anything that could be part of someone's copy of the game, or their progress
# in it.
FORBIDDEN_SUFFIXES = {".z64", ".n64", ".v64", ".rom", ".bin",
                      ".eep", ".sra", ".fla", ".mpk", ".srm"}

DOCS = ["LICENSE", "THIRD_PARTY_NOTICES.md", "README.md"]
LICENSE_MANIFEST = ROOT / "tools" / "third_party_licenses.txt"

LAUNCHER = """#!/usr/bin/env bash
# Pilotwings 64: Recompiled
#
# The libraries this needs come from your distribution, not from this archive:
#
#   Debian, Ubuntu   libsdl2-2.0-0 libvulkan1 libgtk-3-0 libfreetype6
#                    mesa-vulkan-drivers (or your GPU vendor's Vulkan driver)
#   Fedora           SDL2 vulkan-loader gtk3 freetype mesa-vulkan-drivers
#   Arch             sdl2 vulkan-icd-loader gtk3 freetype2 + a Vulkan driver
#
# Settings and saves live in $XDG_DATA_HOME/Pilotwings64Recomp, or
# ~/.local/share/Pilotwings64Recomp. Put a file called portable.txt next to this
# script to keep them here instead.
set -euo pipefail
cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
exec ./Pilotwings64Recomp "$@"
"""

NOTES = """Pilotwings 64: Recompiled -- Linux x86-64
=========================================

Run ./Pilotwings64Recomp.sh and pick your own Pilotwings 64 (USA) dump in the
launcher. No dump is included; see README.md.

Runtime dependencies, from your distribution:

    Debian, Ubuntu   libsdl2-2.0-0 libvulkan1 libgtk-3-0 libfreetype6
                     mesa-vulkan-drivers, or your GPU vendor's Vulkan driver
    Fedora           SDL2 vulkan-loader gtk3 freetype mesa-vulkan-drivers
    Arch             sdl2 vulkan-icd-loader gtk3 freetype2 + a Vulkan driver

RT64 renders through Vulkan here, so a working Vulkan driver is required;
`vulkaninfo --summary` should name your GPU.

Settings and saves live in $XDG_DATA_HOME/Pilotwings64Recomp, or
~/.local/share/Pilotwings64Recomp. A file called portable.txt beside the
executable keeps them next to it instead.
"""


def fail(message):
    raise SystemExit(f"error: {message}")


def copy_licenses(destination: Path):
    """The license texts from tools/third_party_licenses.txt. A missing source
    stops packaging rather than shipping without it."""
    destination.mkdir(parents=True, exist_ok=True)
    for line in LICENSE_MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        name, source = line.split("\t", 1)
        path = ROOT / source
        if not path.is_file():
            fail(f"missing license text {source} ({LICENSE_MANIFEST.relative_to(ROOT)})")
        shutil.copy2(path, destination / name)


def check_no_game_data(stage: Path):
    found = [p for p in stage.rglob("*") if p.is_file() and p.suffix.lower() in FORBIDDEN_SUFFIXES]
    if found:
        for p in found:
            print(f"refusing to package {p}", file=sys.stderr)
        fail("game data found in the staging directory")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def split_debug_info(binary: Path, symbols: Path) -> bool:
    """Move the DWARF out of the executable, where an objcopy exists: it is
    several times the size of the code, and only a crash report needs it."""
    objcopy = shutil.which("llvm-objcopy") or shutil.which("objcopy")
    if objcopy is None:
        return False
    subprocess.run([objcopy, "--only-keep-debug", str(binary), str(symbols)], check=True)
    subprocess.run([objcopy, "--strip-debug", f"--add-gnu-debuglink={symbols}", str(binary)], check=True)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", required=True, help="e.g. 0.1.0")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-linux")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    if platform.system() != "Linux":
        fail("this packages the Linux build. On Windows use tools/package_release.ps1.")

    build_dir = args.build_dir.resolve()
    binary_source = build_dir / EXE
    if not binary_source.is_file():
        fail(f"no executable at {binary_source} -- build first (see docs/BUILDING.md)")
    if not (build_dir / "assets" / "recomp.rcss").is_file():
        fail(f"the frontend's assets are missing from {build_dir / 'assets'}")

    name = f"{EXE}-{args.version}-linux-{platform.machine()}"
    archive = args.out_dir / f"{name}.tar.gz"
    symbols_archive = args.out_dir / f"{name}-debug-symbols.tar.gz"
    if archive.exists():
        fail(f"{archive} already exists. Bump --version or delete it deliberately.")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stage = args.out_dir / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    binary = stage / EXE
    shutil.copy2(binary_source, binary)
    binary.chmod(0o755)
    shutil.copytree(build_dir / "assets", stage / "assets")
    launcher = stage / f"{EXE}.sh"
    launcher.write_text(LAUNCHER, encoding="utf-8")
    launcher.chmod(0o755)
    (stage / "README-LINUX.txt").write_text(NOTES, encoding="utf-8")
    for doc in DOCS:
        source = ROOT / doc
        if not source.is_file():
            fail(f"missing {doc}")
        shutil.copy2(source, stage / source.name)
    copy_licenses(stage / "licenses")

    check_no_game_data(stage)

    symbols = stage / f"{EXE}.debug"
    if split_debug_info(binary, symbols):
        with tarfile.open(symbols_archive, "w:gz") as tar:
            tar.add(symbols, arcname=f"{name}/{symbols.name}")
        symbols.unlink()
        print(f"wrote {symbols_archive}")
    else:
        print("note: no objcopy found; debug info stays in the executable")

    with tarfile.open(archive, "w:gz") as tar:
        tar.add(stage, arcname=name)

    print(f"wrote {archive}")
    print(f"sha256 {sha256(archive)}")


if __name__ == "__main__":
    main()
