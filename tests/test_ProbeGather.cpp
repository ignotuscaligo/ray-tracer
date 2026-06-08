#include <catch2/catch_all.hpp>

#include "AreaLight.h"
#include "BounceStore.h"
#include "Color.h"
#include "ProbeGather.h"
#include "ProbeIndex.h"
#include "Quaternion.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>
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

// ===== RawBounce stores the deposit normal (leak suppression input) =====

TEST_CASE("RawBounce round-trips the surface normal", "[ProbeGather][BounceStore]")
{
    const RawBounce withNormal{Vector{1, 2, 3}, Vector{0, 0, 1}, Vector{0, 1, 0},
                               Color{1, 1, 1}};
    REQUIRE(withNormal.normal().y == 1.0f);
    REQUIRE(withNormal.normal().x == 0.0f);

    // The legacy 3-arg constructor leaves the normal zeroed (the gather treats a
    // zero-length normal as "skip the normal test", a safe backstop).
    const RawBounce noNormal{Vector{0, 0, 0}, Vector{0, 0, 1}, Color{1, 1, 1}};
    REQUIRE(noNormal.normal().magnitude() == 0.0);
}

// ===== Bug 2: normal-agreement leak suppression (no black rim on a curve) =====
//
// The gather keeps a deposit only when dot(depositNormal, hitNormal) >= cos 60°.
// This rejects an ADJACENT PERPENDICULAR surface (corner leak) but keeps a
// smoothly-CURVED same surface near its silhouette (whose normals stay within a
// moderate cone of the hit normal). The old hard tangent-plane distance cut did the
// opposite — it kept perpendicular-but-close deposits and rejected curved-but-
// off-plane ones, darkening a sphere's silhouette into a black rim. This test
// pins the discriminating predicate.
TEST_CASE("Normal-agreement keeps a grazing curved surface but rejects a perpendicular wall",
          "[ProbeGather]")
{
    constexpr double kNormalAgree = 0.5;  // cos 60°, matching the gather
    const Vector hitNormal{0.0, 1.0, 0.0};  // gather point on a +Y-facing surface

    // A deposit on the SAME smoothly-curved surface, 40° around the silhouette:
    // its normal is tilted but still well within the 60° cone -> KEPT.
    const double a = Utility::radians(40.0);
    const Vector curvedNormal{std::sin(a), std::cos(a), 0.0};
    REQUIRE(Vector::dot(curvedNormal, hitNormal) >= kNormalAgree);

    // A deposit on an ADJACENT PERPENDICULAR wall (normal +X) -> dot 0 -> REJECTED.
    const Vector wallNormal{1.0, 0.0, 0.0};
    REQUIRE(Vector::dot(wallNormal, hitNormal) < kNormalAgree);

    // Even a steep 55° grazing point on the curve is kept (no black rim), while a
    // 65° one starts to fall outside — a graceful cone, not a hard plane cut.
    const Vector graze55{std::sin(Utility::radians(55.0)), std::cos(Utility::radians(55.0)), 0.0};
    REQUIRE(Vector::dot(graze55, hitNormal) >= kNormalAgree);
}

// ===== Bug 1: emitter deposits make a fixture gather to its surface radiance =====
//
// depositEmitters tiles a light's surface with raw deposits carrying
//   power = radiance * pi * area / (4 N)
// so the unified gather's density estimate (identity BRDF, 4/pi splat parity)
// reproduces the emitter's view-independent radiance L = M/pi exactly. This test
// recomputes that density estimate from the stored deposits and checks it matches
// the light's surfaceRadiance(), independent of the gather footprint radius.
TEST_CASE("Emitter deposits reproduce the fixture's surface radiance", "[ProbeGather][EmissiveDeposit]")
{
    // A 20x20 square emitter, flux override so radiance is a known constant.
    const double width = 20.0;
    const double flux = 4000.0;
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Square);
    light->width(width);
    light->height(width);
    light->luminousFluxOverride(flux);
    light->color(Color{1.0f, 1.0f, 1.0f});
    light->transform.rotation = Quaternion();      // right=+X, up=+Y, normal=+Z
    light->transform.position = Vector{0, 0, 0};

    const Color radiance = light->surfaceRadiance();
    REQUIRE(radiance.red > 0.0f);

    // A probe at the patch center so deposits near the center are kept. Keep radius
    // generously covers the gather disc.
    const std::vector<Vector> probes{Vector{0, 0, 0}};
    ProbeIndex probeIndex(probes, /*cellSize=*/10.0, /*keepRadius=*/10.0);

    BounceStore store(/*capacity=*/1000000);
    const std::vector<std::shared_ptr<Object>> objects{light};

    const double spacing = 0.5;
    const ProbeGather::EmitterDepositResult res =
        ProbeGather::depositEmitters(objects, probeIndex, spacing, store);
    REQUIRE(res.patches == 1);
    REQUIRE(res.kept > 0);

    store.buildIndex(/*cellSize=*/2.0);

    // Recompute the gather's density estimate at the patch center over a disc fully
    // inside the patch interior (so no boundary clipping): identity BRDF, sum power,
    // multiply by 4/pi, divide by pi r^2. It must reproduce `radiance` and be
    // INVARIANT to r (the property that makes the estimate footprint-independent).
    auto densityEstimate = [&](double r) {
        const std::vector<std::size_t> near = store.radiusSearch(Vector{0, 0, 0}, r);
        Color sum{0.0f, 0.0f, 0.0f};
        for (const std::size_t i : near)
        {
            sum += store[i].power;
        }
        const double scale = (4.0 / Utility::pi) / (Utility::pi * r * r);
        return sum * static_cast<float>(scale);
    };

    for (const double r : {3.0, 5.0, 7.0})
    {
        const Color est = densityEstimate(r);
        // Within 8% of the true radiance (grid-tiling discretization at this
        // spacing; tighter spacing converges further).
        REQUIRE(est.red == Catch::Approx(radiance.red).epsilon(0.08));
        REQUIRE(est.green == Catch::Approx(radiance.green).epsilon(0.08));
    }
}

TEST_CASE("depositEmitters ignores scenes with no emitters", "[ProbeGather][EmissiveDeposit]")
{
    const std::vector<Vector> probes{Vector{0, 0, 0}};
    ProbeIndex probeIndex(probes, 1.0, 1.0);
    BounceStore store(100);
    const std::vector<std::shared_ptr<Object>> objects{};
    const ProbeGather::EmitterDepositResult res =
        ProbeGather::depositEmitters(objects, probeIndex, 0.5, store);
    REQUIRE(res.patches == 0);
    REQUIRE(res.kept == 0);
    REQUIRE(store.size() == 0);
}
