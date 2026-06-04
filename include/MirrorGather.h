#pragma once

#include "AnimationQuery.h"
#include "Buffer.h"
#include "Camera.h"
#include "DensityGrid.h"
#include "MaterialLibrary.h"
#include "Object.h"

#include <cstddef>
#include <memory>
#include <vector>

// Storage pivot M3: the MIRROR GATHER over the quantized density grid.
//
// The direct splat (Worker::splatToCamera) leaves delta/mirror pixels black —
// a delta BRDF has zero probability of a photon landing exactly on the camera
// direction, so the forward splat can't render specular surfaces. This pass
// fills those black pixels: for each camera pixel whose first visible surface is
// a delta material, it EXTENDS the camera ray along the perfect reflection
// (recursively through chained mirrors, reusing the Wave 4c specular ray-
// extension idea) until it reaches a NON-DELTA surface, then looks up the
// DENSITY GRID at that point. The cell's accumulated irradiance, multiplied by
// the reflected surface's diffuse BRDF (albedo/pi for Lambertian — view-
// independent), is the radiance the mirror reflects toward the camera.
//
// Reflections therefore come from the COMPACT grid (bounded by occupied cells),
// not from per-photon records. They are coarser than the old per-photon gather:
// the reflected color is quantized to the grid cell size, so a coarser cell
// yields blockier reflections and a finer cell yields sharper ones.
//
// This pass only WRITES delta pixels (and pixels seen only through mirrors). It
// does not touch the direct (non-delta first-hit) pixels the splat already
// produced, so it composites cleanly into the splat buffer.

namespace MirrorGather
{

struct Result
{
    size_t pixelsDelta = 0;      // camera pixels whose first visible surface is delta
    size_t pixelsReflected = 0;  // delta pixels that resolved to a grid lookup with energy
    size_t pixelsBlack = 0;      // delta pixels that stayed black (no non-delta hit / empty cell)
    double maxRadiance = 0.0;    // peak per-pixel reflected luminance (pre-tonemap), for diagnostics
    double sumRadiance = 0.0;    // sum of per-pixel reflected luminance (pre-tonemap), for diagnostics
};

// Run the mirror gather, compositing reflected radiance into `buffer` at the
// delta pixels. `buffer` already holds the direct splat (non-delta pixels) and
// is sized to the camera resolution; this only adds to currently-black delta
// pixels. `photonsPerLight` is N (the grid lookup's 1/N normalization).
// `workerCount` parallelizes the pixel loop. Pixels seen directly through a
// non-delta surface are skipped (the splat owns them).
Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const DensityGrid& grid,
           const MaterialLibrary& materials,
           const AnimationQuery* animation,
           double photonsPerLight,
           size_t workerCount,
           Buffer& buffer);

}  // namespace MirrorGather
