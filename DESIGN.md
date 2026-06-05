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
(`src/Worker.cpp:244-249`). Default window is infinite (no behavior change).

---

## 8. Tooling invariant — use the right binary and gates

**[INVARIANT] The headless renderer is `build/Release/ray-tracer <scene>.json`.** It reads
`$width` / `$photonsPerLight` from the scene and writes `renders/<renderName>.0.png`
(`src/main.cpp:20-83`). It calls the *same* `Renderer::renderFrame` as everything else.

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
