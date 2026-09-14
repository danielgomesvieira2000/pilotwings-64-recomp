"""Turn a Pilotwings 64 dump into the generated sources the port compiles.

This is phases 01 and 02 in one command: verify the dump, build the
decompilation's matching ELF from it, run N64Recomp and RSPRecomp, and build
the C patches. Everything it writes -- `pilotwings64.us.elf`,
`pilotwings64.us.z64`, `RecompiledFuncs/`, `RecompiledPatches/` -- is derived
from the dump and is not committed. docs/BUILDING.md has the step-by-step
version.

The dump may be a `.z64`, `.n64` or `.v64`, or a ZIP holding exactly one of
those. Byte-swapped images are converted; anything that is not Pilotwings 64
(USA) is rejected here, rather than producing failures later that look like
tooling bugs.

The shell steps need Linux tools (the decompilation's IDO compiler, MIPS
binutils, the recompilers), which on Windows means WSL; on Linux and macOS
they run natively.

    python tools/generate_game.py "Pilotwings 64 (USA).z64"
"""

import argparse
import hashlib
import os
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RECOMPILER_DIR = REPO / "lib" / "N64ModernRuntime" / "N64Recomp" / "build-linux"

SHA1 = "ec771aedf54ee1b214c25404fb4ec51cfd43191a"

MAGIC_Z64 = bytes.fromhex("80371240")
MAGIC_N64 = bytes.fromhex("37804012")
MAGIC_V64 = bytes.fromhex("40123780")


def run(*args, cwd=REPO):
    print("+", " ".join(str(a) for a in args), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def wsl_path(path):
    out = subprocess.run(["wsl", "-d", "Ubuntu", "--", "wslpath", "-a", str(path).replace("\\", "/")],
                         capture_output=True, text=True, check=True)
    return out.stdout.strip()


def run_bash(script, *args):
    """Run one of the tools/*.sh scripts, through WSL on Windows."""
    if os.name == "nt":
        run("wsl", "-d", "Ubuntu", "--", "bash", wsl_path(REPO / script),
            *[wsl_path(a) if isinstance(a, Path) else a for a in args])
    else:
        run("bash", REPO / script, *args)


def read_rom(path: Path) -> bytes:
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            entries = [n for n in archive.namelist()
                       if n.lower().endswith((".z64", ".n64", ".v64"))]
            if len(entries) != 1:
                sys.exit(f"The ZIP holds {len(entries)} ROMs; it must hold exactly one.")
            print(f"reading {entries[0]} from the archive")
            return archive.read(entries[0])
    return path.read_bytes()


def to_big_endian(data: bytes) -> bytes:
    head = data[:4]
    if head == MAGIC_Z64:
        return data
    if head == MAGIC_N64:
        print("byte-swapped (.n64) image: converting")
        return bytes(v for pair in zip(data[1::2], data[::2]) for v in pair)
    if head == MAGIC_V64:
        print("word-swapped (.v64) image: converting")
        return b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))
    sys.exit("Not an N64 ROM: no recognised header magic.")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path, help=".z64, .n64, .v64 or a ZIP holding one")
    parser.add_argument("--skip-elf", action="store_true",
                        help="reuse pilotwings64.us.elf instead of rebuilding it")
    args = parser.parse_args()

    if not args.rom.is_file():
        sys.exit(f"No such file: {args.rom}")

    data = to_big_endian(read_rom(args.rom))
    digest = hashlib.sha1(data).hexdigest()
    if digest != SHA1:
        sys.exit(f"This is not Pilotwings 64 (USA).\n"
                 f"  sha1 of what you gave: {digest}\n"
                 f"  sha1 required:         {SHA1}\n"
                 "The European and Japanese releases are laid out differently and will not work.")
    print(f"dump verified: {digest}")

    if not (RECOMPILER_DIR / "N64Recomp").is_file() or not (RECOMPILER_DIR / "RSPRecomp").is_file():
        sys.exit("The recompiler is not built. Run:\n"
                 "  bash tools/wsl_build_recompiler.sh   (wsl bash ... on Windows)")

    # The dump, big-endian, where the RSP config and the ELF build expect it.
    rom_copy = REPO / "pilotwings64.us.z64"
    rom_copy.write_bytes(data)

    if not args.skip_elf or not (REPO / "pilotwings64.us.elf").is_file():
        print("\n=== phase 01: build the ELF from the decompilation ===")
        run_bash("tools/build_elf.sh", rom_copy)

    print("\n=== phase 02: recompile the game and the audio microcode ===")
    run_bash("tools/recompile.sh")

    patches = REPO / "tools" / "build_patches.py"
    if patches.is_file():
        print("\n=== the C patches ===")
        run(sys.executable, patches)

    print("\nGenerated sources are in RecompiledFuncs/. Configure and build next.")


if __name__ == "__main__":
    main()
