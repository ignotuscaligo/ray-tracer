#include <catch2/catch_all.hpp>

#include "RenderSettings.h"

// ============================================================================
// T10 ProbeDefaults — the documented RenderSettings defaults are what ship
// ============================================================================
//
// DESIGN §0/§6f: the probe-guided unified gather is the DEFAULT path
// ($probeGather defaults true); the legacy splat + density grid is reachable only
// behind $probeGather false. A silent flip of the default would route every render
// through the deprecated path with a green suite. These pin the load-bearing defaults
// a fresh RenderSettings{} must carry (the values DESIGN and the scene docs assume).

TEST_CASE("T10 ProbeDefaults: probe-guided gather is the default", "[ProbeDefaults][T10]")
{
    const RenderSettings s{};
    REQUIRE(s.useProbeGather == true);
}

TEST_CASE("T10 ProbeDefaults: documented termination / bounce defaults", "[ProbeDefaults][T10]")
{
    const RenderSettings s{};
    // DESIGN §2: deterministic decay + bounce cap. Default bounce cap 1, absolute
    // termination floor 1.0 (the units-trap default — see DESIGN §2a).
    REQUIRE(s.bounceThreshold == 1);
    REQUIRE(s.terminationThreshold == Catch::Approx(1.0));
}

TEST_CASE("T10 ProbeDefaults: deterministic mode and seed are OFF by default", "[ProbeDefaults][T10]")
{
    // A production render must NOT silently become single-threaded/seeded: the
    // deterministic test mode is opt-in, and an absent $seed leaves the RNG seeded
    // from random_device (the kUnseeded sentinel).
    const RenderSettings s{};
    REQUIRE(s.deterministic == false);
    REQUIRE(s.seed == RenderSettings::kUnseeded);
}

TEST_CASE("T10 ProbeDefaults: probe keep-radius / sub-sample defaults", "[ProbeDefaults][T10]")
{
    const RenderSettings s{};
    // Keep radius scale >= 1 (a kept bounce must be within at least one footprint of a
    // probe) and a per-pixel probe stride of 1 by default (densest coverage).
    REQUIRE(s.probeKeepRadiusScale >= 1.0);
    REQUIRE(s.probeSubSample == 1);
}
