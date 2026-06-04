#pragma once

#include "AnimationQuery.h"
#include "BounceCloud.h"
#include "Buffer.h"
#include "Camera.h"
#include "HashGrid.h"
#include "MaterialLibrary.h"
#include "Object.h"

#include <cstddef>
#include <memory>
#include <vector>

// Wave 4b: the fixed-footprint GATHER pass.
//
// This REPLACES the Wave 1-3 forward splat (Worker::processFinalHits) as the
// image source. The photon pass still runs and fills the BounceCloud; after it
// drains and the HashGrid is built, the gather produces the image by querying
// the cloud — a backward (camera-first) density estimate instead of a forward
// (photon-splats-onto-camera) accumulation.
//
// For each camera pixel:
//   1. Cast a camera ray (Camera::pixelDirection) and raycast it against the
//      scene Volumes to find the FIRST visible surface point.
//   2. Compute the pixel's world-space gather footprint radius:
//        r = hitDistance * tan(pixelHalfAngle)
//      where pixelHalfAngle is half the angular size of one pixel (FOV/res).
//      This is the pixel's solid angle projected onto the surface — fixed in
//      the sense that it is exactly the area one pixel subtends at that depth,
//      independent of local photon density.
//   3. radiusSearch(hitPoint, r) -> nearby deposits.
//   4. Density estimate of outgoing radiance toward the camera:
//        L = (1 / (pi * r^2)) * (1 / N) * sum_d [ BRDF(d.incoming -> camera) * d.power ]
//      where N = photonsPerLight (the single 1/N flux normalization, the same
//      count divide the splat applied at tonemap time). BRDF is the material's
//      evaluate(wi, wo, normal) with wi = -d.incoming, wo = toCamera.
//   5. The radiance is written into the per-pixel Buffer in the SAME camera-pixel
//      coordinate frame the splat used, so the existing tonemap (exposure + flip)
//      converts it to the final image unchanged — except the Wave-2 global
//      footprint calibration constant is GONE: the footprint is now the real
//      per-pixel pi*r^2 computed here.
//
// Specular / delta surfaces at the visible point are left BLACK this sub-wave;
// ray-extension through mirrors is Wave 4c.

namespace Gather
{

// Result of running the gather: a radiance buffer (already 1/N normalized; in
// physical luminance units, ready for the camera exposure / tonemap), plus a few
// diagnostics for the render-test CLI.
struct GatherResult
{
    std::shared_ptr<Buffer> buffer;

    size_t pixelsHit = 0;       // camera rays that found a visible surface
    size_t pixelsGathered = 0;  // visible pixels that gathered >= 1 deposit
    size_t pixelsDelta = 0;     // visible pixels left black (delta surface)
    size_t pixelsMiss = 0;      // camera rays that hit nothing
    double maxGatherRadius = 0.0;
    double meanDepositsPerGather = 0.0;
    double maxRadiance = 0.0;  // peak per-pixel luminance written (pre-exposure), cd/m^2
};

// Run the gather. `buffer` is sized to the camera resolution; on return it holds
// per-pixel physical luminance (1/N normalized). `photonsPerLight` is N.
// `workerCount` parallelizes the pixel loop across that many threads (clamped to
// at least 1). `representativeCellSize` is unused by the gather itself (the grid
// is already built) and is here only for documentation symmetry; the caller picks
// the grid cell size when constructing the HashGrid.
GatherResult run(const std::vector<std::shared_ptr<Object>>& objects,
                 const std::shared_ptr<Camera>& camera,
                 const BounceCloud& cloud,
                 const HashGrid& grid,
                 const MaterialLibrary& materials,
                 const AnimationQuery* animation,
                 double photonsPerLight,
                 size_t workerCount);

}  // namespace Gather
