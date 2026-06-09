#pragma once

#include "AnimationQuery.h"
#include "BounceStore.h"
#include "Buffer.h"
#include "Camera.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "ProbeIndex.h"
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
//
// TEMPORAL COVERAGE: with a finite shutter the camera sees animated geometry at a
// CONTINUUM of poses across [frameTime, frameTime+shutterTime). A probe set
// collected at a single instant would miss a fast-moving object's later poses, so
// the photon-pass keep-test would CULL the deposits the camera will actually gather
// — the moving object would go dark. So the probe pass is run at several DISCRETE
// time slices spanning the shutter (`timeSlices`), unioning the probes. Probe count
// governs COVERAGE (was a bounce near ANY camera-reachable pose), not gather
// smoothness — the gather itself stays continuous, weighting deposits by photon
// time. A zero/absent shutter collapses to a single slice at `frameTime` (the exact
// static baseline when frameTime is 0). `timeSlices` is clamped to >= 1.
ProbeResult collectProbes(const std::vector<std::shared_ptr<Object>>& objects,
                          const Camera& camera,
                          const MaterialLibrary& materials,
                          const AnimationQuery* animation,
                          float frameTime = 0.0f,
                          float shutterTime = 0.0f,
                          int timeSlices = 1,
                          size_t subSample = 1);

// ===== Emitter deposits (fixture visibility, unified) =====

struct EmitterDepositResult
{
    size_t patches = 0;     // emissive patches found
    size_t generated = 0;   // candidate deposits generated across all patches
    size_t kept = 0;        // deposits that passed the probe keep-test and were stored
};

// Seed the BounceStore with an emitter's own SURFACE RADIANCE as raw deposits, so
// the unified gather renders the light fixture exactly like any other lit surface
// — visible DIRECTLY and in MIRRORS — with no special-case pass. Each emissive
// patch is tiled with deposits spaced `depositSpacing` apart; each carries power
//   power = radiance * (pi * patchArea / (4 * N))
// so the density estimate over the patch (with the gather's identity BRDF for an
// emitter and its 4/pi splat-parity factor) reproduces the patch's view-independent
// radiance L = M/pi exactly — the same value the legacy EmissiveGather wrote.
//
// Deposits are kept only if a probe lies within keep range (the same keep-test that
// bounds all bounce storage), so an off-camera fixture costs nothing. Call AFTER
// the photon pass appends its bounces and BEFORE buildIndex().
EmitterDepositResult depositEmitters(const std::vector<std::shared_ptr<Object>>& objects,
                                     const ProbeIndex& probeIndex,
                                     double depositSpacing,
                                     BounceStore& store);

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
//
// TIME: every camera-side ray (the per-pixel first hit, the specular `shade`
// extension chain, and the emitter intersection) is cast at a CAMERA RAY TIME so
// the scene's animated geometry is resolved at the pose the camera sees — NOT a
// hardcoded 0. With a finite shutter each camera sample draws a RANDOM time in
// [frameTime, frameTime+shutterTime) (matching the per-photon time model), so
// directly-visible AND reflected moving geometry integrates over the shutter into
// motion blur. The gather then keeps a deposit only if its photon time is within a
// temporal window of the camera ray time (`|deposit.time - rayTime| <= window`),
// so a moving surface's lighting is gathered from the photons that lit its
// time-correct pose, not from every photon across the frame. A zero/absent shutter
// uses a single fixed time `frameTime` (the exact static baseline at frameTime 0).
// `cameraSamples` per pixel are averaged so the shutter is integrated (1 = a single
// fixed-time sample for the no-blur / static path).
Result run(const std::vector<std::shared_ptr<Object>>& objects,
           const std::shared_ptr<Camera>& camera,
           const BounceStore& store,
           const MaterialLibrary& materials,
           const AnimationQuery* animation,
           size_t workerCount,
           double minGatherRadius,
           Buffer& buffer,
           float frameTime = 0.0f,
           float shutterTime = 0.0f,
           int cameraSamples = 1);

}  // namespace ProbeGather
