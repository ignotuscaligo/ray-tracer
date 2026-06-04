#pragma once

#include <cstddef>

// Tunables for a render pass. These mirror the fields that main.cpp historically
// read out of a scene JSON's $workerConfiguration / $renderConfiguration blocks.
// Defaults match the historical ProjectConfiguration defaults so that a render
// driven purely from defaults behaves identically to the pre-refactor executable.
struct RenderSettings
{
    static constexpr size_t kMillion = 1000000;
    static constexpr size_t kThousand = 1000;

    size_t photonQueueSize = 20 * kMillion;
    size_t hitQueueSize = 5 * kMillion;
    size_t emittingQueueSize = 20 * kMillion;
    size_t finalQueueSize = 100 * kThousand;
    size_t photonsPerLight = 20 * kMillion;
    size_t workerCount = 32;
    size_t fetchSize = 100000;
    size_t imageWidth = 1080;
    size_t imageHeight = 1080;
    size_t startFrame = 0;
    size_t endFrame = 0;
    size_t bounceThreshold = 1;

    // ===== Russian roulette (unbiased path termination) =====
    // When enabled, a photon that hits a bounceable surface at bounce depth >=
    // russianRouletteMinBounces is probabilistically terminated with survival
    // probability p = clamp(maxChannel(throughput) / referenceEnergy, pMin, 1).
    // Survivors have their carried color scaled by 1/p so the Monte-Carlo
    // estimator stays UNBIASED (the expected energy is unchanged — dim paths are
    // dropped, bright ones are boosted to compensate). Disabled by default for
    // exact back-compat with the pre-RR pipeline.
    //
    // - minBounces: RR is skipped for bounces < this, so early (direct / first-
    //   bounce) paths always survive. Standard practice: terminating high-energy
    //   early paths spikes variance.
    // - minProbability: the survival-probability floor (pMin). Keeps p away from
    //   0 so the 1/p reweight can't explode.
    // - referenceEnergy: the throughput value mapped to p=1 (full survival). A
    //   photon at or above this energy always survives; below it survives with
    //   probability proportional to its energy.
    bool russianRoulette = false;
    size_t russianRouletteMinBounces = 1;
    float russianRouletteMinProbability = 0.05f;
    float russianRouletteReferenceEnergy = 1.0f;

    // ===== Configurable daughter fan-out (Milestone 2) =====
    // Override or scale the per-material daughterPhotonCount() without
    // recompiling. Default behavior (override = 0, scale = 1) leaves each
    // material's native count untouched.
    //   - daughterCountOverride > 0 forces EXACTLY this many daughters on every
    //     bounceable hit, regardless of material (so 9/3/1 sweeps come from the
    //     scene file). The 1/N energy split tracks the forced N, so total
    //     outgoing energy stays correct — only sampling noise changes.
    //   - daughterCountScale multiplies the material's native count (rounded,
    //     min 1) when no override is set.
    // Override takes precedence over scale.
    size_t daughterCountOverride = 0;
    double daughterCountScale = 1.0;

    // ===== Wave 4a: BounceCloud deposit store =====
    // Capacity (in records) of the persistent deposit cloud is budgeted as
    // photonsPerLight * bounceCloudBudgetFactor, clamped to bounceCloudMaxRecords.
    //
    // The factor is an average-deposits-per-emitted-photon estimate. It is NOT 1:
    // with multi-bounce diffuse fan-out, each emitted photon spawns many daughters
    // and each non-delta bounce of each daughter is a deposit, so the true deposit
    // count is a large multiple of photonsPerLight (e.g. MirrorTest at 2M
    // photons/light, bounceThreshold 2, deposits ~90M non-delta hits ⇒ ~45x).
    //
    // A BounceRecord is 128 bytes, so capacity directly sets the footprint
    // (capacity * 128 B). Capturing every deposit of a full-resolution render is
    // not feasible (hundreds of GiB), so the cloud is deliberately BOUNDED: the
    // max-records clamp caps the footprint and, when the photon pass produces more
    // non-delta hits than fit, the surplus appends are dropped and budgetHit()
    // reports it. This is acceptable by design for Wave 4a — the gather (4b) will
    // operate on whatever subset the budget admits. Raise the factor/cap (or lower
    // photonsPerLight) to capture a larger fraction.
    //
    // Default cap = 32M records ≈ 4 GiB. Default factor 64 captures the full
    // deposit set for small renders (≤ ~500K photons/light) and a bounded subset
    // above that.
    double bounceCloudBudgetFactor = 64.0;
    size_t bounceCloudMaxRecords = 32 * kMillion;

    // Hash-grid cell edge length (world units). Defaults to the expected gather
    // radius so a radius-r query touches a 3x3x3 cell neighborhood. Built over
    // the cloud after the photon pass drains.
    double hashGridCellSize = 1.0;

    // ===== Storage pivot: density-grid cell-size tunability =====
    // The density grid's cell edge length is auto-derived from the gather
    // footprint scale (camera-to-scene-depth * pixel half-angle). This multiplier
    // scales that auto value, exposing the memory/quality tradeoff as a scene/
    // render param ($densityCellScale): a LARGER scale = coarser cells = LESS
    // memory + blurrier reflections; a SMALLER scale = finer cells = MORE memory +
    // sharper reflections. Default 1.0 uses the footprint-scale cell unchanged.
    // Clamped to a sane floor so it can never go microscopic (which would defeat
    // the compression).
    double densityCellScale = 1.0;

    // ===== Firefly fix: minimum splat-footprint radius =====
    // The direct camera splat normalizes each photon by its pixel footprint area
    // (pi * r^2, r = hitDepth * tan(pixelHalfAngle)). For an indirect photon that
    // lands on geometry very close to the camera, r collapses and 1/(pi r^2)
    // explodes, spiking a single pixel to white (a firefly) — this speckles the
    // emissive panel and leaves stray bright dots on diffuse geometry.
    //
    // The fix floors r at r_min = splatMinRadiusScale * sceneDepthFootprint,
    // where sceneDepthFootprint is the world-space radius a pixel projects to at
    // scene-centroid depth (the same length that sizes the density grid). Tying
    // r_min to that footprint makes it scale with scene/camera geometry rather
    // than being an arbitrary constant. The floor is energy-preserving: a too-
    // concentrated splat is spread over the minimum footprint, not discarded.
    //
    // Default 0.5: a hit at scene depth is unaffected (its r already equals the
    // full footprint); only hits closer than half the scene depth — where the
    // explosion happens — are floored. Set to 0 to disable (legacy behavior).
    double splatMinRadiusScale = 0.5;

    // ===== Optional per-splat luminance clamp (extreme-firefly guard) =====
    // A GENEROUS upper bound on the luminance a SINGLE camera splat may add to a
    // pixel. The minimum-radius floor above only bounds the geometric 1/(pi r^2)
    // blowup; it cannot touch a firefly whose energy comes from a degenerate
    // light-transport path (e.g. a collinear point-light / sphere-top / camera
    // alignment that produces a 2-pixel over-bright dot with a normal footprint
    // and photon-count-invariant energy). This clamp catches those by scaling a
    // splat's contribution down so its luminance never exceeds the threshold,
    // preserving hue (all channels scaled by the same factor).
    //
    // Default 0 = DISABLED (no clamp, image bit-for-bit unchanged). When set, it
    // is meant to be set HIGH — far above any legitimate single-splat luminance —
    // so it only trims extreme outliers and its energy loss is negligible. It
    // does NOT darken or alter the normal image: a normal splat's luminance is
    // orders of magnitude below a sane threshold and passes through untouched.
    double splatLuminanceClamp = 0.0;
};
