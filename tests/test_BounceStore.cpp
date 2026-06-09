#include <catch2/catch_all.hpp>

#include "BounceStore.h"
#include "Color.h"
#include "Vector.h"

#include <thread>
#include <vector>

// ===== BounceStore overflow signaling =====
//
// Past capacity the store DROPS deposits (lock-free, on the photon-pass hot
// path). A silent drop means the gathered image is quietly missing energy, so
// the overflow must be OBSERVABLE: budgetHit() flips, droppedCount() reports the
// exact number lost, and the surviving prefix stays intact. These tests pin that
// contract at the unit level (the capacity path is trivially reachable here).

TEST_CASE("BounceStore reports no overflow when within capacity", "[BounceStore]")
{
    BounceStore store(/*capacity=*/8);
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(store.append(RawBounce{Vector{static_cast<double>(i), 0.0, 0.0},
                                       Vector{0.0, 0.0, 1.0}, Color{1.0f, 1.0f, 1.0f}}));
    }

    REQUIRE(store.size() == 5);
    REQUIRE(store.attemptedCount() == 5);
    REQUIRE_FALSE(store.budgetHit());
    REQUIRE(store.droppedCount() == 0);
}

TEST_CASE("BounceStore signals overflow and counts dropped deposits", "[BounceStore]")
{
    BounceStore store(/*capacity=*/3);

    // Fill to capacity: every append within capacity succeeds.
    for (int i = 0; i < 3; ++i)
    {
        REQUIRE(store.append(RawBounce{Vector{static_cast<double>(i), 0.0, 0.0},
                                       Vector{0.0, 0.0, 1.0}, Color{1.0f, 1.0f, 1.0f}}));
    }
    REQUIRE_FALSE(store.budgetHit());
    REQUIRE(store.droppedCount() == 0);

    // Now overflow by 4 more: each append past capacity must FAIL (return false)
    // rather than silently succeeding, and the drop must be counted.
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE_FALSE(store.append(RawBounce{Vector{10.0, 0.0, 0.0},
                                             Vector{0.0, 0.0, 1.0}, Color{1.0f, 1.0f, 1.0f}}));
    }

    REQUIRE(store.budgetHit());
    REQUIRE(store.size() == 3);              // clamped at capacity
    REQUIRE(store.attemptedCount() == 7);    // 3 stored + 4 dropped
    REQUIRE(store.droppedCount() == 4);      // exact dropped count is observable
}

TEST_CASE("BounceStore overflow count is correct under concurrent appends", "[BounceStore]")
{
    // The drop counter is the atomic write cursor, so it must stay exact even
    // when many worker threads append past capacity simultaneously (the real
    // photon-pass condition). capacity 100, 8 threads each appending 100 = 800
    // attempts, 700 dropped.
    constexpr std::size_t kCapacity = 100;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 100;

    BounceStore store(kCapacity);

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&store]() {
            for (int i = 0; i < kPerThread; ++i)
            {
                store.append(RawBounce{Vector{0.0, 0.0, 0.0}, Vector{0.0, 0.0, 1.0},
                                       Color{1.0f, 1.0f, 1.0f}});
            }
        });
    }
    for (auto& w : workers)
    {
        w.join();
    }

    const std::uint64_t expectedAttempts =
        static_cast<std::uint64_t>(kThreads) * static_cast<std::uint64_t>(kPerThread);
    REQUIRE(store.attemptedCount() == expectedAttempts);
    REQUIRE(store.size() == kCapacity);
    REQUIRE(store.budgetHit());
    REQUIRE(store.droppedCount() == expectedAttempts - kCapacity);
}
