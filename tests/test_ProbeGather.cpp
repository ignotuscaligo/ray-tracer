#include <catch2/catch_test_macros.hpp>

#include "BounceStore.h"
#include "Color.h"
#include "ProbeIndex.h"
#include "Vector.h"

#include <thread>
#include <vector>

// ===== ProbeIndex: the keep-test that bounds raw-bounce storage =====

TEST_CASE("ProbeIndex keeps bounces near a probe and culls those far away", "[ProbeGather][ProbeIndex]")
{
    // One probe at the origin; keep radius 5.
    const std::vector<Vector> probes{Vector{0.0, 0.0, 0.0}};
    ProbeIndex index(probes, /*cellSize=*/5.0, /*keepRadius=*/5.0);

    REQUIRE(index.probeCount() == 1);

    // A point well inside the keep radius is kept.
    REQUIRE(index.anyWithinKeepRadius(Vector{1.0, 2.0, 0.0}));
    // A point just inside (distance 4.9) is kept.
    REQUIRE(index.anyWithinKeepRadius(Vector{4.9, 0.0, 0.0}));
    // A point well outside the keep radius is culled.
    REQUIRE_FALSE(index.anyWithinKeepRadius(Vector{100.0, 0.0, 0.0}));
    // A point just outside (distance 5.1) is culled.
    REQUIRE_FALSE(index.anyWithinKeepRadius(Vector{5.1, 0.0, 0.0}));
}

TEST_CASE("ProbeIndex anyWithin honors the explicit radius across cell boundaries", "[ProbeGather][ProbeIndex]")
{
    // Probe far from the origin so the query must reach across several cells.
    const std::vector<Vector> probes{Vector{37.0, -12.0, 8.0}};
    ProbeIndex index(probes, /*cellSize=*/2.0, /*keepRadius=*/2.0);

    // A query point 3 units away is found only with r >= 3.
    const Vector q{40.0, -12.0, 8.0};  // distance 3 along +x
    REQUIRE_FALSE(index.anyWithin(q, 2.5));
    REQUIRE(index.anyWithin(q, 3.5));
}

TEST_CASE("ProbeIndex with no probes culls everything", "[ProbeGather][ProbeIndex]")
{
    ProbeIndex index(std::vector<Vector>{}, /*cellSize=*/1.0, /*keepRadius=*/10.0);
    REQUIRE(index.probeCount() == 0);
    REQUIRE_FALSE(index.anyWithinKeepRadius(Vector{0.0, 0.0, 0.0}));
}

// ===== BounceStore: lock-free append + post-pass radius search =====

TEST_CASE("BounceStore appends raw bounces and respects the capacity budget", "[ProbeGather][BounceStore]")
{
    BounceStore store(/*capacity=*/3);

    REQUIRE(store.append(RawBounce{Vector{0, 0, 0}, Vector{0, 0, 1}, Color{1, 0, 0}}));
    REQUIRE(store.append(RawBounce{Vector{1, 0, 0}, Vector{0, 0, 1}, Color{0, 1, 0}}));
    REQUIRE(store.append(RawBounce{Vector{2, 0, 0}, Vector{0, 0, 1}, Color{0, 0, 1}}));
    // The 4th append exceeds capacity: dropped (returns false), counted as an
    // attempt, and the stored size is clamped to capacity.
    REQUIRE_FALSE(store.append(RawBounce{Vector{3, 0, 0}, Vector{0, 0, 1}, Color{1, 1, 1}}));

    REQUIRE(store.size() == 3);
    REQUIRE(store.capacity() == 3);
    REQUIRE(store.attemptedCount() == 4);
    REQUIRE(store.budgetHit());
}

