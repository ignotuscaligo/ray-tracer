#include <catch2/catch_all.hpp>

#include "FurnaceFixture.h"
#include "RenderFixture.h"
#include "StatAssert.h"
#include "Worker.h"

#include <cmath>

// ============================================================================
// T1 FurnaceLedger — truncated-furnace energy conservation (the top oracle)
// ============================================================================
//
// In a sealed diffuse box, Lambertian throughput is EXACTLY albedo per bounce with
// ZERO variance (cosine-weighted f*cos/pdf = albedo identically). A photon emitted
// at magnitude Phi/N therefore deposits (Phi/N)*rho^b at bounce depth b, and the
// hard bounce cap B stops it after b = 0..B. The total deposited power is the exact
// closed form  E = Phi * (1 - rho^(B+1)) / (1 - rho)  (FurnaceFixture oracle).
//
// This one number pins DESIGN invariants #3 (additive gather / no gather-side 1/N),
// #4 (relative absorption), #5 (single stochastic sample), #7/#9/#10 (deterministic
// decay + bounce cap + absolute floor). It breaks under ANY reintroduction of
// Russian roulette (survivor boost inflates the tail), a gather-side 1/N (would not
// touch the store sum, but the count-equivalence sibling T7 would catch it), or
// mode-sampling (per-bounce throughput would drift off exactly rho).
//
// Because the throughput is deterministic and the test runs in SINGLE-THREAD
// DETERMINISTIC mode, the ONLY thing between the measured sum and the exact oracle
// is (a) the probe keep-test culling deposits and (b) the store capacity dropping
// them. The test asserts BOTH are zero, so the comparison is exact (to float
// summation rounding over ~1M deposits), not statistical.

using Catch::Approx;

TEST_CASE("T1 FurnaceLedger: total deposited power matches the truncated geometric series",
          "[Furnace][T1][energy]")
{
    rt_test::FurnaceParams p;
    p.albedo = 0.5;
    p.bounceThreshold = 4;
    p.brightnessCd = 100.0;
    p.photonsPerLight = 80000;  // 80k * (B+1=5) = 400k deposits < 48*48*256 capacity
    p.half = 100.0;

    WorkerDebug::resetBounceCounters();
    rt_test::RenderScene furnace{rt_test::buildFurnaceScene(p)};

    REQUIRE(furnace.result.bounceStore != nullptr);
    const BounceStore& store = *furnace.result.bounceStore;

    // The ledger is complete only if NOTHING was culled or dropped. (A point-source
    // furnace seen by an inside camera with a generous keep radius keeps every
    // deposit; emitter deposits are absent because the source is an OmniLight.)
    INFO("bounceCulled=" << WorkerDebug::bounceCulled()
         << " dropped=" << store.droppedCount() << " stored=" << store.size());
    REQUIRE(WorkerDebug::bounceCulled() == 0);
    REQUIRE(store.droppedCount() == 0);
    REQUIRE(furnace.result.emitterDepositsKept == 0);  // point source: no emitter patches

    const double measured = rt_test::sumDepositedPower(store);
    const double oracle = rt_test::furnaceEnergyOracle(p);

    // Each photon deposits at b = 0..B, so the store holds exactly N*(B+1) deposits.
    REQUIRE(store.size() == p.photonsPerLight * (p.bounceThreshold + 1));

    INFO("measured=" << measured << " oracle=" << oracle
         << " rel=" << std::abs(measured - oracle) / oracle);
    // Exact up to float-summation rounding over ~1M single-precision adds.
    REQUIRE(measured == Approx(oracle).epsilon(1e-4));
}

TEST_CASE("T1 FurnaceLedger: the oracle tracks the bounce cap (deeper cap => more energy)",
          "[Furnace][T1][energy]")
{
    // A second, independent angle on the same invariant: raising the bounce cap adds
    // exactly the next geometric term. With rho = 0.5 the per-photon series is
    // 1 + 0.5 + 0.25 + ..., so going from cap 2 to cap 3 must add Phi*rho^3 of energy.
    rt_test::FurnaceParams base;
    base.albedo = 0.5;
    base.brightnessCd = 100.0;
    base.photonsPerLight = 100000;  // cap 3 => 4 deposits => 400k < capacity

    rt_test::FurnaceParams capLo = base;
    capLo.bounceThreshold = 2;
    capLo.seed = 7;
    rt_test::FurnaceParams capHi = base;
    capHi.bounceThreshold = 3;
    capHi.seed = 7;

    WorkerDebug::resetBounceCounters();
    rt_test::RenderScene lo{rt_test::buildFurnaceScene(capLo)};
    REQUIRE(WorkerDebug::bounceCulled() == 0);
    const double eLo = rt_test::sumDepositedPower(*lo.result.bounceStore);

    WorkerDebug::resetBounceCounters();
    rt_test::RenderScene hi{rt_test::buildFurnaceScene(capHi)};
    REQUIRE(WorkerDebug::bounceCulled() == 0);
    const double eHi = rt_test::sumDepositedPower(*hi.result.bounceStore);

    const double phi = rt_test::furnaceFlux(base);
    const double expectedDelta = phi * std::pow(base.albedo, 3.0);  // the rho^3 term

    INFO("eLo=" << eLo << " eHi=" << eHi << " delta=" << (eHi - eLo)
         << " expectedDelta=" << expectedDelta);
    REQUIRE((eHi - eLo) == Approx(expectedDelta).epsilon(2e-3));
}
