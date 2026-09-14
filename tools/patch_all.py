"""Apply every submodule patch this port needs, in order. Idempotent.

Each patch is its own script, with the reason for it at the top; this only runs
them. A submodule update reverts a hand edit silently, which is why none of
these changes are made by hand -- rerun this after any submodule update.

    python tools/patch_all.py
"""

import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent

SCRIPTS = [
    "patch_rsprecomp.py",          # RSPRecomp: indirect jumps ignore the low two bits
    "patch_librecomp.py",          # librecomp: report the caller of a failed lookup
    "patch_runtime_shutdown.py",   # ultramodern: join workers before freeing RDRAM
    "patch_rt64_eventfilter.py",   # RT64: take the SDL event filter back off
    "patch_macos.py",              # RT64's hlsl++: <stdlib.h> for labs on macOS
    "patch_recompinput.py",        # RecompFrontend: assign pads in connection order
]


def main() -> int:
    failed = []
    for script in SCRIPTS:
        path = TOOLS / script
        if not path.is_file():
            continue
        print(f"--- {script}")
        if subprocess.run([sys.executable, str(path)]).returncode != 0:
            failed.append(script)
    if failed:
        print(f"\nfailed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nall submodule patches are in place")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
