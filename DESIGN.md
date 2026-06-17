# DESIGN.md — Renderer Intent & Deliberate Invariants

**Branch:** `renderer-single-photon`. **Read this before changing any renderer behavior.**

## What this document is

This is a normative record of *expected behaviors* and *deliberate invariants* for the
photon-mapping renderer. It exists because several decisions in this codebase **look like
bugs but are intentional**. The test suite verifies the implementation is *self-consistent*
— but an agent (human or AI) can change the implementation AND its test together and pass a
green suite while silently destroying the original intent (e.g. reintroducing biased
lighting). Tests alone cannot catch that. This doc is the authoritative record of intent
that does.

**Rules for changing behavior:**

1. Read this doc *first*, before editing renderer code or "fixing" anything that looks off.
2. Changing anything tagged **[INVARIANT]** requires **updating this doc and justifying the
   change in the commit** — not merely updating the test to match new behavior. A green
   suite is *not* sufficient license to change an [INVARIANT].
3. Items tagged **[DETAIL]** are incidental implementation choices; refactor freely.
4. Items under **"Looks like a bug — it is not"** are the highest-risk targets. Each says
   *why* and *do not "fix" this*. If you believe one is genuinely wrong, the bar is: explain
   why the cited reasoning fails, update this doc, then change code + test together.

Full design reasoning lives in
`~/repos/personal-notes/research/ray-tracer-architecture-vision.md` (the running design
narrative). This doc links to it rather than reproducing the long-form arguments.

Every load-bearing invariant below cites `file:line` on this branch so it is verifiable.
Line numbers drift; the surrounding code comment is the durable anchor.

---

## 0. Pipeline shape

Single-machine forward (light-side) photon tracer with a camera-side
**probe-guided unified gather** (Phase 2a). The shape is **probe → emit →
trace-each-photon-to-completion (keep raw bounces near probes) → unified gather**:

0. **Probe pass (Phase 2a) = the single camera-side specular tracer.** Before the
   photon pass, cast camera rays and EXTEND each through delta surfaces (mirror/glass)
   to its FIRST NON-DELTA hit, emitting a `GatherPoint` record per surviving sample;
   the record positions are the PROBES and are indexed
   (`ProbeGather::collectGatherPoints` + `ProbeIndex`, `src/ProbeGather.cpp`,
   `src/ProbeIndex.cpp`). The probes are exactly the diffuse/glossy surface points the
   camera can see directly OR via any specular path; the gather later just collects
   over the records (§6f).
1. **Emit.** Each `Light` produces photons into the one bounded `photonQueue`. The
   per-photon magnitude `Phi/N` is baked at emission (`LightQueue::registerLight`,
   `src/LightQueue.cpp:16`).
2. **Trace to completion.** A worker fetches a batch and traces each photon's *entire*
   random walk in a single loop (`Worker::processPhotons`, `src/Worker.cpp`):
   raycast → if non-delta, KEEP the bounce RAW in the `BounceStore` iff a probe is
   within the keep-radius (else discard) → check terminators → scatter exactly one
   continuation photon → repeat. The continuation lives in a local stack slot; it
   **never re-enters the queue**. The source batch's queue slots are released only
   after the whole batch finishes.
3. **Unified gather (post-pass, per camera) = pure collection.** `ProbeGather::run`
   loops over this camera's `GatherPoint` records (from the probe pass, step 0) and
   density-estimates the retained RAW bounces near each record's position — NO ray
   casting, NO delta extension (that happened once in the probe pass). The SAME path
   renders both directly-visible diffuse (extension depth 0) AND reflected/refracted
   diffuse (extension depth > 0). Emitter deposits make light fixtures camera-visible.

**[INVARIANT] Mirror == direct (Phase 2a headline).** A reflection in a mirror is
the scene gathered the SAME way as the direct view: a reflected diffuse point is
gathered identically to a directly-visible one (extension depth is the only
difference), so a flat mirror shows the scene at the SAME fidelity as pointing the
camera at it directly — not blurrier/blockier. Verified by `MirrorDirectTest.json`
(flat mirror wall + ball: the reflected ball matches the direct ball in size,
brightness, and sharpness; reflection visibly crisper than the retired density
grid). Do not reintroduce a separate, lower-fidelity reflection path.

**[INVARIANT] Density grid retired from the default path.** The probe-guided raw-
bounce gather replaces BOTH the direct-diffuse SPLAT and the quantized
DENSITY-GRID reflection lookup. `$probeGather` defaults true. The legacy splat +
`DensityGrid` + `MirrorGather` path is kept ONLY behind `$probeGather false` for
A/B comparison and is deprecated pending deletion; do not build new behavior on
it. Memory under the gather is bounded by the probe KEEP-TEST (visible-surface-
area), not by photon count — see §6f.

**[INVARIANT] One bounded queue, trace-to-completion, no per-bounce requeue.** The photon
population is constant (one-in, one-out per bounce — see §1). The old multi-queue
back-pressure / overflow / emitter-fan-out machinery has been deleted *on purpose*; do not
reintroduce a per-bounce requeue stage. The single queue exists only to decouple emission
from tracing and bound concurrent in-flight photons. Reasoning: architecture-vision
"REDESIGN SHIPPED" section.

---

## 1. Single-photon scatter — looks like a bug, it is not

**[INVARIANT] Each bounce produces exactly ONE photon, drawn from the material's STOCHASTIC
importance sample (`Material::sample()`), NEVER the deterministic `sampleMode()` peak.**

- Live path: `Worker::processPhotons` calls `generateDaughters(..., count=1,
  totalDaughters=1, ...)` (`src/Worker.cpp:558-572`), which calls
  `sample(incident, normal, generator)` unconditionally (`src/Material.cpp:93`).
- `sample()` is the stochastic draw: cosine-weighted hemisphere for Lambertian, GGX lobe
  for Microfacet, the delta direction for Mirror/Dielectric.
- The outgoing photon carries `parentColor * s.weight` where `s.weight` encodes the Monte
  Carlo throughput `f·cos/pdf` (`src/Material.cpp:104`). Expected outgoing energy =
  incoming · albedo, i.e. energy-conserving with a single sample.

**Why it looks like a bug, and why it is not:** an earlier "N=1 daughter is too bright /
floor hot-spot" result was a **sampling bug**, not evidence against single-photon scatter.
The old fan-out used the *mode* (`sampleMode()`, the lobe peak) for daughter index 0. The
mode is the distribution *peak*, not a fair draw — a lone mode photon over-weights the
dominant lobe direction and biases diffuse transport (the mean-cosine result: a Lambertian
mode-photon leaves along the normal instead of a fair cosine sample). The fix was to scatter
the single photon from `sample()`, never `sampleMode()`. This is documented at length in the
`generateDaughters` comment (`src/Material.cpp:54-77`).

