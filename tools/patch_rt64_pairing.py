"""Make RT64 count how well frame interpolation pairs transforms. Idempotent.

RT64 draws the frames between two game frames by pairing each transform with
the previous frame's and interpolating; a transform that finds no pair is drawn
where the newer frame puts it and holds there, stepping at the game's rate while
everything around it glides. Nothing reports how often that happens, so without
this every change to the port's matrix groups (patches/interpolation.c) would be
argued rather than measured -- and a capture cannot settle it, because a window
capture on a laptop saves 30-55 of the 60 frames a second and its timing jitter
is larger than the effect.

The counters, per frame: world transforms; those the game asked RT64 not to
interpolate (matrix groups with the ignore id -- 2D, effects -- which RT64 leaves
unpaired on purpose, so they are counted apart); those that otherwise found no
pair; and those of the unpaired whose matrix appears nowhere in the previous
frame (the ones that cost something: moving or new). The port reads them through
RT64_GetTransformPairing and prints rates under PW64_PAIRING=1.

Carried over from Wave Race 64: Recompiled's tools/patch_rt64.py, where it was
written; only the counters are taken, not that port's pairing heuristics, which
this port replaces with explicit matrix groups.

    python tools/patch_rt64_pairing.py
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GAME_FRAME = REPO / "lib" / "RT64" / "src" / "hle" / "rt64_game_frame.cpp"

MARKER = "pw64PairingIgnored"
# The first version of these counters, without the ignored count.
OLD_MARKER = "pw64PairingFrames"

EDITS = [
    (
        '#include "xxHash/xxh3.h"\n',
        '#include "xxHash/xxh3.h"\n'
        "\n"
        "// pw64: for the transform-pairing counters below.\n"
        "#include <unordered_set>\n",
    ),
    (
        "namespace RT64 {\n    // GameFrame\n",
        "// pw64: running totals of the transform pairing, read by the port through the\n"
        "// accessor below. Written on the workload thread and read on the game thread\n"
        "// without synchronisation, which is enough for a counter only ever reported\n"
        "// as a rate over seconds.\n"
        "static uint64_t pw64PairingFrames = 0;\n"
        "static uint64_t pw64PairingTotal = 0;\n"
        "static uint64_t pw64PairingIgnored = 0;\n"
        "static uint64_t pw64PairingUnpaired = 0;\n"
        "static uint64_t pw64PairingUnpairedMoved = 0;\n"
        "\n"
        'extern "C" void RT64_GetTransformPairing(unsigned long long *frames, unsigned long long *total,\n'
        "                                         unsigned long long *ignored, unsigned long long *unpaired,\n"
        "                                         unsigned long long *unpairedMoved) {\n"
        "    *frames = pw64PairingFrames;\n"
        "    *total = pw64PairingTotal;\n"
        "    *ignored = pw64PairingIgnored;\n"
        "    *unpaired = pw64PairingUnpaired;\n"
        "    *unpairedMoved = pw64PairingUnpairedMoved;\n"
        "}\n"
        "\n"
        "namespace RT64 {\n    // GameFrame\n",
    ),
    (
        "        matchScenes(perspectiveScenes, prevFrame.perspectiveScenes);\n"
        "        matchScenes(orthographicScenes, prevFrame.orthographicScenes);\n",
        "        matchScenes(perspectiveScenes, prevFrame.perspectiveScenes);\n"
        "        matchScenes(orthographicScenes, prevFrame.orthographicScenes);\n"
        "\n"
        "        // pw64: count what failed to pair (tools/patch_rt64_pairing.py).\n"
        "        {\n"
        "            thread_local std::unordered_set<uint64_t> pw64PrevMatrices;\n"
        "            pw64PrevMatrices.clear();\n"
        "            for (uint32_t w : workloads) {\n"
        "                const GameFrameMap::WorkloadMap &map = frameMap.workloads[w];\n"
        "                if (!map.mapped) {\n"
        "                    continue;\n"
        "                }\n"
        "\n"
        "                const Workload &prevWorkload = workloadQueue.workloads[map.prevWorkloadIndex];\n"
        "                for (const interop::float4x4 &m : prevWorkload.drawData.worldTransforms) {\n"
        "                    pw64PrevMatrices.insert(XXH3_64bits(&m, sizeof(m)));\n"
        "                }\n"
        "            }\n"
        "\n"
        "            uint32_t total = 0, ignored = 0, unpaired = 0, unpairedMoved = 0;\n"
        "            for (uint32_t w : workloads) {\n"
        "                const GameFrameMap::WorkloadMap &map = frameMap.workloads[w];\n"
        "                const Workload &curWorkload = workloadQueue.workloads[w];\n"
        "                for (size_t t = 0; t < map.transforms.size(); t++) {\n"
        "                    total++;\n"
        "                    if (map.transforms[t].mapped) {\n"
        "                        continue;\n"
        "                    }\n"
        "\n"
        "                    const uint32_t groupIndex = curWorkload.drawData.worldTransformGroups[t];\n"
        "                    if (curWorkload.drawData.transformGroups[groupIndex].matrixId == G_EX_ID_IGNORE) {\n"
        "                        ignored++;\n"
        "                        continue;\n"
        "                    }\n"
        "\n"
        "                    unpaired++;\n"
        "                    const interop::float4x4 &m = curWorkload.drawData.worldTransforms[t];\n"
        "                    if (pw64PrevMatrices.find(XXH3_64bits(&m, sizeof(m))) == pw64PrevMatrices.end()) {\n"
        "                        unpairedMoved++;\n"
        "                    }\n"
        "                }\n"
        "            }\n"
        "\n"
        "            pw64PairingFrames++;\n"
        "            pw64PairingTotal += total;\n"
        "            pw64PairingIgnored += ignored;\n"
        "            pw64PairingUnpaired += unpaired;\n"
        "            pw64PairingUnpairedMoved += unpairedMoved;\n"
        "        }\n",
    ),
]


def main() -> int:
    if not GAME_FRAME.is_file():
        sys.exit(f"missing {GAME_FRAME}. Run: git submodule update --init --recursive")
    text = GAME_FRAME.read_text(encoding="utf-8")
    if MARKER not in text and OLD_MARKER in text:
        # Nothing else patches this file: restore it and apply the current version.
        subprocess.run(["git", "-C", str(GAME_FRAME.parents[2]), "checkout", "--", "src/hle/rt64_game_frame.cpp"],
                       check=True)
        print("  rt64_game_frame.cpp restored (older pairing counters)")
        text = GAME_FRAME.read_text(encoding="utf-8")
    if MARKER in text:
        print("  rt64_game_frame.cpp already patched (pairing counters)")
        return 0
    crlf = "\r\n" in text
    if crlf:
        text = text.replace("\r\n", "\n")
    for anchor, replacement in EDITS:
        if anchor not in text:
            sys.exit(f"anchor not found in {GAME_FRAME.name}; upstream has changed:\n  {anchor.splitlines()[0]}")
        text = text.replace(anchor, replacement, 1)
    if crlf:
        text = text.replace("\n", "\r\n")
    GAME_FRAME.write_text(text, encoding="utf-8", newline="")
    print("  rt64_game_frame.cpp patched (pairing counters)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
