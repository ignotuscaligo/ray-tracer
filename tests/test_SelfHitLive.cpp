#include <catch2/catch_all.hpp>

#include "BounceStore.h"
#include "FurnaceFixture.h"
#include "RenderFixture.h"
#include "Worker.h"

#include <cmath>

// ============================================================================
// T11 SelfHitLive — the self-hit threshold pinned against PRODUCTION + a live render
// ============================================================================
//
// DESIGN §2b [INVARIANT]: a spawned continuation ray must not re-intersect the surface
// it was spawned on; the only world-space backstop is the worker's ABSOLUTE 1e-4
// selfHitThreshold (NOT DBL_EPSILON). test_SelfHitEpsilon previously re-declared the
// 1e-4 literal, so reverting the worker to machine epsilon still passed (the
// self-consistent-but-wrong trap). This test reads the PRODUCTION value and then does
// a LIVE render check: if the threshold were too small, grazing continuation rays in a
// sealed box would self-re-intersect and produce EXTRA deposits, inflating the
// deterministic per-photon deposit count above the bounce-cap ledger of exactly B+1.

TEST_CASE("T11 SelfHitLive: the production self-hit threshold is the absolute 1e-4 floor",
          "[SelfHitLive][T11]")
{
    // Pin the production constant directly (no duplicated literal). A reversion toward
    // machine epsilon changes this and fails here.
    const double t = WorkerDebug::selfHitThreshold();
    INFO("selfHitThreshold = " << t);
    REQUIRE(t == Catch::Approx(1e-4).epsilon(1e-9));
    // It must be far above float self-intersection noise (DBL_EPSILON ~ 2.2e-16) and
    // far below any real Cornell-scale feature size.
    REQUIRE(t > 1e3 * std::numeric_limits<double>::epsilon());
    REQUIRE(t < 1.0);
}

TEST_CASE("T11 SelfHitLive: no self-re-hit double-deposits in a sealed box",
          "[SelfHitLive][T11]")
{
    // In a sealed diffuse box every photon deposits at bounce depths 0..B (B+1 times)
    // and nowhere else — UNLESS a grazing continuation re-intersects its own surface
    // at a sub-threshold distance, which would deposit AGAIN (shadow acne / energy
    // error). The deterministic furnace ledger is therefore exactly N*(B+1) deposits;
    // an admitted self-re-hit would push the count above that. (This is the live
    // counterpart to test_SelfHitEpsilon's analytic band check.)
    rt_test::FurnaceParams p;
    p.albedo = 0.6;
    p.bounceThreshold = 3;
    p.photonsPerLight = 60000;  // 60k * (B+1=4) = 240k deposits < capacity
    p.seed = 4321;

    WorkerDebug::resetBounceCounters();
    rt_test::RenderScene furnace{rt_test::buildFurnaceScene(p)};
    const BounceStore& store = *furnace.result.bounceStore;

    INFO("stored=" << store.size() << " culled=" << WorkerDebug::bounceCulled()
         << " dropped=" << store.droppedCount());
    REQUIRE(WorkerDebug::bounceCulled() == 0);
    REQUIRE(store.droppedCount() == 0);

    // Exactly B+1 deposits per photon — no extra self-re-hit deposits.
    REQUIRE(store.size() == p.photonsPerLight * (p.bounceThreshold + 1));
}