**Do not "fix" this by:** switching the single continuation to `sampleMode()` ("the peak is
the obvious direction"), or reintroducing N-daughter fan-out to "reduce noise." Single
sample per bounce + constant population is the deliberate model. More emitted photons (not
more daughters) is the noise lever; cost is linear.

> **Documented false alarm (this is the canonical example this whole doc exists to prevent).**
> Code-review-2 §1f/§2c (`research/ray-tracer-code-review-2.md`) claims the N=1 continuation
> takes the `sampleMode()` branch via "global index 0 is the BRDF mode" and flags it as a
> possible bias bug. **That claim is false on this branch.** The index-0-is-mode rule was
> removed; `generateDaughters` calls `sample()` for *every* index (`src/Material.cpp:93`).
> The reviewer read a superseded rule. This is exactly the trap: a plausible-sounding
> "fix" (force index 0 to `sampleMode`) would *reintroduce* the original bias and could ship
> with a green, co-updated test. **The mean-cosine / hemisphere-distribution test is the
> guard** — do not weaken it.

**[DETAIL]** `totalDaughters` / `globalStart` remain in the `generateDaughters` signature
for legacy plumbing; with `count=1` they are inert. `Material::bounce` (`src/Material.cpp:18`)
is the eager all-N wrapper, now test-only. These are dead-code-removal candidates, not
behavior.

---

## 2. Termination: deterministic decay, NOT Russian roulette — looks like a bug, it is not

**[INVARIANT] A photon dies when its current magnitude falls below a fixed ABSOLUTE
`terminationThreshold`, OR when it reaches the `bounceThreshold` hard cap — whichever fires
first. There is NO Russian roulette and NO survivor reweight.**

- Predicate: `photonDecayAlive(photon, terminationThreshold)` returns `alive iff
  currentMagnitude > terminationThreshold` (`include/Photon.h:50-62`). `currentMagnitude` =
  max color channel.
- Loop terminator: `if (!decayAlive || bounces >= bounceThreshold) break;`
  (`src/Worker.cpp:543-547`).
- Magnitude is monotonic non-increasing: every BSDF weight is ≤ 1, so each bounce only
  attenuates (`src/Worker.cpp:536-542` comment). **[INVARIANT] This bound must hold for
  EVERY material**, including the glossy Microfacet/GGX lobe. Microfacet uses **VNDF
  sampling** (Heitz 2018) precisely so its reflection throughput is `F·G2/G1(wi) =
  F·G1(wo) ≤ 1` by construction (`src/MicrofacetMaterial.cpp`). The earlier plain-NDF
  sampler's weight `F·G2·(wi·wh)/(cos_i·cos_h)` could EXCEED 1 at grazing incidence — a
  per-bounce energy GAIN that breaks this premise (fireflies, photons that outlive the
  decay model). A new glossy/specular material must likewise keep `weight ≤ 1`; if a
  sampler cannot, this monotonicity claim (and the termination model built on it) does
  not hold. Pinned by the grazing-incidence weight sweep `tests/test_BSDF.cpp`
  (`[VNDF]`).
- The bounce cap is the hard safety bound guaranteeing termination even for an
  albedo→1 material that never decays (`src/Worker.cpp:532-535`). Default `bounceThreshold
  = 1` (`include/RenderSettings.h:22`).

**Why this was chosen over RR (deliberate, not an oversight):**

- **Per-photon interpretability.** Because magnitude only decays, a photon's brightness
  stays physically meaningful — it encodes that path's real energy. The image *noise* is
  then diagnostic (sparse-but-bright = bright/distant source; dense-dim = mixed bounce). RR
  destroys this: boosted survivors carry fake-inflated energy unmoored from any real path.
- **Lower variance.** RR's survivor boost (÷(1−q)) manufactures high-energy spikes — a known
  firefly source. Deterministic decay adds no such variance.
- The only bias is the dropped residual tail of a photon killed at the cutoff (deep
  high-order indirect light); with a low threshold this is a tiny, smooth, artifact-free
  global dimming, drivable toward zero by lowering the threshold.

**Known limitation — do not mistake it for a bug:** in high-albedo / nearly-enclosed scenes
(furnace test, hall of mirrors, near-white room) light keeps most of its energy across many
bounces, the dropped tail is large, and the image reads **too dark**. In that regime the
**bounce cap is the real terminator**, not the decay floor. This is an accepted tradeoff,
not a defect. Mitigation is to lower `terminationThreshold` and raise `bounceThreshold`, not
to add RR.

**Do not "fix" this by:** "adding Russian roulette back" because it "looks missing," or
because a furnace-test scene renders dark. RR was implemented, evaluated, and **deliberately
removed** (dead RR config was deleted in the cleanup; see code-review-1 §1b). Reasoning:
architecture-vision "DECISION: deterministic decay termination over RR".

### 2a. The `terminationThreshold` units trap — [INVARIANT] document, do not silently "fix"

**Photon magnitude is `flux / photonsPerLight`, an ABSOLUTE flux-bundle value that is
scene-dependent and usually LARGE — often hundreds (e.g. flux 125.6M / 300K photons ≈ 419
per photon), NOT near 1.0.** Photons do **not** start near 1.0.

- Honestly documented at `include/RenderSettings.h:32-38` and `include/Photon.h:47-49`.
- Default `terminationThreshold = 1.0` (`include/RenderSettings.h:38`).

Consequence: a fixed absolute threshold is **not scene-portable**. On a high-flux scene the
floor never fires and `bounceThreshold` does all the terminating; on a low-flux/high-count
scene a `1.0` floor could kill all multi-bounce transport. **This is intended behavior given
the absolute-floor decision** (Elijah explicitly chose absolute over relative so brighter
bundles bounce deeper — `include/Photon.h:43-46`). Anyone tuning a scene must set
`$terminationThreshold` *relative to that scene's per-photon emission magnitude*.

**Do not "fix" this by:** assuming photons start near 1.0 and hard-coding a small default; or
silently switching to a relative-to-emission cutoff (that was the *prior* model and was
deliberately reversed — architecture-vision "Termination switched to ABSOLUTE floor"). A
legitimate improvement is to *derive* the default from mean emission magnitude — but that
changes the [INVARIANT] semantics and must be recorded here first.

### 2b. Self-hit / shadow-acne epsilon — [INVARIANT] keep an absolute world-space floor

**[INVARIANT] A spawned ray must not re-intersect the surface it was spawned on.**
The photon pass spawns each continuation ray EXACTLY at the bare hit position
(`Material::generateDaughters` sets `out.ray = {position, direction}` with no
offset, `src/Material.cpp`), and the camera splat's occlusion ray starts at the
hit too. The geometric primitives have NO matching positive world-space t-floor of
their own — the watertight triangle test only rejects `t <= 0` (`src/Ray.cpp`
`rayIntersectsTriangle`) and the sphere test's `1e-6` floor is PARAMETRIC (it
shrinks with a non-unit direction, `src/Ray.cpp` `rayIntersectsSphere`). So the
only world-space backstop is `Worker.cpp`'s `selfHitThreshold`, compared against
`hit.distance`.

`selfHitThreshold` is an ABSOLUTE world-space `1e-4` (`src/Worker.cpp`), NOT
`DBL_EPSILON`. A grazing continuation off a curved surface self-re-intersects at a
tiny-but-positive distance (~`1e-4` at Cornell scale) that sails past `DBL_EPSILON`
(~`2.2e-16`); admitting it double-deposits / double-attenuates that photon (shadow
acne, energy error). The `1e-4` floor is the same order as the gather path's
ray-spawn offset (`kReflectionEpsilon = 1e-3`, `src/ProbeGather.cpp`), so the
photon side and the camera side are consistent. Pinned by
`tests/test_SelfHitEpsilon.cpp` (a grazing self-re-hit lands in the
`(DBL_EPSILON, 1e-4)` band the floor rejects; a legitimate far hit stays above
`1e-4` and is kept).

**Do not "fix" this by:** lowering `selfHitThreshold` back toward machine epsilon
("the threshold looks too coarse"). At this renderer's scale `1e-4` is far below
any real feature size and far above float self-intersection noise; a near-zero
threshold reintroduces the acne.

---

## 3. Gather is additive — NO 1/N count-normalization — looks like a bug, it is not

**[INVARIANT] The 1/N photon-count normalization is baked at EMISSION, not applied at the
gather. The gather is a pure additive sum; the only divide remaining is a geometric
footprint-area divide (units, not counting).**

- Emission bakes it: `perPhotonFlux = luminousFlux / count` (`src/LightQueue.cpp:16`).
- Density-grid lookup does **not** divide by N — only by cell footprint area
  (`src/DensityGrid.cpp:58-71`, the comment is explicit: "NO 1/N count-normalization here").
