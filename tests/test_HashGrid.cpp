#include <catch2/catch_test_macros.hpp>

#include "BounceCloud.h"
#include "HashGrid.h"
#include "Vector.h"

#include <algorithm>
#include <set>
#include <thread>
#include <vector>

namespace
{

BounceRecord makeRecord(double x, double y, double z)
{
    BounceRecord r;
    r.position = Vector{x, y, z};
    r.incoming = Vector{0.0, 0.0, -1.0};
    r.power = Color{1.0f};
    r.normal = Vector{0.0, 1.0, 0.0};
    r.material = 0;
    r.time = 0.0f;
    return r;
}

// Brute-force reference: indices of cloud points within r of p, by exact distance.
std::set<std::size_t> bruteForce(const BounceCloud& cloud, const Vector& p, double r)
{
    std::set<std::size_t> out;
    const double r2 = r * r;
    for (std::size_t i = 0; i < cloud.size(); ++i)
    {
        const Vector& q = cloud[i].position;
        const double dx = q.x - p.x;
        const double dy = q.y - p.y;
        const double dz = q.z - p.z;
        if (dx * dx + dy * dy + dz * dz <= r2)
        {
            out.insert(i);
        }
    }
    return out;
}

std::set<std::size_t> asSet(const std::vector<std::size_t>& v)
{
    return std::set<std::size_t>(v.begin(), v.end());
}

}  // namespace

TEST_CASE("HashGrid radius query returns exactly the points within r", "[HashGrid]")
{
    // Known layout: a cluster near the origin plus deliberate points just inside
    // and just outside a unit-radius query, in three known cells.
    BounceCloud cloud(64);

    // index 0: exactly at the query center
    REQUIRE(cloud.append(makeRecord(0.0, 0.0, 0.0)));
    // index 1: distance 0.5 (inside r=1)
    REQUIRE(cloud.append(makeRecord(0.5, 0.0, 0.0)));
    // index 2: distance ~0.866 (inside r=1)
    REQUIRE(cloud.append(makeRecord(0.5, 0.5, 0.5)));
    // index 3: distance exactly 1.0 (ON the boundary — inclusive, must be IN)
    REQUIRE(cloud.append(makeRecord(0.0, 1.0, 0.0)));
    // index 4: distance ~1.0001 (just outside r=1 — must be OUT)
    REQUIRE(cloud.append(makeRecord(1.0001, 0.0, 0.0)));
    // index 5: distance 5 (far outside, different cell)
    REQUIRE(cloud.append(makeRecord(5.0, 0.0, 0.0)));
    // index 6: distance ~8.66 (far, another cell)
    REQUIRE(cloud.append(makeRecord(-5.0, -5.0, -5.0)));

    HashGrid grid(cloud, /*cellSize=*/1.0);

    const Vector p{0.0, 0.0, 0.0};
    const std::vector<std::size_t> hits = grid.radiusSearch(p, 1.0);
    const std::set<std::size_t> hitSet = asSet(hits);

    // Exactly indices 0,1,2,3 are within (or on) r=1; 4,5,6 are outside.
    REQUIRE(hitSet == std::set<std::size_t>{0, 1, 2, 3});
    REQUIRE(hitSet.count(4) == 0);  // just-outside point excluded
    REQUIRE(hitSet.count(5) == 0);
    REQUIRE(hitSet.count(6) == 0);

    // No duplicate indices returned.
    REQUIRE(hits.size() == hitSet.size());
}

TEST_CASE("HashGrid query matches brute force over a randomized cloud", "[HashGrid]")
{
    // Deterministic pseudo-random points across several cells, plus several query
    // points and radii. The grid result must EXACTLY equal the brute-force set
    // for every query — this is the correctness gate.
    BounceCloud cloud(4096);

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        // map to [-10, 10)
        return (static_cast<double>(state % 200000) / 10000.0) - 10.0;
    };

    for (int i = 0; i < 2000; ++i)
    {
        REQUIRE(cloud.append(makeRecord(next(), next(), next())));
    }

    // Cell size deliberately different from the query radii to exercise the
    // multi-cell reach (reach = ceil(r / cellSize)).
    HashGrid grid(cloud, /*cellSize=*/0.75);

    const std::vector<Vector> queries = {
        Vector{0.0, 0.0, 0.0},
        Vector{3.3, -2.1, 5.5},
        Vector{-7.0, 7.0, 0.0},
        Vector{9.9, 9.9, 9.9},
    };
    const std::vector<double> radii = {0.5, 1.0, 2.5, 4.0};

    for (const Vector& q : queries)
    {
        for (double r : radii)
        {
            const std::set<std::size_t> expected = bruteForce(cloud, q, r);
            const std::set<std::size_t> actual = asSet(grid.radiusSearch(q, r));
            REQUIRE(actual == expected);
        }
    }
}

TEST_CASE("BounceCloud lock-free append is race-free and respects the budget", "[BounceCloud]")
{
    // Concurrent appends from many threads. Total attempts exceed capacity, so
    // some appends must be rejected; the stored count must equal capacity, the
    // dropped count must account for the rest, and no record index may be written
    // twice (verified by tagging each record's time with a unique value and
    // confirming all stored times are distinct).
    constexpr std::size_t capacity = 10000;
    constexpr int threadCount = 8;
    constexpr int perThread = 2000;  // 16000 attempts > 10000 capacity

    BounceCloud cloud(capacity);

    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; ++t)
    {
        threads.emplace_back([&cloud, t]() {
            for (int i = 0; i < perThread; ++i)
            {
                BounceRecord r = makeRecord(0.0, 0.0, 0.0);
                // Unique tag per (thread, i): encode into time.
                r.time = static_cast<float>(t * perThread + i);
                cloud.append(r);
            }
        });
    }
    for (auto& th : threads)
    {
        th.join();
    }

    REQUIRE(cloud.size() == capacity);
    REQUIRE(cloud.budgetHit());
    REQUIRE(cloud.droppedCount() == (threadCount * perThread) - capacity);

    // Every stored slot was written exactly once (no torn / overlapping writes):
    // the tags are all unique across the populated prefix.
    std::set<float> tags;
    for (std::size_t i = 0; i < cloud.size(); ++i)
    {
        tags.insert(cloud[i].time);
    }
    REQUIRE(tags.size() == cloud.size());
}
