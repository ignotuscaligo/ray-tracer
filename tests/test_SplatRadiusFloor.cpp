#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Utility.h"

using Catch::Matchers::WithinRel;

// Firefly fix: the direct camera splat normalizes each photon by its pixel
// footprint area (pi * r^2, r = hitDepth * tan(pixelHalfAngle)). When a photon
// lands close to the camera r collapses and 1/(pi r^2) explodes, spiking a
// single pixel to white. flooredSplatRadius / splatFootprintWeight apply a
// world-space minimum radius so that weight is bounded.

TEST_CASE("flooredSplatRadius clamps a too-small radius up to the floor",
          "[SplatRadiusFloor]")
{
    // A radius below the floor is raised to exactly the floor.
    REQUIRE(Utility::flooredSplatRadius(0.01, 0.5) == 0.5);
    // A radius at or above the floor is untouched.
    REQUIRE(Utility::flooredSplatRadius(0.5, 0.5) == 0.5);
    REQUIRE(Utility::flooredSplatRadius(2.0, 0.5) == 2.0);
}

TEST_CASE("flooredSplatRadius with a disabled floor is a pass-through",
          "[SplatRadiusFloor]")
{
    // minRadius <= 0 disables the floor: the raw radius is returned unchanged,
    // including the tiny values that would otherwise produce a firefly.
    REQUIRE(Utility::flooredSplatRadius(0.001, 0.0) == 0.001);
    REQUIRE(Utility::flooredSplatRadius(0.001, -1.0) == 0.001);
}

TEST_CASE("splatFootprintWeight is bounded once the floor engages",
          "[SplatRadiusFloor]")
{
    const double minRadius = 0.5;
    const double cap = 1.0 / (Utility::pi * minRadius * minRadius);

    // Far hit (large r): weight is small and the floor does not engage, so the
    // weight matches the unfloored value.
    const double farRaw = 2.0;
    REQUIRE_THAT(Utility::splatFootprintWeight(farRaw, minRadius),
                 WithinRel(1.0 / (Utility::pi * farRaw * farRaw), 1e-9));

    // Close hit (tiny r): WITHOUT the floor the weight is enormous (the
    // firefly); WITH the floor it is capped at 1/(pi r_min^2).
    const double closeRaw = 1e-4;
    const double unflooredWeight = Utility::splatFootprintWeight(closeRaw, 0.0);
    const double flooredWeight = Utility::splatFootprintWeight(closeRaw, minRadius);

    REQUIRE(unflooredWeight > 1e6);          // would-be firefly: blows up
    REQUIRE_THAT(flooredWeight, WithinRel(cap, 1e-9));  // bounded by the cap
    REQUIRE(flooredWeight < unflooredWeight);

    // The cap is exactly the weight at the floor radius, and no floored splat
    // can ever exceed it however small the raw radius gets.
    REQUIRE(Utility::splatFootprintWeight(1e-12, minRadius) <= cap + 1e-12);
}

TEST_CASE("splatFootprintWeight returns zero for a degenerate radius",
          "[SplatRadiusFloor]")
{
    // No floor and a zero/negative raw radius: no usable footprint -> 0 weight
    // (the splat caller skips it rather than dividing by zero).
    REQUIRE(Utility::splatFootprintWeight(0.0, 0.0) == 0.0);
    REQUIRE(Utility::splatFootprintWeight(-1.0, 0.0) == 0.0);
}