- Camera splat does **not** divide by N — only by pixel footprint area
  (`src/Worker.cpp:197-200`).
- `DensityGrid::add` is a plain additive accumulate: `cell.power += power`
  (`src/DensityGrid.cpp:31`).

**Why:** baking magnitude at emission makes the gather purely additive, which makes
distribution trivial — independent machines emit disjoint photon sets and just SUM their
buffers, with no shared N to coordinate (§7). The count-equivalence guarantee: 100 photons
of Phi/100 and 10 of Phi/10 deposit the same expected total energy
(`src/LightQueue.cpp:11-15`). Verified: identical 128px Cornell, old daughter model vs new
single-photon, mean luminance matched within 0.55% (architecture-vision Step 4).

**Do not "fix" this by:** adding a `1/photonCount` (or `1/N`) factor in
`DensityGrid::lookupIrradiance`, `Worker::splatToCamera`, or `MirrorGather` because "the
gather looks unnormalized." It is normalized — at emission. Adding a second divide
double-counts and dims the image by a factor of N. **`m_photonsPerLight` in the worker is now
only an on/off gate for the splat (`src/Worker.cpp:190`), not a divisor** — stale comments
that still say "normalized by 1/N" (`src/Worker.cpp:295-298`) are wrong and should be
corrected, but the *code* is right.

---

## 4. Bundled absolute magnitude + relative (percentage) absorption

**[INVARIANT] Photons carry an ABSOLUTE floating-point flux-bundle magnitude. Materials
attenuate by MULTIPLYING by per-channel reflectance — they never absolute-subtract energy.**

- Scatter multiplies: `out.color = parentColor * s.weight` (`src/Material.cpp:104`).
- Per-channel: a red surface zeroes G/B and leaves R near-full via the `Color` multiply;
  `(1,1,1)→(0.9,0,0)` and `(10,10,10)→(9,0,0)` are the same *relative* loss
  (architecture-vision "Two refinements").
- "Magnitude 1.0" is a defined photometric bundle of unit photons; brightness is controlled
  by (emit count × per-photon magnitude).

**[DETAIL]** The geometric divide at gather (`/ cellArea`, `/ πr²`) converts accumulated
power to radiance/irradiance — a units conversion, orthogonal to the absorption model.

---

## 5. Materials & BSDF `isDelta` dispatch

**[INVARIANT] The gather dispatches on ONE property — `Material::isDelta()` — not on
per-material special cases.**

- **delta (`isDelta()==true`)** → ray EXTENSION (follow the deterministic direction and
  recurse). Mirror (`include/MirrorMaterial.h:21`), Dielectric/glass
  (`include/DielectricMaterial.h:45`).
- **smooth (`isDelta()==false`)** → DENSITY GATHER (read deposited grid irradiance).
  Lambertian (`src/LambertianMaterial.cpp:72,89`), Microfacet/GGX (inherits the base
  `false`, `include/Material.h:72`; samples set `s.isDelta=false`,
  `src/MicrofacetMaterial.cpp:165,207`).
- Worker uses the same property to decide deposit/splat eligibility: delta materials are
  excluded from grid deposit and camera splat (`src/Worker.cpp:520`, `src/Worker.cpp:190`)
  because a delta bounce has no diffuse deposit — it is the extension case at gather time.

