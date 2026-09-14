#!/usr/bin/env python3
"""Measure whether captured frames move every frame or in steps.

    python tools/frame_motion.py shots/run [--region x0,y0,x1,y1] [--threshold 1.0]

Reads the frames tools/capture_frames.py saved (tMMMMMM.jpg), and for each
consecutive pair prints the mean absolute pixel difference, optionally over a
region only. When the game runs slower than the display and nothing is
interpolated, the presented picture changes only when the game produces a frame,
so most consecutive pairs are identical -- two of every three with the game at
20 frames per second on a 60 Hz display. With interpolation every pair differs.
The summary says which fraction of pairs changed, which is the number to compare
between runs.

A capture is not frame-exact (it saves 30-55 of the 60 presented frames a
second on a laptop), so read the fraction, not individual pairs.
"""
import argparse
import glob
import os
import sys

try:
    import cv2
    import numpy as np
except ImportError:
    sys.exit("needs: pip install opencv-python numpy")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--region", default=None, help="x0,y0,x1,y1 in captured pixels")
    ap.add_argument("--threshold", type=float, default=1.0,
                    help="mean difference above which a pair counts as changed")
    ap.add_argument("--quiet", action="store_true", help="print only the summary")
    args = ap.parse_args()

    frames = sorted(glob.glob(os.path.join(args.outdir, "t*.jpg")))
    if len(frames) < 2:
        sys.exit("fewer than two frames")

    region = tuple(int(v) for v in args.region.split(",")) if args.region else None
    previous = None
    changed = 0
    pairs = 0
    diffs = []
    for path in frames:
        image = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if region:
            x0, y0, x1, y1 = region
            image = image[y0:y1, x0:x1]
        image = image.astype(np.int16)
        if previous is not None and previous.shape == image.shape:
            diff = float(np.abs(image - previous).mean())
            diffs.append(diff)
            pairs += 1
            if diff > args.threshold:
                changed += 1
            if not args.quiet:
                print(f"{os.path.basename(path)}  {diff:6.2f}")
        previous = image

    print(f"{changed} of {pairs} consecutive pairs changed ({100.0 * changed / pairs:.0f}%)")
    # Evenness. Motion the renderer interpolates changes the picture by a similar
    # amount every frame; motion that steps with the game changes it a lot on the
    # frames the game produced and little in between. The coefficient of
    # variation of the per-pair differences separates the two when both runs
    # change every frame.
    arr = np.array(diffs)
    print(f"difference per pair: mean {arr.mean():.2f}, coefficient of variation {arr.std() / arr.mean():.2f}")


if __name__ == "__main__":
    main()
