"""Shared helper for the scripts that patch a submodule with a diff.

Most submodule patches here are anchored string replacements. The ones too
large for that are diffs under tools/patches/, applied through apply_patch,
which is idempotent: running it on a tree that already has the patch is a
no-op, and a tree the patch no longer fits is refused without changing anything.

Line endings: the submodules are checked out by Git for Windows with
core.autocrlf=true, so their sources are CRLF in the working tree while the
patch files are LF. `git apply` run from Windows normalises and never notices;
run from WSL it refuses the patch in both directions, so a reverse check reports
"not applied" for a tree where it plainly is. --ignore-whitespace fixes that
without weakening the protection against applying twice, and a marker string in
a patched file settles "already applied" without invoking git at all.
"""

import subprocess
import sys
from pathlib import Path

IGNORE_WS = "--ignore-whitespace"


def apply_patch(submodule: Path, patch: Path, name: str, marker: tuple = None) -> int:
    """Apply `patch` to `submodule` idempotently; returns a process exit code.

    `marker` is an optional (path, string) pair: if the string is already in
    the file, the patch is taken as applied.
    """
    if marker is not None:
        marker_path, marker_text = marker
        if marker_path.is_file() and marker_text in marker_path.read_text(encoding="utf-8"):
            print(f"{name}: already applied")
            return 0
    if not (submodule / ".git").exists() and not (submodule / "CMakeLists.txt").is_file():
        print(f"{name}: {submodule} is missing.\n"
              "Run: git submodule update --init --recursive", file=sys.stderr)
        return 1
    if not patch.is_file():
        print(f"{name}: {patch} is missing.", file=sys.stderr)
        return 1

    def check(*args):
        return subprocess.run(["git", "apply", "--check", IGNORE_WS, *args, str(patch)],
                              cwd=submodule, capture_output=True, text=True)

    if check("--reverse").returncode == 0:
        print(f"{name}: already applied")
        return 0

    forward = check()
    if forward.returncode != 0:
        print(f"{name}: this patch does not match the checkout in {submodule.name}.\n"
              "Nothing has been changed. Either the submodule has moved, or another\n"
              "patch has already touched the same lines -- re-derive the patch rather\n"
              "than forcing it.\n", file=sys.stderr)
        print(forward.stderr, file=sys.stderr)
        return 1

    subprocess.run(["git", "apply", IGNORE_WS, str(patch)], cwd=submodule, check=True)
    print(f"{name}: applied")
    return 0