**[INVARIANT] The BSDF contract is `sample` / `evaluate` / `pdf` / `isDelta`.** New materials
(translucency, volumetric media, flakes-in-glass) are added as "a new BSDF / new medium," NOT
as a new renderer special case. This is the PBRT structure and the deliberate decision to
unify under BSDF *before* adding glass (architecture-vision "the material-special-case
concern → commit to a BSDF interface").

**[INVARIANT] `evaluate` is HELMHOLTZ-RECIPROCAL: `f(wi, wo) == f(wo, wi)`, and zero
when EITHER direction is below the surface.** `LambertianMaterial::evaluate` returns
`albedo/pi` only when both `wi·n > 0` and `wo·n > 0`, else 0. It previously checked only
`wo`, so `f(wi_below, wo_above) = albedo/pi` while `f(wo_above, wi_below) = 0` — a
reciprocity violation at the hemisphere boundary. This only affects the degenerate
boundary case: a correctly-formed deposit/view has both directions above the surface
(the gather's `wi = -incoming` points out of the surface for any real photon arrival),
so correct renders are unchanged. Pinned by `tests/test_BSDFConsistency.cpp` (`[T2]`).

**[DETAIL]** `Material::sampleMode`, `daughterPhotonCount`, and the eager `bounce` wrapper
are inert in the live path (test-only / interface-requirement). They are dead-code-removal
candidates; their presence is not a behavioral statement.

---

## 6. Probe-guided gather (default), legacy density grid, glass, fixture visibility

### 6f. Probe-guided unified gather — [INVARIANT] the default gather (Phase 2a)

The renderer's gather is the probe-guided raw-bounce gather (`$probeGather` true by
default). **[INVARIANT] The camera-side specular trace lives in EXACTLY ONE place —
the probe pass — and the gather is PURE COLLECTION.** Three pieces:

- **Probe pass = THE SINGLE CAMERA-SIDE SPECULAR TRACER**
  (`ProbeGather::collectGatherPoints`, `ProbeIndex`). For every pixel sample (all DOF
  aperture samples for a RealLens camera, all `$cameraTimeSamples` random
  shutter-time samples for a finite shutter, and — for a dielectric first hit — all
  `kCameraSamplesPerPixel` stochastic Fresnel branches) a camera ray is EXTENDED
  through delta surfaces (mirror reflect / glass refract) to its first non-delta hit,
  ACCUMULATING the product of the delta BSDF weights. Each surviving sample emits one
  **`GatherPoint` record** carrying everything the gather needs with no further
  tracing: `{pixel, position, normal, viewDir (wo), materialIndex,
  specularThroughput, unfoldedPathLength, footprintRadius, sampleTime, sampleWeight}`.
  The record POSITIONS are the probe points → uniform-grid index; `anyWithinKeepRadius()`
  is the photon-pass keep-test.
  **[INVARIANT] In a MULTI-CAMERA scene the probes are the UNION over ALL non-debug
  cameras** (`Renderer::renderFrame` collects records from every camera and unions
  their POSITIONS before building the index; the full records are kept PER-CAMERA, so
  a camera's gather reads only its own records — cameras never cross-contaminate). The
  single shared `BounceStore` is gathered once per camera, so the keep-test must
  retain every bounce reachable from ANY camera — otherwise a secondary camera viewing
  geometry (or a specular reflection) the primary cannot see would gather from a store
  whose deposits there were culled, and render black with no warning. The "dropping is
  exact, not lossy" claim below holds for the UNION of cameras, not the primary alone.
  (Debug cameras with a bounce/light filter run no gather and contribute no probes.)
- **Guided raw storage** (`Worker::processPhotons` + `BounceStore`). A non-delta
  bounce is stored RAW (position, incoming dir, power) iff a probe is within the
  keep-radius; else DISCARDED. **[INVARIANT] the keep-test is what bounds memory by
  visible-surface-area, not photon count** — bounces far from every probe can never
  reach the camera, so dropping them is exact, not lossy. Evidence: `bounceCulled`
  ≫ `bounceKept` on a scene with off-camera geometry (`WorkerDebug::bounceKept/
  bounceCulled`); the store size tracks visible area, not the (much larger) total
  bounce count.
- **Unified gather = PURE COLLECTION** (`ProbeGather::run` / `gatherRadiance`). A flat
  thread-partitioned loop over a camera's `GatherPoint` records — **no ray casting, no
  specular recursion, no delta extension** (all of that happened in the probe pass).
  For each record: density-estimate the retained raw bounces at its `position` over
  its precomputed `footprintRadius`, multiply by its `specularThroughput`, weight by
  `sampleWeight` (1/N over the pixel's samples), and add into its `pixel` via the
  atomic buffer: `L = throughput·(1/πr²)·Σ f(wi,wo)·Φ`, with NORMAL-AGREEMENT leak
  suppression (see below). The footprint `r` is a RAY DIFFERENTIAL (min with the
  perpendicular footprint) for a direct hit and the unfolded-path perpendicular
  footprint for a reflected one — both computed in the probe pass and stored on the
  record. A `4/π` parity factor reproduces the retired splat's energy-per-pixel (the
  splat binned a full pixel but normalized by a half-pixel-radius disc).
  **[INVARIANT] Every gather point IS a probe by construction**, so a surface reached
  only through the MINORITY Fresnel branch of a small glass object is probed AND
  gathered with the SAME sampling — the old split (1 probe Fresnel sample vs 16 gather
  samples) could leave such a surface unprobed, its deposits culled, and it read dark;
  that mismatch class is now impossible. Pinned by `tests/test_MinorityFresnelGather.cpp`.

**[INVARIANT] Leak suppression is by NORMAL AGREEMENT, not a tangent-plane distance
cut.** The radius search returns deposits inside a Euclidean SPHERE; near a
corner/edge that catches an ADJACENT perpendicular surface (light leak). A deposit
is kept only if its stored surface NORMAL agrees with the gather point's normal
(`dot >= cos 60°`); a loose tangent-plane band (`2r`) is a coarse backstop only.
The earlier hard tangent-plane cutoff (`0.25r`) over-rejected legitimate deposits
near a CURVED surface's silhouette (their positions bow off the local tangent
plane), darkening it into a black RIM. Normal agreement still rejects a
perpendicular wall (dot≈0) but keeps a smoothly-curved same-surface neighborhood
(dot≈1). `RawBounce` stores the deposit normal for this test, and a per-deposit
photon TIME so the gather is time-aware (motion blur on the default path — §9d;
52 B/record).

**[INVARIANT] The reflected gather footprint is NOT inflated by 1/cos(view) at
grazing.** A mirror is an unfolded straight path, so the reflected perpendicular
footprint is `(pathLength)·tan(halfAngle)` — the same quantity the direct path caps
its gather radius at. An earlier `/min(1, max(0.15, cosView))` inflated the disc up
to ~6.7× at a grazing reflected surface, making reflected ceiling/floor blurrier
than the direct view. The factor is capped at 2× (`max(0.5, cosView)`); brightness
parity does NOT depend on it (the density estimate divides by the same area it
gathers, so r trades sharpness for noise, not energy).

**[INVARIANT] The reflected gather footprint applies the SAME ray-differential
tightening as the direct path (issue #63).** The reflected disc is the MIN of (a) the
adjacent-pixel ray-differential spacing — half the on-surface distance between this
reflected hit and the adjacent pixel's reflected hit, unfolded through the SAME
specular chain — and (b) the perpendicular footprint above (with its 2× grazing cap
as the upper-bound CEILING only). Before this, the reflected disc was ONLY the
perpendicular footprint (capped), with no differential — so reflected contact shadows
washed out and reflected noise smeared, because the disc was often far larger than the
adjacent reflected rays actually spaced on the surface (`reflectedFootprintRadius`,
`src/ProbeGather.cpp`; the differential adjacent hit is computed in the probe pass via
a second `extendAndRecord` of the adjacent pixel's primary ray, using a fresh local
RNG so it never perturbs the main sampling sequence). The differential is taken ONLY
for a DETERMINISTIC mirror chain — a stochastic dielectric (glass) chain would re-walk
a different random Fresnel path, making the differential noise, so glass falls back to
the perpendicular footprint. The 2× cap is preserved as the perpendicular term's
ceiling, so a diverging differential at a grazing reflected surface still cannot
over-blur. Pinned by `tests/test_MirrorDirectParity.cpp`
(`[ReflectedFootprint]`): a unit test on the production `reflectedFootprintRadius`
(a close adjacent hit tightens below perp; a far one stays capped at perp; no adjacent
hit == the perpendicular fallback; the 2× ceiling is exactly 2×, not 1/cos), and a
rendered test where the reflected blob's second moment is tighter than the pre-fix
perpendicular-only blur while the direct blob is unchanged. **Do not "fix" this by**
removing the reflected differential ("the perpendicular footprint is enough"); it
re-blurs reflected contact shadows and noise.

**[INVARIANT] No cos(θ_view) term in the gather.** The density estimate divides by
the on-surface gather AREA only; the deposited photons already carry incoming
geometry and the BRDF handles the view direction (the retired splat's `cosCamera`
was a perpendicular-footprint-to-surface-area correction, folded into the `4/π`
parity factor here). Adding a cos(θ_view) double-darkens grazing surfaces.

Brightness parity with the legacy splat+grid on CornellBoxArea: mean luminance
within ~5% (stable across seeds); the per-region spread is the density-estimate-vs-
splat estimator difference (an accepted, documented bias — see architecture-vision
"biased — density-estimate blur").

### 6a. Density grid — LEGACY (retired from the default path)

The quantized `DensityGrid` + `MirrorGather` reflection lookup is RETIRED: the
probe gather (§6f) replaces it. It is reachable only behind `$probeGather false`
for A/B comparison and is deprecated pending deletion. Its still-valid component
unit tests (`test_DensityGrid`, `test_MirrorGather`, `test_SplatToCamera`) keep the
primitives covered. Historical model (unchanged in the legacy path): energy
accumulated into spatial cells, bounded by occupied cells; **density-of-deposits**
brightness; per-shard locking. Do not build new behavior on this path.

### 6b. Specular gather — EXTEND, do not gather, at a delta vertex (the extension runs ONCE, in the probe pass)

**[INVARIANT]** At a delta hit the camera path follows the single reflection/refraction ray
and extends; it gathers only when the chain lands on a non-delta surface. A perfect mirror
is a path-extension problem, NOT a too-narrow-cone gather. The measure-zero rationale is
load-bearing: a delta BSDF against a POINT camera is a measure-zero event — no stochastically
deposited photon lands in the pixel-footprint reflection cone, so a mirror cannot be GATHERED,
it must be TRACED.

In the DEFAULT probe gather this extension happens in EXACTLY ONE place: the probe pass
(`ProbeGather::collectGatherPoints` → `extendAndRecord`, `src/ProbeGather.cpp`), which walks
each pixel's delta chain ONCE, accumulates the specular throughput, and emits a `GatherPoint`
record at the first non-delta hit (§6f). The gather then does NO extension — it is pure
collection over those records. (Historically the chain-walk was written TWICE — a throughput-
discarding probe walk and a separate `shade()` recursion at gather time — with drifted
sampling that caused the minority-Fresnel cull bug; both were collapsed into the one probe-pass
walk. The legacy `MirrorGather` keeps its own walk on the deprecated `$probeGather false`
path.) Recursion depth is capped at `kMaxSpecularDepth = 8` — a hall of mirrors returns black
at the cap, it does not infinite-loop.

### 6c. Glass camera path — STOCHASTIC Fresnel, single ray — looks like a bug, it is not

**[INVARIANT] At a dielectric camera hit, take ONE stochastic Fresnel pick via
`DielectricMaterial::sample()` (reflect with prob R, refract with prob 1−R, weight = tint),
averaged over `kCameraSamplesPerPixel` samples. Do NOT trace both the reflect and refract
branches (Whitted-style).**

- Single stochastic pick: `src/DielectricMaterial.cpp:113-128`; camera path uses it and
  averages `kCameraSamplesPerPixel = 16` samples on dielectric first-hits
  (`src/MirrorGather.cpp:197-227`). Mirror first-hits are deterministic → a single sample
  (`stochasticDelta == false`, `src/MirrorGather.cpp:197-199`).
- Unbiased: selection probability cancels the lobe weight, so the average converges to
  `R·reflect + (1−R)·refract` (`src/DielectricMaterial.cpp:113-117`).

**Why it looks like a bug, and why it is not:** the *first* glass implementation traced both
branches per dielectric hit (Whitted), which is `2^depth` per pixel — glass+mirror+diffuse
could not finish a 128px frame in 10 minutes. The both-branch path was **deliberately
removed** and replaced with the stochastic single-ray pick (128px: >10 min → 29s).
Reasoning: architecture-vision "Camera-cost fix (exponential → linear)".

**Do not "fix" this by:** "restoring" both-branch reflect+refract tracing because the
stochastic glass looks noisy. The noise lever is `kCameraSamplesPerPixel` (raise it for
cleaner glass at *linear* cost), not exponential branch tracing.

### 6d. Area-light fixture visibility — emitter deposits in the unified gather (probe mode)

**[INVARIANT] In probe mode a light FIXTURE is made camera-visible by depositing its own
surface radiance as RAW BOUNCES on its surface**, which the unified gather collects exactly
like any other surface's deposits — so the fixture renders DIRECTLY and in MIRRORS with **no**
special-case pass. The emitter is also a first-class gatherable surface in the probe pass and
the gather's `firstHit` (intersected as an `EmitterPatch`; an emitter Hit carries the
`kEmitterMaterial` sentinel and is gathered with an IDENTITY BRDF, f = 1).

- `surfaceRadiance = luminousFlux / area / π` for a Lambertian emitter (`src/AreaLight.cpp`).
- `ProbeGather::depositEmitters` tiles each patch with deposits and gives each
  `power = radiance · π · area / (4·N)`. With the identity BRDF and the `4/π`
  splat-parity factor, the density estimate over the patch reproduces `L = M/π`
  regardless of `N` or footprint `r` (uniform areal density ρ gathers to `L = (4/π)ρ`,
  so total patch power = `radiance·π·area/4`). The panel reads at the SAME brightness as
  the legacy `EmissiveGather` wrote (verified: identical saturated panel).
- **[INVARIANT] Emitter deposits are appended to the `BounceStore` BEFORE the photon
  pass — they RESERVE their slots first (issue #62).** The store drops every append
  past its capacity ceiling (`BounceStore::append` fetch_add; `slot >= capacity` ⇒
  dropped). When `depositEmitters` ran AFTER the photon pass (the old order), a scene
  whose photons filled the store to capacity dropped EVERY emitter deposit, blacking out
  the fixture at high photon counts. Depositing emitters first guarantees the fixture's
  own radiance is always stored; the photon pass then competes only for the REMAINING
  slots. `bounceStore->buildIndex` still runs once after the photon pass drains, indexing
  the full populated prefix (emitter + photon deposits). Pinned by
  `tests/test_BounceStoreEmitterOverflow.cpp` (a tiny `$bounceStoreCapacity` forces a
  deterministic photon-pass overflow; the emitter panel stays lit and matches the
  no-overflow baseline). **Do not "fix" this by** moving the emitter deposit back after
  the photon pass.
- Deposits pass the same probe keep-test as every other bounce, so an off-camera fixture
  costs nothing.

**[LEGACY] `EmissiveGather` is retained ONLY for the `$probeGather false` path.** It composites
the fixture by intersecting each pixel ray against the emitter patch and writing
`surfaceRadiance` (no primary-ray-vs-light bypass in the tracer). Its coplanar-occlusion
epsilon (the z-fix) must not be removed on that path: an occluder is ignored only if it sits
*past* `bestT · (1 − kOcclusionCoincidenceMargin)` with `kOcclusionCoincidenceMargin = 1e-3`
(`src/EmissiveGather.cpp`). This relative margin lets a ceiling mesh coplanar with the emitter
(hit at `t ≈ bestT`) NOT self-occlude, while genuine occluders sit far inside `bestT` and
still block. The emitter-patch geometry/intersection is shared with the probe gather via
`EmitterPatch` (`include/EmitterPatch.h`).

**[DETAIL]** The area light is pulled a few mm off the ceiling plane as a physical fix for
panel z-fighting; the `EmissiveGather` epsilon is kept anyway as general numerical hygiene
(architecture-vision "Glass shipped").

---

## 6e. Camera projection model — rectilinear perspective, NOT f-theta

**[INVARIANT] The default camera projection is RECTILINEAR PERSPECTIVE (pinhole), NOT
f-theta.** A pixel maps to a primary ray through a FLAT image plane via the tangent of the
field of view; straight world lines stay straight on screen. Concretely, for normalized
screen coords `sx, sy` in `[-1, 1]` (sx left→right, sy bottom→top), with
`aspect = width/height` and `halfHeight = tan(verticalFieldOfView/2)`:

```
direction = normalize(forward + sx*aspect*halfHeight*right + sy*halfHeight*up)
```

The camera basis is `right = rot·X`, `up = rot·Y`, `forward = rot·Z`
(`src/Camera.cpp` `generatePrimaryRay`).

**[INVARIANT] Do NOT restore the old f-theta (equidistant fisheye) ray-gen.** The renderer
historically built the ray as `angle = pixelOffset * angularStep` and took
`(sin(angle), sin(angle), cos·cos)` — an f-theta / equidistant construction. That mapping
makes the screen offset proportional to the *angle* instead of `tan(angle)`, so straight
edges BOW outward near the frame (barrel distortion — visible on Cornell-box walls at wide
FOV). This was a deliberate correction, not a refactor. A future agent must not "simplify"
`generatePrimaryRay` back to the sin/cos angular form. The unit test
`tests/test_CameraProjection.cpp` pins the tan-not-theta identity precisely so a regression
to f-theta fails the suite.

**[INVARIANT] The reverse projection is the exact inverse of the forward ray-gen.**
`Camera::coordForPoint` / `coordForPointSubPixel` (used by the forward photon SPLAT,
`Worker::splatToCamera`) project a world point with the same rectilinear math
(`dx/depth / (aspect*halfHeight)` etc.). Forward ray-gen and reverse splat MUST agree, or a
photon's splat lands in a different pixel than the gather ray for that pixel — the two
projections are a matched pair and must change together.

**Projection types (scene key `$projection`, per camera; default `"perspective"`).** A
camera declares `Camera::Projection` (`include/Camera.h`); `SceneLoader` maps the JSON key.
An absent key defaults to perspective, so all pre-existing scenes render unchanged (modulo
the f-theta→perspective correction above).

- **`perspective`** — the rectilinear pinhole above. Ray origin = eye for every pixel.
- **`orthographic`** — PARALLEL rays: every pixel's direction is `forward`; the ORIGIN varies
  across the image plane: `origin = eye + sx*aspect*(orthoHeight/2)*right +
  sy*(orthoHeight/2)*up`. No perspective convergence — parallel world lines stay parallel and
  object size is constant with depth. Param: `$orthoHeight` (world-space frame height; width
  follows from aspect).
- **`reallens`** (thin-lens depth of field) — like perspective but the ray ORIGINATES from a
  point sampled on a finite aperture DISK (in the `right`/`up` lens plane) and is aimed at the
  FOCUS POINT where the pinhole ray crosses the focus plane at `$focusDistance`. Result:
  sharp at `$focusDistance`, blurred elsewhere; moving `$focusDistance` moves the focus.
  Params: `$apertureRadius` (explicit lens-disk radius; if `<= 0`, derived as
  `$focalLength / (2 · $fNumber)`), `$focusDistance`, `$focalLength`. The radius actually used
  is `Camera::effectiveApertureRadius()` (the explicit `$apertureRadius` if `> 0`, else the
  derived value). All params default to a 0 effective aperture absent the scene keys, i.e. a
  pinhole (no DOF) — see the invariants below.

### Thin-lens DOF model (probe-pass aperture sampling)

DOF is **sampled noise, never a post-process or a stepped approximation** — it integrates over
the camera samples exactly like motion blur integrates over shutter time. The implementation
lives entirely in the **probe pass** (`ProbeGather::collectGatherPoints`), the single
camera-side tracer: for a reallens camera with non-zero aperture, each pixel shoots
`kCameraSamplesPerPixel` PRIMARY samples, each originating from a fresh aperture-disk point and
aimed at that pixel's focus point. Every surviving sample emits a `GatherPoint` record at its
first non-delta hit; the per-pixel `sampleWeight = 1/N` averages them. The blur is the SPREAD
of where those converging rays land on off-focus geometry — many records per pixel scattered
across nearby surface points, each gathered over a tight (sharp-image-scale) footprint. More
samples ⇒ less noise, zero bias. Because the probe pass now images EVERY pixel (diffuse,
glass, mirror, emissive) — not just delta/emissive ones via a cast ray — **DOF applies to all
surface types, diffuse included.** (This supersedes the pre-probe-restructure note that diffuse
surfaces showed no DOF because they were imaged by the forward photon splat; the splat path is
retired behind `$probeGather true` — §6f.)

**The sampling hook.** `Camera::generatePrimaryRay(coord, generator)` /
`generatePrimaryRayAt(coord, time, animation, generator)` take an optional RNG: with it each
call jitters the sub-pixel film position AND (for reallens) the aperture-disk sample (uniform
disk via `r = R·√ξ₁`, `θ = 2πξ₂`). The focus point is fixed by the FILM sample (the pinhole
direction), independent of the aperture sample — all aperture samples for one film point
converge on the focus plane (PBRT thin-lens model). The probe pass passes a generator on the
reallens path and none otherwise, so the matched probe/gather projection pair stays consistent:
whatever ray the probe casts is what the footprint + gather assume. The direct-hit footprint
(`pixelFootprintRadius`) differences the hit against the pinhole-CENTER adjacent-pixel ray (no
generator), giving each aperture sample a sharp-image-scale gather disc — correct, since the
blur must come from the inter-sample spread, not from inflating each sample's disc.

**Circle of confusion.** To first order CoC ∝ `effectiveApertureRadius · |depthHit −
focusDistance| / depthHit`: zero at the focus plane, growing ~linearly with defocus and with
aperture. Numerically verified (silhouette transition-width oracle) in `test_DepthOfField.cpp`.

**[INVARIANT] DOF off == pinhole, byte-for-byte.** `dofActive` in the probe pass gates on
`projection == RealLens` **AND** `effectiveApertureRadius() > 0`. A perspective camera, OR a
reallens camera whose effective aperture is 0, takes the SAME single-sample, pixel-center,
no-generator path — so the no-DOF image is identical to the legacy renderer. The reallens ray
with a 0 effective aperture also reduces geometrically to the pinhole ray (every aperture
sample collapses to the eye), so the two are consistent. `test_DepthOfField.cpp` pins both: a
zero-effective-aperture reallens ray equals the perspective ray for every pixel, and a
zero-aperture reallens RENDER matches a pinhole render within the Monte-Carlo noise floor.

**[INVARIANT] Focus plane stays sharp under aperture change; off-focus blur grows with
aperture.** Increasing `$apertureRadius` widens the off-focus circle of confusion but leaves
the focus-plane silhouette unchanged (within noise) — the regression test asserts the
focus-distance object's edge width is flat across apertures while the near/far objects' edge
widths grow monotonically.

---

## 7. Camera exposure, physical units, distribution-readiness

**[INVARIANT] Lights are in physical photometric units** (candela; `flux = I · solid-angle`)
and the camera exposure is a physical f-number / shutter / ISO model (one stop = 2×;
architecture-vision "Wave 2"). The splat writes physical luminance the tonemap consumes
unchanged.

**[INVARIANT] Buffers are additive and distribution-ready.** `Buffer::addColor` is a
per-channel lock-free `atomic<float>::fetch_add`. Because magnitude is baked at emission
(§3), independent machines rendering disjoint photon sets can simply SUM their buffers for
the correct result — no shared photon-count coordination. This property is *load-bearing for
the distributed-rendering goal*; do not introduce a gather-time term that depends on the
global photon count, which would break summability.

**[INVARIANT/knob] Per-light fidelity is `count × bundle-brightness` at fixed energy.** A
hero light can use many dim photons (smooth); a background light few bright photons (noisier)
for the same energy — a supported aesthetic lever, not a bug (architecture-vision "Per-light
fidelity"). A deliberately sparse-bright light is intended, not under-sampled.

**[DETAIL]** Motion-blur / rolling-shutter machinery: photons carry an emission `time`
(`include/Photon.h:26`); the splat gates on `Camera::exposureWindowForPixel`
(`src/Worker.cpp:248-252`). See §9 for the full animation/time model and the
shutter normalization.

---

## 8. Tooling invariant — use the right binary and gates

**[INVARIANT] The headless renderer is `build/Release/ray-tracer <scene>.json`.** It reads
`$width` / `$photonsPerLight` from the scene and writes `renders/<renderName>.0.png`
(`src/main.cpp:20-83`). It calls the *same* `Renderer::renderFrame` as everything else.
For an animation it loops `$startFrame`..`$endFrame`, mapping each frame to a time
(§9c) and writing `<renderName>.<frame>.png`.

**`editor --render-test` is NOT a faster/alternate render path — do not use it for renders.**
It runs the identical `Renderer::renderFrame` but through the GLFW/ImGui/OpenGL-linked
`editor` binary and is pathologically slow for headless work. A past multi-hour "deadlock /
perf regression" scare was a wrong-binary artifact, not a pipeline problem (architecture-
vision "TOOLING LESSON"). Render via `ray-tracer`.

**[INVARIANT] Required gates before changing renderer behavior:** the test suite
(`cmake --build --preset conan-release --target tests` → `build/Release/tests/tests`), plus
ASan/UBSan and TSan clean, warnings-as-errors (`-Werror`, gated by `RAYTRACER_WERROR` /
`-fsanitize=` in `CMakeLists.txt:29,52,62`), and clang-tidy (`.clang-tidy`). A behavior
change that is only "green on the default build" has not cleared the bar — the sanitizers and
the invariants in this doc are part of the gate.

### 8a. Deterministic test mode — SINGLE-THREAD seeded render — [INVARIANT]

**[INVARIANT] `$deterministic true` makes a render BITWISE-reproducible by running the
photon pass on ONE worker with a seeded RNG and the gather single-threaded.** Two
`Renderer::renderFrame` calls on the same `$deterministic` scene produce byte-for-byte
identical float buffers (and identical deposit ledgers). This is the deliberate design:
bitwise determinism is achieved by the SINGLE worker, **not** by per-worker seeding.
Multi-threaded runs cannot be bitwise-reproducible because the atomic-`float` buffer
adds (`Buffer::addColor`) are non-associative — thread interleaving perturbs the low
bits — so the deterministic mode collapses `workerCount` to 1 and runs the probe pass +
gather serially (`RenderSettings::deterministic`, `Renderer::renderFrame`'s
`effectiveWorkerCount`).

- `$seed` (`RenderSettings::seed`, sentinel `kUnseeded`) plumbs a FIXED base RNG seed.
  In deterministic mode the worker, the probe pass, and the gather are all seeded from
  it. In a NON-deterministic but seeded run, workers seed from `seed + workerIndex`
  (reproducible per-worker, **no** bitwise guarantee — the non-associative adds remain).
  An absent `$seed` leaves the production `std::random_device` seeding.
- `Worker::setSeed` replaces the `random_device`-seeded `m_generator`;
  `ProbeGather::collectGatherPoints` takes an optional `seed` for the camera-side
  sampling RNG.
- **Do not "fix" this by** seeding per worker and expecting bitwise reproducibility in a
  multi-thread run — the float-add order is not fixed there. The single-worker mode is
  the only bitwise-deterministic configuration.

Pinned by `tests/test_Determinism.cpp` (bitwise equality across two renders;
1-vs-8-worker mean-luminance equivalence with NO bitwise claim).

### 8b. Test-visibility hooks — declared APIs, not re-implemented math

Several internals are exposed to the objective test suite (`docs/test-plan-fable-2026-06.md`)
so tests assert against PRODUCTION code rather than a re-derivation (the
self-consistent-but-wrong trap §intro warns about). Widening their visibility is **not** a
behavior statement; the renderer's own call sites use the same definitions.

- **`ProbeGather::testing`** (`include/ProbeGather.h`): `gatherRadiance` (the per-record
  density estimate), `pixelFootprintRadius` / `reflectedFootprintRadius`, and
  `extendToNonDelta` (the camera-side specular trace), hoisted out of the `.cpp`
  anonymous namespace. Precedent: `Worker::splatToCamera` was made public for tests.
- **`WorkerDebug::selfHitThreshold()`** (`include/Worker.h`): the production `1e-4`
  self-hit floor (§2b), so a test pins the real constant instead of a duplicated literal
  (a reversion toward `DBL_EPSILON` then fails `tests/test_SelfHitLive.cpp`).
- **`RenderResult::buffer` / `CameraRender::buffer`** are the PRE-tonemap float radiance
  buffers; tests assert on radiance, not the 16-bit tonemapped pixels (which quantize
  away small regressions). `tests/RenderFixture.h` wraps the load→render→stat path.

---

## 9. Animation, per-photon time, and motion blur — the time model

The renderer never advances "the scene" to a discrete frame. Every time-varying
quantity is a function of a continuous timestamp, and each photon carries its own
time. Over many photons spread across a frame's shutter, time-varying geometry
smears into motion blur. This composes with the single-photon + bundled-magnitude
model (§1, §4) — time is orthogonal to the energy model.

### 9a. `Property<T>` — the animation atom

A `Property<T>` (`include/Property.h`) is either a CONSTANT or an ANIMATED keyframe
curve, with `T evaluate(double timeSeconds)`. `double` and `Vector` are the
instantiated types. Interpolation is a **cubic Hermite spline** with Catmull-Rom
auto-tangents (or explicit per-keyframe tangents).

**[INVARIANT] A CONSTANT Property is time-independent — `evaluate(t)` returns the
same value for all `t`.** A scene built from constant Properties (i.e. any
non-animated object) is therefore bit-for-bit the pre-animation baseline. Do not
make `evaluate` on a constant depend on `t`.

**[INVARIANT] At a keyframe's exact time, `evaluate` returns that keyframe's value
exactly; outside the authored range the endpoint value is HELD (clamped), not
extrapolated.** Velocity (the first derivative) is C1-continuous across interior
keyframes — this is load-bearing: **blur length tracks instantaneous speed**, so an
eased spin-up smears progressively rather than in steps. The interpolator is pinned
by `tests/test_Property.cpp` (endpoint exactness, hold-outside-range, eased
midpoints, C1 velocity continuity, zero-tangent ease).

`KeyframedAnimationQuery` (`include/AnimationQuery.h`) maps object name → animated
transform: a position `Property<Vector>` plus a **scalar-angle `Property<double>`
about a fixed axis**, composed onto the object's scene-load orientation. The angle
parameterization (vs a quaternion curve) makes ANGULAR VELOCITY a direct smooth
function of the keyframes — the thing that drives blur length. An object with no
entry returns `std::nullopt` from `transformAt`, so callers fall back to the
scene-load transform (static object → unchanged).

### 9b. Time threads photon → intersection → splat

- **Emission** (`src/Worker.cpp:438-461`): each freshly-emitted photon is stamped
  with a time from the camera's global exposure window (§9c).
- **Intersection at time** (`Volume::castRayAt`, `src/Volume.cpp:34-49`): the
  volume's world transform is resolved by the `AnimationQuery` AT THE RAY'S TIME, so
  a ray cast at time `t` hits the object at its time-`t` pose. The photon's full
  random walk (and the splat's occlusion ray) all cast at the photon's time
  (`src/Worker.cpp:496,286`).
- **Splat** projects through the camera; the per-pixel exposure-window gate
  (`src/Worker.cpp:248-252`) admits the photon. **[DETAIL] camera-at-photon-time on
  the LEGACY splat:** the LEGACY (`$probeGather false`) diffuse splat projects
  through the camera's STATIC transform (`Worker::splatToCamera` →
  `coordForPoint`, a static pinhole with no aperture model, so the legacy splat path
  carries neither DOF nor camera motion blur — it is a sharp pinhole projection). The
  DEFAULT
  (`$probeGather true`) path DOES — its camera rays are generated at the ray's
  shutter time and so resolve an animated camera's pose at that time (§9e). Camera
  motion blur is therefore a guaranteed behavior of the default gather, not the
  retired splat.

### 9c. Shutter + photometric normalization — [INVARIANT], do not "fix"

Frame `f` maps to time `frameOffset + f / frameRate` (`src/main.cpp`). The
Renderer sets every camera's global exposure window from that frame's shutter
(`src/Renderer.cpp`):

- **Finite shutter** `$shutterTime > 0`: window `[t_open, t_open + shutter)`.
  Photons are stamped with a time sampled UNIFORMLY in that half-open interval
  (`src/Worker.cpp:438-461`) and the scene is evaluated per-photon at that time.
- **Zero shutter**: window `[t_open, +inf)`. Emission stamps EVERY photon at exactly
  `t_open` (no jitter); the gate admits all. For the default static `frameTime = 0`
  this is `time = 0` — the exact baseline.

**[INVARIANT] Spreading N photons over the shutter does NOT change total light
energy.** `emit()` still produces the same photon COUNT carrying the same
per-photon flux (Φ/N, baked at emission, §3). The shutter only re-tags each
photon's TIME; it does not scale magnitude. Every sampled time lies INSIDE the
half-open window, so the splat's exposure-window gate never drops a photon. Hence a
STATIC scene rendered with a shutter has the SAME brightness as one rendered
without. Verified: `tests/test_ShutterBrightness.cpp` matches within ~0.1%.

**Do not "fix" this by:** using a tiny `[t, t+ε)` window for the zero-shutter case.
Sub-ulp emission times round to the EXCLUSIVE window end and fail the
`contains()` gate, silently dropping ≈half the photons and ≈halving image energy.
This was a real, fixed bug — the zero-shutter window is half-infinite on purpose
(`src/Renderer.cpp`, `src/Worker.cpp:438-461`). Do not reintroduce an ε-window.

### 9d. The probe gather is TIME-AWARE — [INVARIANT] camera rays carry a time

**[INVARIANT] Every camera-side ray in the probe gather is cast at a SCENE TIME,
not a hardcoded 0.** The probe pass — the single camera-side specular tracer — casts
the per-pixel first hit, the specular extension chain (`extendAndRecord`), and the
emitter intersection at the sample's time via the same `castRayAt(..., time,
animationQuery)` the photon pass uses (`src/ProbeGather.cpp`), stamping each
`GatherPoint` record with its `sampleTime`. `collectGatherPoints` takes the frame
time + shutter; `Renderer::renderFrame` forwards `settings.frameTime` /
`settings.shutterTime` to it, mirroring the photon side. The gather is pure
collection over the records, so it needs no rays of its own — it just uses each
record's stamped `sampleTime` as the temporal-window reference. This is what makes
animation work in the DEFAULT (`$probeGather` true) path — the camera sees the scene
at the time the photons lit it.

History: the probe gather was originally time-blind (every camera ray cast at
`time = 0`), so an animated scene rendered with the default gather showed frame-0
geometry while only the lighting moved, and the §9 motion-blur deliverable imaged
only through the now-retired splat path. That contradiction is fixed; the splat path
is no longer the motion-blur carrier.

**[INVARIANT] Motion blur on the camera side is per-sample RANDOM shutter time.**
With a finite shutter each per-pixel camera sample (in the probe pass) draws a
uniform time in `[frameTime, frameTime + shutterTime)` (`$cameraTimeSamples` samples,
averaged at gather time via each record's `sampleWeight`), matching the per-photon
emission-time model. The directly-visible AND the reflected (specularly-extended)
moving geometry therefore integrate over the shutter into object motion blur — a
reflected gather point carries its own `sampleTime`, so a moving object's reflection
in a mirror blurs exactly like the direct view. A zero shutter is a single fixed-time
sample at `frameTime` (the exact static baseline at `frameTime` 0).

**[INVARIANT] Probe temporal coverage and the gather's continuity are DECOUPLED.**
The camera sees a moving object at a CONTINUUM of poses across the shutter, but
probes collected at a single instant would miss the later poses, so the photon-pass
keep-test would cull the deposits the camera actually gathers (the object goes dark).
Because the probe pass IS the camera sampler, this is automatic: each of the
`$cameraTimeSamples` per-pixel samples carries its own random shutter time, so the
union of record positions COVERS every pose the camera sees during the shutter — the
same samples that probe are the ones that gather, so coverage and gather sampling can
never drift apart (the source of the old minority-Fresnel cull bug, §6f). (The
earlier split design needed a SEPARATE `$probeTimeSlices` discrete-time probe sweep to
approximate this coverage; that knob is retired — every sample carries a continuous
time.) Probe count governs COVERAGE — was a bounce near ANY camera-reachable pose —
NOT gather smoothness. The GATHER itself stays CONTINUOUS: it keeps a deposit only if
its photon time is within a shutter-sized temporal window of the record's sampleTime
(`RawBounce::time`, +4 B/record, 52 B total). A deposit is kept if it is near a probe
in SPACE (the keep-test, conservative) AND within a covered time window of the camera
sample (the gather); discrete samples in time, continuous weighting in the gather.

**[INVARIANT] The temporal window preserves STATIC-scene parity.** The window is the
full shutter span, so a static surface — whose deposits sit at one world position
regardless of their shutter-spread photon times — keeps every one of them (the time
filter is inert on static geometry; the pre-animation baseline is bit-for-bit
unchanged modulo Monte-Carlo noise). A MOVING surface self-filters SPATIALLY: a
deposit from a far-off time was laid where the object was THEN, a different position
already excluded by the radius search; the temporal window is the backstop against
two distinct poses that overlap within the gather radius. Emitter deposits carry a
TIMELESS sentinel time (`RawBounce::kTimelessDeposit`) and pass at any camera time
(the fixture is static).

Verified: spinning-fan (`CornellBoxFan.json`) in the default probe path renders the
fan at the correct per-frame orientation (not pinned at frame 0) AND motion-blurs on
fast frames; a moving object's reflection in a mirror blurs on fast frames and is
sharp on slow ones; static scenes (`CornellBoxArea`) match the pre-change mean
luminance within run-to-run noise. Pinned by `tests/test_AnimatedGather.cpp`
(intersection-at-time + gather-at-time render).

### 9e. The CAMERA transform is evaluated at the ray's time — [INVARIANT] camera motion blur

§9d makes every camera-side ray carry a scene TIME and resolves animated GEOMETRY
at that time. **[INVARIANT] The CAMERA's own transform (eye position + orientation)
is ALSO resolved at each camera ray's time**, so when the camera itself moves or
pans fast across a non-zero shutter the (even static) geometry it images integrates
over the camera's poses into directional motion blur. Without this a fast-moving
camera would NOT blur — the geometry would be sampled at many times but always
imaged from the camera's frame-0 pose, leaving static scenery sharp.

- A camera carries keyframed position/orientation exactly like a scene object: the
  scene loader registers a camera's `$animation` block in the same
  `KeyframedAnimationQuery`, keyed by the camera's `name()`
  (`SceneLoader::parseObjectFromJson` applies `$animation` to ANY object, cameras
  included). No camera-specific animation type.
- `Camera::resolveEyeRotationAt(time, animation)` looks the camera's pose up from
  the `AnimationQuery` by `name()` at the ray's time — the camera-side analogue of
  `Volume::resolveTransformAt` (`src/Camera.cpp`). `Camera::generatePrimaryRayAt`
  builds the ray from that resolved pose via the SAME projection body as the static
  `generatePrimaryRay` (one shared `buildPrimaryRay(eye, rotation)` — perspective /
  orthographic / reallens math lives in one place).
- The DEFAULT probe path generates every camera ray at the ray's time: the probe
  pass (`collectProbeRows`, so the keep-test covers every pose the moving camera
  sees across the shutter — otherwise a fast move blacks out the swept-in geometry),
  the per-pixel gather primary ray, and the ray-differential footprint
  (`pixelFootprintRadius`) all call `generatePrimaryRayAt(coord, time, animation)`
  (`src/ProbeGather.cpp`). With the per-sample random shutter time of §9d, the
  poses integrate into blur.

**[INVARIANT] STATIC-camera parity is byte-for-byte.** With a null `animation`, or a
camera with no animation entry, `resolveEyeRotationAt` returns the scene-load
(eye, rotation) for ALL times, so `generatePrimaryRayAt` is identical to the static
`generatePrimaryRay`. A non-animated camera (every pre-existing scene) is unchanged.

**[DETAIL] The LEGACY (`$probeGather false`) path keeps a static camera.** The
legacy splat (`coordForPoint`) and `MirrorGather`/`EmissiveGather` still image
through the camera's static pose (consistent with §9b's legacy-splat note and the
no-DOF-on-diffuse boundary in §6e); camera motion blur is a property of the default
gather only. Whenever both a deposit-projection and a gather camera ray ARE
exercised together they must be generated at the SAME time (the matched-pair rule of
§6e); in the default path only the gather generates camera rays, so the pair is
trivially consistent.

Verified: a fast-TRANSLATING camera over a static red/green-seam wall smears the
vertical seam (max red-channel column step `static ~6500 → moving ~2840`, ratio
~0.44) while the zero-shutter same-pose render stays crisp; a fast-PANNING camera
likewise smears the sharpest wall edge (`88 → 3.9`). Before the change moving and
static were the identical sharp image (ratio ~1.0). Pinned by
`tests/test_CameraMotionBlur.cpp` (the `resolveEyeRotationAt`/`generatePrimaryRayAt`
pose unit test + the moving-vs-static seam-steepness regression). Proof renders +
scenes: `CornellBoxCameraMoveTranslate{,Static}.json`, `CornellBoxCameraPan{,Static}.json`.

---

## Appendix: code ↔ notes discrepancies found while writing this doc

- **`research/ray-tracer-code-review-2.md` §1f / §2c is stale and WRONG on this branch.** It
  claims the single-photon continuation uses `sampleMode()` (BRDF mode) via an
  "index-0-is-mode" rule and flags possible bias. The current code calls `sample()`
  unconditionally in `generateDaughters` (`src/Material.cpp:93`); there is no `sampleMode`
  branch in the live scatter. Trust the code. (Recorded as the canonical false alarm in §1.)
- The architecture-vision notes contain superseded states by design (relative-cutoff
  termination, Whitted both-branch glass, daughter fan-out, RR). The *current* code is:
  absolute-floor termination (§2a), stochastic single-ray glass (§6c), single-photon scatter
  (§1), no RR. Where a note describes an earlier state, this doc reflects the code.