TEST_CASE("BounceStore radiusSearch returns exactly the bounces within radius", "[ProbeGather][BounceStore]")
{
    BounceStore store(/*capacity=*/16);
    store.append(RawBounce{Vector{0.0, 0.0, 0.0}, Vector{0, 0, 1}, Color{1, 1, 1}});
    store.append(RawBounce{Vector{1.0, 0.0, 0.0}, Vector{0, 0, 1}, Color{1, 1, 1}});
    store.append(RawBounce{Vector{10.0, 0.0, 0.0}, Vector{0, 0, 1}, Color{1, 1, 1}});
    store.buildIndex(/*cellSize=*/2.0);

    // Radius 1.5 around the origin catches the first two, not the far one.
    const std::vector<std::size_t> near = store.radiusSearch(Vector{0.0, 0.0, 0.0}, 1.5);
    REQUIRE(near.size() == 2);

    // Radius 0.5 catches only the exact-origin bounce.
    const std::vector<std::size_t> tight = store.radiusSearch(Vector{0.0, 0.0, 0.0}, 0.5);
    REQUIRE(tight.size() == 1);

    // A query far from every bounce returns nothing.
    const std::vector<std::size_t> empty = store.radiusSearch(Vector{100.0, 0.0, 0.0}, 1.0);
    REQUIRE(empty.empty());
}

TEST_CASE("BounceStore append is safe under concurrent writers", "[ProbeGather][BounceStore]")
{
    constexpr std::size_t kPerThread = 5000;
    constexpr std::size_t kThreads = 8;
    BounceStore store(kPerThread * kThreads);

    std::vector<std::thread> pool;
    for (std::size_t t = 0; t < kThreads; ++t)
    {
        pool.emplace_back([&store]() {
            for (std::size_t i = 0; i < kPerThread; ++i)
            {
                store.append(RawBounce{Vector{static_cast<double>(i), 0.0, 0.0},
                                       Vector{0, 0, 1}, Color{1, 1, 1}});
            }
        });
    }
    for (auto& thread : pool)
    {
        thread.join();
    }

    // Every append fit within capacity, so all were stored with no loss/overlap.
    REQUIRE(store.size() == kPerThread * kThreads);
    REQUIRE(store.attemptedCount() == kPerThread * kThreads);
    REQUIRE_FALSE(store.budgetHit());
}

// ===== The keep-test bounds memory (the memory-bound proof) =====

TEST_CASE("Probe keep-test culls bounces far from all probes (memory bound)", "[ProbeGather][ProbeIndex]")
{
    // A single visible probe. Simulate the photon pass depositing bounces both
    // near the probe (camera-visible surface) and scattered across far-away,
    // never-visible regions. Only the near ones must survive the keep-test, so the
    // store grows with visible-surface-area, not with the total bounce count.
    const std::vector<Vector> probes{Vector{0.0, 0.0, 0.0}};
    ProbeIndex index(probes, /*cellSize=*/3.0, /*keepRadius=*/3.0);

    BounceStore store(/*capacity=*/100000);

    std::size_t totalBounces = 0;
    std::size_t kept = 0;
    // 1000 near bounces (within the keep radius), 50000 far bounces.
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const Vector p{0.0, static_cast<double>(i % 3) * 0.5, 0.0};  // all within r=3
        ++totalBounces;
        if (index.anyWithinKeepRadius(p))
        {
            store.append(RawBounce{p, Vector{0, 0, 1}, Color{1, 1, 1}});
            ++kept;
        }
    }
    for (std::size_t i = 0; i < 50000; ++i)
    {
        const Vector p{1000.0 + static_cast<double>(i), 0.0, 0.0};  // far away
        ++totalBounces;
        if (index.anyWithinKeepRadius(p))
        {
            store.append(RawBounce{p, Vector{0, 0, 1}, Color{1, 1, 1}});
            ++kept;
        }
    }

    REQUIRE(totalBounces == 51000);
    // Only the near bounces survived; the store did NOT grow to all bounces.
    REQUIRE(kept == 1000);
    REQUIRE(store.size() == 1000);
    // Strong cull: the store holds a tiny fraction of the total bounce volume.
    REQUIRE(store.size() < totalBounces / 10);
}
