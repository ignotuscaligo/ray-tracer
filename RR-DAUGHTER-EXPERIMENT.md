# Russian Roulette + Configurable Daughter Count

Branch `renderer-rr-daughters` (off `renderer-edge-fix`). Adds unbiased path
termination (Russian roulette) and a scene-configurable daughter fan-out count
to the deposit-then-gather forward photon pipeline, then measures both.

All renders below: `MirrorTest.json` geometry (Cornell box + diffuse / mirror /
rough-microfacet spheres), `$bounceThreshold` 4, 256x256, single OmniLight,
deposit cloud sized so `budget-hit=no` (complete cloud — every non-delta
deposit captured). Mean luminance is the renderer's HDR pre-tonemap figure
(`mean-luminance` in the `--render-test` output). Noise is the coefficient of
variation (std/mean) of sRGB-linearized Rec.709 luma in a flat floor patch
(`scripts/noise_metric.py`, region 0.30,0.62-0.70,0.80).

---

## Milestone 1 — Russian roulette (unbiased path termination)

### Design

Applied in `Worker::processPhotons` at the **continuation decision** — the point
where a bounceable hit (below the bounce threshold) decides whether to spawn a
continuation Emitter:

1. The current hit's cloud deposit is recorded first, with the **un-reweighted**
   color. RR governs only whether the random walk *continues* from this hit, so
   the deposit (this bounce's contribution) is never biased by RR.
2. RR is skipped while `bounces < minBounces` (default 2) — early / first-bounce
   high-energy paths always survive (standard variance control).
3. Survival probability `p = clamp(maxChannel(color) / referenceEnergy, pMin, 1)`.
4. Roll a uniform. On **death** the path stops (no Emitter spawned). On
   **survival** the continuation Emitter's color is scaled by `1/p`, so the
   expected continued energy is unchanged — the estimator is unbiased. Dim paths
   are dropped; survivors are boosted to compensate.
5. Composes with the fan-out: each daughter is its own photon → own raycast →
   own RR roll at its next bounce.

Scene config (all in `$renderConfiguration`, default off / back-compat):

| Key | Default | Meaning |
|---|---|---|
| `$russianRoulette` | `false` | master toggle |
| `$russianRouletteMinBounces` | `1` | RR only at `bounces >= this` |
| `$russianRouletteMinProbability` | `0.05` | survival floor `pMin` |
| `$russianRouletteReferenceEnergy` | `1.0` | throughput mapped to p=1 |

Note on `referenceEnergy`: photon `color` carries **physical flux** (the light's
total luminous flux Phi, not yet divided by photon count), so raw magnitudes at a
depth-2 bounce are in the thousands, not ~1. With the default `referenceEnergy=1`
the clamp pins `p=1` and RR never fires (no bias, no saving). To make RR engage
you set `referenceEnergy` near the throughput scale at the RR depth. The runs
below use `referenceEnergy=5000`, `minBounces=2`, `pMin=0.05`.

### Unbiasedness evidence (matching mean luminance)

5 independent renders each (fresh RNG seed per run), 2K photons/light, N=native
fan-out, bounceThreshold 4:

| | mean luminance (5 runs) | mean | run-to-run sd |
|---|---|---|---|
| RR **off** | 0.5049 0.5106 0.4807 0.5016 0.4864 | **0.4968** | 0.0114 |
| RR **on** (ref 5000) | 0.4922 0.4841 0.4995 0.5110 0.4898 | **0.4953** | 0.0093 |

Difference of means = **0.0015**, combined standard error ≈ 0.0066 — i.e. 0.23
sigma. The two means are statistically indistinguishable: **RR is unbiased**
(same expected image, fewer rays).

### Compute saving and its depth-dependence

Same 5-run batch (2K photons, bounceThreshold 4):

| | mean deposits | photon-pass |
|---|---|---|
| RR off | 4,234,494 | 2.40 s |
| RR on (ref 5000) | 938,868 | 0.98 s |
| **ratio** | **4.5x fewer** | **2.45x faster** |

More aggressive `referenceEnergy` prunes more (single 2K run):

| referenceEnergy | deposits | photon-pass | mean-lum |
|---|---|---|---|
| off | 4,317,069 | 2.54 s | 0.5206 |
| 5000 | 938,329 | 1.08 s | 0.5117 |
| 20000 | 306,626 | 0.47 s | 0.5008 |
| 100000 | 140,384 | 0.46 s | 0.4987 |

**Depth dependence (honest):** RR's saving comes from killing *deep, dim*
continuation paths. At this scene's bounceThreshold 4 there is real depth to
prune, so the saving is large (2.5–5x). At a **shallow** cap (bounceThreshold 1–2)
paths already terminate at the cap, so RR has little to cut and the saving is
small. The reweight keeps the result unbiased either way — RR trades a fixed bit
of extra variance for fewer rays, and that trade only pays off when there are
many low-energy deep paths to skip.

---

## Milestone 2 — Configurable daughter count

`Worker::resolveDaughterCount` applies scene config on top of each material's
native `daughterPhotonCount()`:

| Key | Default | Meaning |
|---|---|---|
| `$daughterCount` | `0` (off) | force EXACTLY this many daughters on every bounceable hit |
| `$daughterCountScale` | `1.0` | multiply native count (rounded, min 1) when no override |

Override wins over scale. The resolved N is carried in the Emitter, so the `1/N`
energy split tracks the count actually used — total outgoing energy stays correct,
only sampling noise changes. Default (override 0, scale 1) is an exact no-op.

---

## Milestone 3 — Daughter-count experiment (9 / 3 / 1)

MirrorTest geometry, bounceThreshold 4, **8,000 photons/light fixed**, complete
cloud (cap 24M, `budget-hit=no` for all three), same exposure.

| daughters | cloud deposits | cloud footprint | photon-pass | gather | mean luminance | floor noise (cov) | PNG (tracked) |
|---|---|---|---|---|---|---|---|
| **9** | 22,897,678 | 3113 MiB (cap) | 9.35 s | 0.130 s | 0.4924 | 2.16 | `reference-renders/rr-daughter-9.png` |
| **3** | 429,517 | 3113 MiB (cap) | 0.55 s | 0.035 s | 0.5114 | 2.60 | `reference-renders/rr-daughter-3.png` |
| **1** | 29,283 | 3113 MiB (cap) | 0.52 s | 0.028 s | 0.6790 | 5.61 | `reference-renders/rr-daughter-1.png` |

(Working-copy originals also at `renders/daughter{9,3,1}.png`; `renders/` is
gitignored, so the canonical copies live in `reference-renders/`. The RR
unbiasedness pair is `reference-renders/rr-off.png` and `rr-on.png`.)

(Footprint is the preallocated cap, identical across runs; **actual** populated
records are the deposit column. Deposit→memory at 128 B/record: 9-daughter
populates ~2.8 GiB of the cloud, 1-daughter ~3.6 MiB.)

### What the numbers show

- **1 daughter is dramatically smaller and faster than 9:** **782x fewer**
  deposits (29K vs 22.9M) and **18x faster** photon pass (0.52 s vs 9.35 s). The
  cloud the gather must search shrinks from 22.9M points to 29K.
- **Noise rises as daughters fall**, exactly as expected — the diffuse-indirect
  flat-region coefficient of variation goes 2.16 → 2.60 → 5.61 for 9 → 3 → 1.
  Fewer directional samples per bounce ⇒ sparser deposit cloud ⇒ noisier gather.
- **Mean luminance is roughly constant for N≥2 but BREAKS at N=1.** 9→3 holds
  (0.49 vs 0.51; the `1/N` split keeps energy correct, only noise changes). But
  N=1 jumps to 0.68.

### Honest caveat: N=1 is biased, not just noisy

The fan-out's daughter at **global index 0** is the deterministic BRDF **mode**
(`sampleMode()` — the cosine peak along the normal for Lambertian), and indices
1..N-1 are random `sample()` draws. With N≥2 the random samples average toward
the true hemisphere integral. With **N=1 there is only the mode sample** — a
deterministic peak-direction estimate with no Monte Carlo averaging — so it does
not integrate the BRDF and systematically concentrates continuation energy along
surface normals (visible as a hot spot on the floor in `daughter1.png`).

This is **bias, not noise**, confirmed by holding the count and raising photons:

| config | photons | mean luminance |
|---|---|---|
| N=1 | 8K | 0.6790 |
| N=1 | 2M (250x) | 0.6827 |
| N=3 | 8K | 0.5114 |
| N=3 | 100K | 0.5177 |
| N=9 | 8K | 0.4924 |

N=1 stays at ~0.68 with 250x more photons — it converges to a *different* value,
not toward the N≥2 result. So the "1/N split keeps mean luminance constant across
daughter counts" claim holds for **N≥2** and breaks for **N=1** because of the
index-0-is-mode rule. Configurable daughter count is correct and useful for 9/3
(big size/speed win, modest noise cost); going to a single daughter additionally
collapses the estimator to mode-only and shifts the result.
