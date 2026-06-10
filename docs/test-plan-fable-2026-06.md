# Renderer Test-Suite Review (Fable, 2026-06-10)

*Read-only review by Fable 5. Goal: validate every corner of the renderer objectively (analytic/numeric assertions) rather than by subjective image inspection. Maps DESIGN.md [INVARIANT] tags to test coverage, finds gaps, proposes objective tests.*

## Headline findings

- **Ten DESIGN.md invariants have NO test at all**, including the two most alarming:
  - **#1 mirror==direct** (the Phase-2a headline) — currently pinned only by looking at `MirrorDirectTest.json` PNG.
  - **#23 reverse projection is the exact inverse of forward ray-gen** — a divergence would silently shear splats off their gather pixels. DESIGN explicitly calls these "a matched pair that must change together."
- **Several existing tests assert re-implemented math, not production code** — the self-consistent-but-wrong trap DESIGN warns about:
  - `test_SelfHitEpsilon.cpp` duplicates the `1e-4` constant; reverting the worker to `DBL_EPSILON` still passes.
  - `test_ProbeGather.cpp` normal-agreement test computes `Vector::dot` against `0.5` literally — never calls the gather. The production predicate (tangent-band backstop, zero-normal skip, emitter branch, temporal window) never runs.
  - `test_SplatToCamera.cpp` "a delta material deposits nothing" actually passes a Lambertian with photons-per-light 0 — tests the on/off gate, not the `isDelta()` guard.
  - The probe gather's real density-estimate normalization (`gatherRadiance`) is never executed by a unit test — the test recomputes the estimate in its own body.
- **Bonus source bug found while reading:** `LambertianMaterial::evaluate` ignores `wi`, so `f(wi_below, wo_above) = albedo/pi != f(wo_above, wi_below) = 0` — a Helmholtz-reciprocity violation at the hemisphere boundary.
- **No seeded reproducibility today** — production RNG seeds from `random_device`, and `gatherRows` constructs unseeded per-thread generators, so every integration test pays with loose tolerances (3%, "stable across unseeded runs").

## Invariant coverage matrix (abridged)

Untested invariants: #1 mirror==direct, #2 probe-gather default, #3 bounded-queue/trace-to-completion population, #16 reflected-footprint 2x cap, #17 no cos(theta_view) in gather, #18 specular depth cap / hall-of-mirrors terminates, #23 forward/reverse projection inverse, #25 buffer additivity/distribution-readiness, #26 per-light count-equivalence, #30 per-sample random shutter time -> blur, #31 probe time-slice union / temporal window / timeless-emitter sentinel.

Full matrix (32 rows) in the original review; see Fable output.

## Top 10 highest-leverage tests to add first

1. **T1 FurnaceLedger** — truncated-furnace energy conservation. In a sealed diffuse box, Lambertian throughput is deterministic (exactly `albedo` per bounce, zero variance), so total deposited power = `Phi * (1 - rho^(B+1))/(1 - rho)` — an *exact* closed-form oracle. One number pins invariants #3, #4, #5, #7, #9, #10 and breaks under any reintroduction of Russian roulette, gather-side 1/N, or mode-sampling.
2. **T3 CameraRoundTrip** — `coordForPointSubPixel(ray at pixel center)` must return `(x+0.5, y+0.5)` within 1e-9; off-frustum returns nullopt. Pure algebra; pins the untested matched-pair invariant #23.
3. **T2 BSDFConsistency** — per-sample identity `weight == evaluate*cos/pdf`, Helmholtz reciprocity, hemisphere normalization `integral f*cos <= 1`, pdf-is-a-density chi-square. Exact identities; exactly the bug class the VNDF migration already hit.
4. **T4 MirrorDirectParity** — diffuse ball + flat mirror in frame: reflected-blob mean luminance / direct-blob == mirror albedo; blob second moment matches after unfolding path-length scale. Converts the headline invariant from "look at a PNG" to a hard number.
5. **T6 GatherRadianceUnit** — expose `gatherRadiance`; append hand-built RawBounces and call the real gather. Asserts normalization with a real BRDF, no cos(theta_view), normal agreement + 2r band, temporal window (timeless sentinel always kept), additivity. Unblocks #15, #17, #31.
6. **T5 InverseSquareFalloff** — OmniLight over diffuse floor; rendered `L(p1)/L(p2) == (cos1/d1^2)/(cos2/d2^2)`. Ratio form cancels exposure/tonemap/parity constants — first analytic pixel-value oracle.
7. **T7 CountEquivalence + BufferSummability** — `(N, Phi)` vs `(4N, Phi)` equal mean luminance; two N/2 buffers summed == one N render. Pins distribution-readiness invariants #25, #26.
8. **T10 DeltaExclusion + ProbeDefaults** — fix the misnamed splat test (real MirrorMaterial deposits 0); worker bounce-store delta exclusion; `RenderSettings{}.useProbeGather == true`; documented defaults.
9. **T11 SelfHitLive** — export the worker threshold; live one-photon rig where an admitted acne re-hit produces a second deposit; assert count == 1. A reversion then actually fails the suite.
10. **T8 VarianceConvergence + ThreadEquivalence** — RMS pixel diff at N vs 4N has ratio 2 +/- CI (the 1/sqrt(N) law, the only consistency check); 1-worker vs 8-worker equal mean luminance in expectation (bitwise is unattainable — atomic float add is non-associative).

Next tier: T9 specular cap, T12 blur length / timeless emitter, T13 dielectric estimator sweep, T14 quad/triangle + shared-vertex watertightness, T15 RMSE reference-regression backstop (after the seed plumb).

## Test infrastructure to build first (the unlock)

1. **`tests/RenderFixture.h`** — extract the copy-pasted temp-JSON -> SceneLoader -> renderFrame -> cleanup pattern + image-statistics helpers (meanLuminance, regionMean, centroid, secondMoment, rmse). T1/T4/T5/T7/T8/T9/T12/T15 all sit on this.
2. **Statistical assert helpers** — `REQUIRE_MEAN_CI`, `REQUIRE_PROPORTION_CI` (binomial), chi-square for histogram-vs-pdf. Replaces magic margins (0.02, 0.04, 0.90) with explicit false-positive rates.
3. **A render-level seed** (`$seed` -> per-worker `seed + workerIndex`) — one settings field + two constructor call sites. Tightens every integration tolerance, enables true seeded-reproducibility tests and a meaningful RMSE backstop.
4. **Expose gather internals** (`gatherRadiance`, `extendToNonDelta`, `pixelFootprintRadius`) out of the anonymous namespace into a test-visible API — precedent: `splatToCamera` was made public "purely for this test." Without this, leak-suppression and temporal-window invariants can only be tested by re-implementing the math (the trap).
5. **Furnace fixture** — sealed-cube builder + geometric-series oracle. Comment must flag: this is deliberately a *truncated* furnace; a strict white-furnace assertion contradicts DESIGN §2 and must not be "fixed in."
6. **Float-buffer access in RenderResult** — T15 and tighter T4/T5 want pre-tonemap radiance; 16-bit tonemapped pixels quantize away small regressions.

## Process note from Fable

Several tests (T1, T4, T6) are deliberately redundant on the energy model. That redundancy is the point: DESIGN's stated threat is an agent changing code and test together; independent oracles approaching the same invariant from different directions (a ledger, a parity ratio, a unit estimate) make a coordinated silent change much harder to pass.
