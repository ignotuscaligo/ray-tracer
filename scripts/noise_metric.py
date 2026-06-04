#!/usr/bin/env python3
"""Mean luminance + noise (coefficient of variation) for a render PNG.

Reports:
  - full-image mean luminance (sRGB-linearized, Rec.709 luma)
  - mean luminance + std + coefficient of variation (std/mean) over a flat
    rectangular region (default: a patch of the back wall, upper-center,
    avoiding the spheres), used as the diffuse-indirect noise measure.

Usage: noise_metric.py <png> [x0 y0 x1 y1]
Region coords are fractions of width/height in [0,1]. Default region is a
flat back-wall patch.
"""
import sys
from PIL import Image


def srgb_to_linear(c):
    c = c / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


# Precompute LUT.
_LIN = [srgb_to_linear(i) for i in range(256)]


def luminance(r, g, b):
    # Rec.709 luma on linearized channels.
    return 0.2126 * _LIN[r] + 0.7152 * _LIN[g] + 0.0722 * _LIN[b]


def analyze(path, region):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()

    # Full-image mean luminance.
    total = 0.0
    n = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            total += luminance(r, g, b)
            n += 1
    full_mean = total / n if n else 0.0

    # Flat-region statistics.
    fx0, fy0, fx1, fy1 = region
    x0, y0 = int(fx0 * w), int(fy0 * h)
    x1, y1 = int(fx1 * w), int(fy1 * h)
    vals = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            vals.append(luminance(*px[x, y]))
    rn = len(vals)
    rmean = sum(vals) / rn if rn else 0.0
    var = sum((v - rmean) ** 2 for v in vals) / rn if rn else 0.0
    std = var ** 0.5
    cov = (std / rmean) if rmean > 0 else 0.0

    print(f"== {path} ({w}x{h}) ==")
    print(f"full-image mean luminance: {full_mean:.6f}")
    print(f"flat region [{x0},{y0}]-[{x1},{y1}] ({rn} px):")
    print(f"  mean={rmean:.6f} std={std:.6f} cov(std/mean)={cov:.4f}")
    return full_mean, rmean, cov


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    if len(sys.argv) >= 6:
        region = tuple(float(a) for a in sys.argv[2:6])
    else:
        # Default flat patch: upper-center back wall, above the spheres.
        region = (0.40, 0.20, 0.60, 0.38)
    analyze(path, region)
