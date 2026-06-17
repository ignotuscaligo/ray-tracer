#pragma once

#include "AnimationQuery.h"
#include "BounceStore.h"
#include "Buffer.h"
#include "Camera.h"
#include "Color.h"
#include "Hit.h"
#include "Material.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "PixelCoords.h"
#include "ProbeIndex.h"
#include "RandomGenerator.h"
#include "Ray.h"
#include "Vector.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

// Phase 2a: PROBE-GUIDED UNIFIED GATHER.
//
// Replaces BOTH camera-side mechanisms — the direct-diffuse SPLAT and the
// quantized DENSITY GRID specular reflections — with ONE probe-guided raw-bounce
// gather. The camera-side specular trace lives in EXACTLY ONE place — the probe
// pass — and the gather is PURE COLLECTION.
//
//   1. PROBE PASS = THE SINGLE CAMERA-SIDE SPECULAR TRACER (collectGatherPoints).
//      For every pixel sample (DOF aperture / random shutter time / per-Fresnel-
//      branch for glass) cast a camera ray and EXTEND it through delta surfaces
//      (mirror reflect / glass refract) to its FIRST NON-DELTA hit, ACCUMULATING
//      the BSDF throughput along the chain. Emit ONE GatherPoint record per
//      surviving sample: the non-delta hit it landed on, the product of the delta
//      BSDF weights to reach it, the unfolded path length (for the reflected
//      footprint), the sample's shutter time, and its 1/N pixel weight. The record
//      POSITIONS are the PROBES — exactly the diffuse/glossy surface points the
//      camera sees directly or via any specular path — and drive the photon-pass
//      keep-test (ProbeIndex).
//
//      WHY here and not the gather: a delta BSDF against a POINT camera is a
//      measure-zero event — a mirror cannot be GATHERED (no stored photon lands in
//      the pixel-footprint reflection cone), it must be TRACED. That irreducible
//      trace is this pass. (DESIGN §6b: EXTEND, do not gather, at a delta vertex —
//      the extension happens ONCE, here.)
//
//   2. GUIDED STORAGE (in the Worker): a non-delta photon bounce is stored raw in
//      a BounceStore only if it lies within gather range of some probe; otherwise
//      it is discarded (it can never reach the camera). Memory is bounded by
//      visible-surface-area, which makes raw storage affordable.
//
//   3. UNIFIED GATHER (run) = PURE COLLECTION. A flat loop over this camera's
//      GatherPoint records: for each, density-estimate the retained raw bounces at
//      its position over a footprint sized from the unfolded path length, multiply
//      by the record's specular throughput, and accumulate (weighted by the
//      sample weight) into its pixel. NO ray casting, NO specular recursion, NO
//      delta extension in the gather — those all happened in the probe pass. Every
//      gather point IS a probe by construction, so a surface reached only through a
//      minority Fresnel branch of a small glass object is probed AND gathered with
//      the same sampling — no 1-vs-N mismatch can cull its deposits.
//
// The mirror==direct invariant: a reflection in a mirror is the scene gathered
// the SAME way as the direct view, so reflections match the direct view (reversed)
// at the same fidelity instead of being a compressed separate path.
namespace ProbeGather
{

// ===== Probe pass = single camera-side specular tracer =====

// One per-pixel camera-side specular trace result: the non-delta surface a camera
// sample reached (directly at extension depth 0, or through a delta chain), plus
// everything the PURE-COLLECTION gather needs to turn it into pixel radiance with
// no further ray casting. Its `position` is ALSO the probe point for the keep-test
// (every gather point is a probe by construction). ~72 B.
struct GatherPoint
{
    PixelCoords pixel{0, 0};       // the camera pixel this sample contributes to
    Vector position{};             // first-non-delta world hit (the probe point)
    Vector normal{};               // surface normal at the hit
    Vector viewDir{};              // unit direction from the hit toward the viewer (the
                                   //   camera eye for a direct hit, the last specular
                                   //   vertex for a reflected one): the BRDF's wo. Stored
                                   //   so the gather needs no ray to recover it.
    std::size_t materialIndex = 0; // hit material (kEmitterMaterial for an emitter face)
    Color specularThroughput{1.0f, 1.0f, 1.0f};  // product of delta BSDF weights to reach
                                                  //   the hit (identity for a direct hit)
    double unfoldedPathLength = 0.0;  // total camera-to-hit distance along the (unfolded)
                                      //   specular chain; sizes the reflected footprint
    double footprintRadius = 0.0;     // the gather disc radius for this record, computed in
                                      //   the probe pass: a RAY DIFFERENTIAL (min perp) for a
                                      //   direct hit, the unfolded-path perpendicular
                                      //   footprint (capped) for a reflected hit. Stored so
                                      //   the gather does NO geometry — pure collection.
    float sampleTime = 0.0f;          // the shutter time this camera sample was cast at
                                      //   (the gather's temporal-window reference)
    float sampleWeight = 1.0f;        // 1/N over this pixel's surviving samples (the
                                      //   DOF/shutter/Fresnel average)
};

struct ProbeResult
{
    // One record per surviving per-pixel camera sample (direct or specularly
    // extended). The record positions are the probe points; the gather consumes the
    // full records. Records for ONE camera only — the Renderer keeps per-camera
    // record sets and unions only the POSITIONS for the shared keep-test index, so a
    // camera's gather never reads another camera's records.
    std::vector<GatherPoint> points;
    size_t cameraRays = 0;       // primary rays cast
    size_t deltaExtensions = 0;  // samples that passed through at least one delta surface
    size_t misses = 0;           // samples that escaped without reaching a non-delta surface
};

// THE SINGLE CAMERA-SIDE SPECULAR TRACER. For every pixel (strided by `subSample`)
// take this pixel's camera samples — all DOF aperture samples for a RealLens camera,
// all `cameraSamples` random shutter-time samples for a finite shutter, and (for a
// dielectric first hit) all `kCameraSamplesPerPixel` stochastic Fresnel branches —
// extend each through delta surfaces to its first non-delta hit accumulating the BSDF
// throughput, and emit one GatherPoint per surviving sample. The pure-collection
// gather later consumes these records with no further tracing. Does not read the
// bounce store.
//
// SAMPLING MIRRORS THE FORMER GATHER LOOP exactly, so the records are an unbiased
// camera-side estimate:
//   - primarySamples = max(DOF samples, shutter samples); each draws a random shutter
//     time (or the fixed frameTime for a zero shutter) and generates its ray at that
//     time via generatePrimaryRayAt (the camera pose is resolved at the sample time —
//     §9e camera motion blur). DOF samples pass the generator for aperture jitter.
//   - a dielectric first hit fans into `kCameraSamplesPerPixel` Fresnel picks (unless
//     DOF already multisamples), each its own surviving record; a mirror is a single
//     deterministic extension. sampleWeight = 1 / (surviving sample count) per pixel.
//
// TEMPORAL COVERAGE: each record carries its own `sampleTime`, drawn across the
// shutter, so the union of record positions COVERS every pose the camera sees during
// the shutter — exactly the surface regions the photon-pass keep-test must retain
// deposits for. The gather then keeps a deposit only if its photon time is within a
// shutter-sized window of the record's sampleTime (continuous weighting over discrete
// samples). A zero/absent shutter => one sample at `frameTime` (the static baseline).
//
// CAMERA MOTION BLUR (§9e): every ray is generated at its sample time, so an animated
// camera's probe rays (and thus the keep-test coverage and the gather) originate from
// its pose at each sample time.
// `seed`: when non-negative, the probe pass's RNG (DOF/shutter/Fresnel sampling) is
// SEEDED to this value for reproducibility; -1 (default) seeds from random_device
// (the production path). Used by the single-thread deterministic test mode so the
// camera-side sampling is a fixed draw sequence.
ProbeResult collectGatherPoints(const std::vector<std::shared_ptr<Object>>& objects,
                                const Camera& camera,
                                const MaterialLibrary& materials,
                                const AnimationQuery* animation,
                                float frameTime = 0.0f,
                                float shutterTime = 0.0f,
                                int cameraSamples = 1,
                                size_t subSample = 1,
                                long long seed = -1);

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

// Run the unified gather = PURE COLLECTION. A flat loop over this camera's
// GatherPoint `points` (produced by collectGatherPoints — the single camera-side
// specular tracer): for each record, density-estimate the retained raw bounces at
// its `position` over its `footprintRadius`, multiply by its `specularThroughput`,
// and accumulate (weighted by `sampleWeight`) into its `pixel`. Writes radiance
// into `buffer` (sized to the camera resolution; the gather OWNS the whole image —
// direct and reflected pixels). `store` must have had buildIndex() called.
// `workerCount` parallelizes the record loop.
//
// NO RAY CASTING, NO specular recursion, NO delta extension here — all of that
// happened in the probe pass, whose records this consumes. The gather only does the
// density estimate (BRDF evaluate over deposits in the footprint), exactly as for a
// directly-visible surface; a reflected gather point is identical to a direct one
// modulo its precomputed throughput/footprint. Because every record IS a probe, a
// surface reached only through a minority Fresnel branch is gathered with the same
// sampling that probed it — no 1-vs-N cull.
//
// TIME: each record carries the shutter `sampleTime` of the camera sample that
// produced it; the gather keeps a deposit only if its photon time is within a
// shutter-sized window of that sampleTime (a timeless emitter deposit always
// passes). A moving surface's lighting is thus gathered from the photons that lit
// its time-correct pose. `shutterTime` sizes the temporal half-window (0 => only
// same-instant deposits, which on a static scene is every deposit).
Result run(const std::shared_ptr<Camera>& camera,
           const std::vector<GatherPoint>& points,
           const BounceStore& store,
           const MaterialLibrary& materials,
           size_t workerCount,
           double minGatherRadius,
           Buffer& buffer,
           float shutterTime = 0.0f);

// ===== Test-visible gather internals =====
//
// These are the gather's load-bearing geometric / radiometric primitives, hoisted
// out of the .cpp's anonymous namespace into a declared API so unit tests can call
// the PRODUCTION code directly instead of re-implementing the math in the test body
// (the self-consistent-but-wrong trap DESIGN warns about). Precedent:
// Worker::splatToCamera was made public "purely for this test." No behavior change —
// the renderer's own call sites use these same definitions.
namespace testing
{

// Sentinel material index for an emitter (AreaLight) hit: gathered with an identity
// BRDF (f = 1). Mirrors the .cpp's internal constant so tests can build emitter Hits.
constexpr std::size_t kEmitterMaterial = std::numeric_limits<std::size_t>::max();

// THE density estimate the unified gather performs per record:
//   L_o = (4/pi) * (1 / (pi r^2)) * Σ_p f(wi_p, wo) Φ_p
// over the raw bounces within `footprintRadius` of `hit.position`, with NO
// cos(theta_view) term, NORMAL-AGREEMENT leak suppression, a loose tangent-band
// backstop, and the temporal window (|deposit.time - rayTime| <= timeHalfWindow; a
// timeless emitter deposit always passes). `r` is floored at `minGatherRadius`.
// `wo` is the unit direction from the hit toward the viewer. Returns the radiance
// leaving the surface toward `wo`; `outDeposits` reports how many deposits were kept.
// `store` must have had buildIndex() called. This is the exact function the
// per-record gather loop invokes.
Color gatherRadiance(const BounceStore& store,
                     const MaterialLibrary& materials,
                     const Hit& hit,
                     const std::shared_ptr<Material>& material,
                     const Vector& wo,
                     double footprintRadius,
                     double minGatherRadius,
                     float rayTime,
                     float timeHalfWindow,
                     std::size_t& outDeposits);

// World-space surface footprint radius of a DIRECT (depth-0) pixel hit, via a ray
// differential (the min of the adjacent-pixel on-surface spacing and the
// perpendicular constant-angle footprint). Distortion- and foreshortening-correct.
double pixelFootprintRadius(const Camera& camera,
                            const AnimationQuery* animation,
                            double pixelHalfAngle,
                            const PixelCoords& coord,
                            const Hit& hit,
                            double fallbackDistance,
                            float rayTime);

// World-space footprint radius for a REFLECTED (specularly-extended) hit: the pixel
// half-angle over the unfolded path length, with the foreshortening enlargement
// capped at 2x (NOT inflated by 1/cos(view) — DESIGN §6f).
double reflectedFootprintRadius(double pixelHalfAngle,
                                double unfoldedPathLength,
                                const Vector& viewer,
                                const Hit& hit);

// Result of extending a camera ray through the delta chain to its first non-delta
// (or emitter) hit. The camera-side specular trace (DESIGN §6b), exposed so the
// mirror-parity test can verify the unfolded path length + accumulated throughput.
struct ExtendResult
{
    Hit hit;                              // first non-delta / emitter surface reached
    Color throughput{1.0f, 1.0f, 1.0f};  // product of delta BSDF weights to reach it
    double unfoldedPathLength = 0.0;      // total camera-to-hit distance along the chain
    Vector finalDirection{};             // last segment direction; wo = -finalDirection
    bool traversedDelta = false;         // passed through at least one delta surface
    bool valid = false;                  // false => escaped or exceeded the depth cap
};

// Extend `ray` through delta surfaces to the first non-delta hit, accumulating the
// specular throughput and unfolded path length. The same trace the probe pass runs.
ExtendResult extendToNonDelta(const std::vector<std::shared_ptr<Object>>& objects,
                              const MaterialLibrary& materials,
                              const AnimationQuery* animation,
                              RandomGenerator& generator,
                              const Ray& ray,
                              float time);

}  // namespace testing

}  // namespace ProbeGather
