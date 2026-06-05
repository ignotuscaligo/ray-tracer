#include <catch2/catch_all.hpp>

#include "Color.h"
#include "Photon.h"

// Decay termination + bounce-cap interaction (review HIGH gap: test_Emitter covers
// the absolute decay floor in isolation, but NOT the interaction with the bounce
// cap). The live worker continues a photon's walk only while BOTH terminators allow
// it, dying at WHICHEVER fires first (src/Worker.cpp:686-688):
//
//     decayAlive = photonDecayAlive(photon, terminationThreshold);
//     if (decayAlive && bounces < bounceThreshold) { continue the walk }
//
//   1. ABSOLUTE DECAY: photonDecayAlive returns false once current magnitude
//      <= terminationThreshold (Photon.h:50-62) — a hard absolute brightness floor.
//   2. BOUNCE CAP: the walk also stops once bounces reaches bounceThreshold.
//
// These tests assert each condition independently and the "whichever first"
// composition that the worker applies.

namespace
{

// The worker's continuation predicate (mirrors Worker.cpp:688). Returns true iff the
// photon should push another bounce.
bool continuesWalk(const Photon& photon, double terminationThreshold, size_t bounceThreshold)
{
    const bool decayAlive = photonDecayAlive(photon, terminationThreshold);
    return decayAlive && photon.bounces < static_cast<int>(bounceThreshold);
}

Photon photonWith(float magnitude, int bounces)
{
    Photon p;
    p.color = Color{magnitude, magnitude, magnitude};
    p.bounces = bounces;
    return p;
}

}  // namespace

TEST_CASE("Decay alone: photon dies when magnitude falls below the absolute floor", "[Decay]")
{
    const double threshold = 1.0;
    // Above the floor -> alive; at-or-below -> dead (strictly-greater predicate).
    REQUIRE(photonDecayAlive(/*current=*/1.001f, threshold));
    REQUIRE_FALSE(photonDecayAlive(/*current=*/1.0f, threshold));
    REQUIRE_FALSE(photonDecayAlive(/*current=*/0.999f, threshold));

    // Absolute, not relative-to-emission: a brighter photon survives deeper.
    REQUIRE(photonDecayAlive(/*current=*/100.0f, threshold));
    REQUIRE_FALSE(photonDecayAlive(/*current=*/0.5f, threshold));
}

TEST_CASE("Bounce cap alone: photon dies once bounces reaches the cap", "[Decay]")
{
    const double threshold = 0.0;  // decay disabled (any positive magnitude is alive)
    const size_t cap = 3;

    // A bright photon (decay never fires) is bounded purely by the cap.
    REQUIRE(continuesWalk(photonWith(50.0f, /*bounces=*/0), threshold, cap));
    REQUIRE(continuesWalk(photonWith(50.0f, /*bounces=*/2), threshold, cap));
    // At the cap (bounces == cap) the walk stops.
    REQUIRE_FALSE(continuesWalk(photonWith(50.0f, /*bounces=*/3), threshold, cap));
    REQUIRE_FALSE(continuesWalk(photonWith(50.0f, /*bounces=*/4), threshold, cap));
}

TEST_CASE("Whichever first: a bright photon dies at the cap, a dim one dies on decay",
          "[Decay]")
{
    const double threshold = 1.0;
    const size_t cap = 5;

    // A BRIGHT photon stays above the decay floor, so the BOUNCE CAP terminates it
    // first: alive through bounce cap-1, dead at the cap even though magnitude is high.
    REQUIRE(continuesWalk(photonWith(100.0f, /*bounces=*/4), threshold, cap));
    REQUIRE_FALSE(continuesWalk(photonWith(100.0f, /*bounces=*/5), threshold, cap));

    // A DIM photon is killed by DECAY before it ever reaches the cap: magnitude below
    // the floor terminates it at a low bounce count regardless of remaining cap budget.
    REQUIRE_FALSE(continuesWalk(photonWith(0.5f, /*bounces=*/0), threshold, cap));
    REQUIRE_FALSE(continuesWalk(photonWith(0.5f, /*bounces=*/1), threshold, cap));

    // Both conditions must hold to continue: bright AND under the cap.
    REQUIRE(continuesWalk(photonWith(2.0f, /*bounces=*/1), threshold, cap));
}

TEST_CASE("Decay predicate uses the max colour channel as current magnitude", "[Decay]")
{
    const double threshold = 1.0;
    // Max channel above the floor -> alive.
    Photon bright;
    bright.color = Color{0.1f, 1.5f, 0.2f};  // max 1.5 > 1.0
    REQUIRE(photonDecayAlive(bright, threshold));
    // All channels below the floor -> dead.
    Photon dim;
    dim.color = Color{0.3f, 0.4f, 0.2f};  // max 0.4 < 1.0
    REQUIRE_FALSE(photonDecayAlive(dim, threshold));
}
