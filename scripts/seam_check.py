#!/usr/bin/env python3
"""Decode a render PNG and report black-pixel statistics, with emphasis on the
top wall/ceiling seam region. Used to verify the Cornell-box edge fix.

Usage: seam_check.py <png> [<png2> ...]
"""
import sys
from PIL import Image


def analyze(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    total_black = 0
    black_coords = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r == 0 and g == 0 and b == 0:
                total_black += 1
                black_coords.append((x, y))

    # Row-by-row black counts to locate horizontal seam lines.
    row_black = [0] * h
    for (x, y) in black_coords:
        row_black[y] += 1

    # Identify the rows with the most interior black pixels (exclude the frame
    # border columns where the box doesn't fill the image — black there is the
    # background outside the box, not a seam).
    print(f"== {path}  ({w}x{h}) ==")
    print(f"total black pixels: {total_black}")

    # Top region (ceiling seam lives in the upper third of the box).
    top_rows = [(y, c) for y, c in enumerate(row_black) if c > 0]
    top_rows.sort(key=lambda t: -t[1])
    print("rows with most black pixels (y, count):")
    for y, c in top_rows[:12]:
        print(f"  y={y:4d}  black={c}")
    return total_black, black_coords


if __name__ == "__main__":
    for p in sys.argv[1:]:
        analyze(p)
        print()
