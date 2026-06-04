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
};
