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

Single-machine forward (light-side) photon tracer with a camera-side gather for specular.
The shape is **emit → trace-each-photon-to-completion → gather**:

1. **Emit.** Each `Light` produces photons into the one bounded `photonQueue`. The
   per-photon magnitude `Phi/N` is baked at emission (`LightQueue::registerLight`,
   `src/LightQueue.cpp:16`).
2. **Trace to completion.** A worker fetches a batch and traces each photon's *entire*
   random walk in a single loop (`Worker::processPhotons`, `src/Worker.cpp:439-589`):
   raycast → deposit into the density grid (non-delta only) → splat to camera (non-delta
   only) → check terminators → scatter exactly one continuation photon → repeat. The
   continuation lives in a local stack slot; it **never re-enters the queue**. The source
   batch's queue slots are released only after the whole batch finishes
   (`src/Worker.cpp:582`).
3. **Gather (post-pass, per camera).** `MirrorGather` traces camera rays, extends through
   delta surfaces, and reads radiance from the density grid at diffuse ends.
   `EmissiveGather` makes light fixtures camera-visible.

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
  attenuates (`src/Worker.cpp:536-542` comment).
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

**[DETAIL]** `Material::sampleMode`, `daughterPhotonCount`, and the eager `bounce` wrapper
are inert in the live path (test-only / interface-requirement). They are dead-code-removal
candidates; their presence is not a behavioral statement.

---

## 6. Density grid, specular gather, glass, and fixture visibility

### 6a. Density grid — [INVARIANT] quantized, additive, distribution-ready

Energy is accumulated into spatial cells and the bounce discarded; storage is bounded by
**occupied cells**, not photon count (`src/DensityGrid.cpp:25-33`). The brightness model is
**density-of-deposits**: sum of per-photon bundle magnitudes in a cell, divided by cell
footprint area at lookup. Per-shard locking (`shardOf` → `lock_guard`) lets distinct cells
proceed concurrently. This replaced an earlier per-photon-record hash-grid that blew up
memory (15.6 GiB at 2M photons) — do not revert to storing every bounce record
(architecture-vision "quantized density grid" pivot).

### 6b. Specular gather — EXTEND, do not gather, at a delta vertex

**[INVARIANT]** At a delta hit the camera path follows the single reflection/refraction ray
and recurses (`reflectedRadiance`, `src/MirrorGather.cpp:113-138`); it gathers the grid only
when the chain lands on a non-delta surface (`src/MirrorGather.cpp:147`). A perfect mirror is
a path-extension problem, NOT a too-narrow-cone gather. Recursion depth is capped at
`kMaxSpecularDepth = 8` (`src/MirrorGather.cpp:25,86`) — a hall of mirrors returns black at
the cap, it does not infinite-loop.

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

### 6d. Area-light fixture visibility — emissive deposit, no special camera-vs-light path

**[INVARIANT]** A light FIXTURE is made camera-visible by depositing its own surface radiance
into the gather (`EmissiveGather`), exactly like any other lit surface — there is **no**
special primary-ray-hits-light bypass path. `surfaceRadiance = luminousFlux / area / π` for a
Lambertian emitter (`src/AreaLight.cpp`, written to the pixel in `EmissiveGather`). This
generalizes to any emissive material. Reasoning: architecture-vision "Area light shipped +
emissive-fixture visibility".

**[INVARIANT] The `EmissiveGather` coplanar-occlusion epsilon (the z-fix) must not be
removed.** An occluder is ignored only if it sits *past* `bestT * (1 − kOcclusionCoincidence-
Margin)` with `kOcclusionCoincidenceMargin = 1e-3` (`src/EmissiveGather.cpp:40,217`). This
relative margin lets a ceiling mesh coplanar with the emitter (hit at `t ≈ bestT`) NOT
self-occlude, while genuine occluders (walls, blocker spheres) sit far inside `bestT` and
still block. Removing it brings back salt-and-pepper occlusion noise on the light panel.

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
  `$focalLength / (2 · $fNumber)`), `$focusDistance`, `$focalLength`.

**DOF sampling hook + its boundary.** `generatePrimaryRay(coord, generator)` takes an
optional RNG: with it, each call jitters the sub-pixel film position AND (for reallens) the
aperture-disk sample, so DOF integrates over a samples-per-pixel loop. The focus point is
fixed by the FILM sample (pinhole direction), independent of the aperture sample — all
aperture samples for one film point converge on the focus plane (PBRT thin-lens model).
`MirrorGather` multi-samples the primary ray (`kCameraSamplesPerPixel`) when the camera is
reallens, so MIRROR/GLASS (delta) and emissive pixels — the ones imaged by a *cast camera
ray* — show DOF. **[DETAIL] DIFFUSE surfaces do NOT show DOF**: they are imaged by the
forward photon SPLAT (`coordForPoint`), which has no aperture model. Full diffuse DOF would
require circle-of-confusion splatting keyed on each photon hit's depth vs. the focus plane —
deliberately out of scope; the splat stays a sharp pinhole projection.

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
  (`src/Worker.cpp:248-252`) admits the photon. **[DETAIL] camera-at-photon-time:**
  the per-photon-time path is wired end-to-end; the diffuse splat currently projects
  through the camera's static transform (camera animation → camera motion blur is
  the same per-photon-time mechanism but the splat's `coordForPoint` is a static
  pinhole, consistent with the no-DOF-on-diffuse boundary in §6e).

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

**Deferred (Phase 2): continuous-time specular reflections.** The density grid
(§6a) compresses bounce energy over the whole frame, so a time-varying object's
REFLECTION in a mirror is smeared across the frame, not crisply time-resolved. The
fan deliverable keeps the fan DIFFUSE so it images via the direct per-photon-time
splat. Crisp time-varying reflections are out of scope; do not change the reflection
storage to chase them here.

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
