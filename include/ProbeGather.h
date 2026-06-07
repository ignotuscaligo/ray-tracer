#pragma once

#include "AnimationQuery.h"
#include "BounceStore.h"
#include "Buffer.h"
#include "Camera.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "Vector.h"

#include <cstddef>
#include <memory>
#include <vector>

// Phase 2a: PROBE-GUIDED UNIFIED GATHER.
//
// Replaces BOTH camera-side mechanisms — the direct-diffuse SPLAT and the
// quantized DENSITY GRID specular reflections — with ONE probe-guided raw-bounce
// gather:
//
//   1. PROBE PASS (collectProbes): cast camera rays and EXTEND each through delta
//      surfaces (mirror reflect / glass refract) to its FIRST NON-DELTA hit.
//      Those hit points are the PROBES — exactly the diffuse/glossy surface
//      points the camera can see directly or via any specular path. A spatial
//      index over them (ProbeIndex) drives the photon-pass keep-test.
//
//   2. GUIDED STORAGE (in the Worker): a non-delta photon bounce is stored raw in
//      a BounceStore only if it lies within gather range of some probe; otherwise
//      it is discarded (it can never reach the camera). Memory is bounded by
//      visible-surface-area, which makes raw storage affordable.
//
//   3. UNIFIED GATHER (run): for each camera ray, extend through delta surfaces to
//      the first non-delta hit, then gather the retained raw bounces near that hit:
//        L = (cos / footprintArea) * sum over bounces in radius of
//              BRDF(incoming, toCamera) * power
//      This SINGLE path renders both directly-visible diffuse (extension depth 0)
//      AND reflected/refracted diffuse (extension depth > 0) at identical fidelity.
//
// The mirror==direct invariant: a reflection in a mirror is the scene gathered
// the SAME way as the direct view, so reflections match the direct view (reversed)
// at the same fidelity instead of being a compressed separate path.
namespace ProbeGather
{

// ===== Probe pass =====

struct ProbeResult
{
    std::vector<Vector> probes;  // first-non-delta hit points reachable from the camera
    size_t cameraRays = 0;       // primary rays cast
    size_t deltaExtensions = 0;  // rays that passed through at least one delta surface
    size_t misses = 0;           // rays that escaped without reaching a non-delta surface
};

// Cast camera rays (every pixel) and extend each through delta surfaces to the
// first non-delta hit, collecting those points as probes. Pure geometry — does
// not read the bounce store. `subSample` >= 1 strides the pixel grid (1 = every
// pixel) to reduce probe-pass cost on large frames; the probes still tile the
// visible surface because adjacent pixels project to overlapping footprints.
ProbeResult collectProbes(const std::vector<std::shared_ptr<Object>>& objects,
                          const Camera& camera,
                          const MaterialLibrary& materials,
                          const AnimationQuery* animation,
                          size_t subSample = 1);

// ===== Unified gather =====

struct Result
{
    size_t pixelsHit = 0;        // camera pixels whose ray reached a non-delta surface
    size_t pixelsGathered = 0;   // pixels that summed at least one raw bounce
    size_t pixelsDelta = 0;      // pixels whose first surface was delta (extended)
    size_t pixelsMiss = 0;       // pixels whose ray escaped the scene
    double maxRadiance = 0.0;    // peak per-pixel luminance written (pre-exposure)
    double sumRadiance = 0.0;    // sum of per-pixel luminance (diagnostic)
    size_t depositsAccum = 0;    // total raw bounces summed across gathered pixels
};

// Run the unified gather, writing radiance into `buffer` (sized to the camera
// resolution; the gather OWNS the whole image — both direct and reflected
// pixels). `store` must have had buildIndex() called. `workerCount` parallelizes
// the pixel loop.
Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const BounceStore& store,
           const MaterialLibrary& materials,
           const AnimationQuery* animation,
           size_t workerCount,
           double minGatherRadius,
           Buffer& buffer);

}  // namespace ProbeGather
