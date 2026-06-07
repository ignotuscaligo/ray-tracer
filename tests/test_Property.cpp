#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Property.h"
#include "Vector.h"

#include <cmath>

using Catch::Approx;

// A constant Property returns its value at every time (static-scene invariance).
TEST_CASE("Property constant returns its value at all times", "[property]")
{
    Property<double> p(3.5);
    REQUIRE_FALSE(p.isAnimated());
    REQUIRE(p.evaluate(-100.0) == Approx(3.5));
    REQUIRE(p.evaluate(0.0) == Approx(3.5));
    REQUIRE(p.evaluate(1000.0) == Approx(3.5));
}

TEST_CASE("Property default-constructed constant is value-initialized", "[property]")
{
    Property<double> p;
    REQUIRE_FALSE(p.isAnimated());
    REQUIRE(p.evaluate(0.0) == Approx(0.0));
}

// Adding keyframes makes the property animated and the stored constant is ignored.
TEST_CASE("Property becomes animated after a keyframe is added", "[property]")
{
    Property<double> p(99.0);
    p.addKeyframe(0.0, 0.0);
    p.addKeyframe(1.0, 10.0);
    REQUIRE(p.isAnimated());
    REQUIRE(p.keyframeCount() == 2);
    // Constant 99.0 is no longer consulted.
    REQUIRE(p.evaluate(0.0) == Approx(0.0));
    REQUIRE(p.evaluate(1.0) == Approx(10.0));
}

// Endpoints are returned EXACTLY at keyframe times (Hermite basis property).
TEST_CASE("Property returns keyframe values exactly at keyframe times", "[property]")
{
    Property<double> p;
    p.addKeyframe(0.0, 2.0);
    p.addKeyframe(1.0, 5.0);
    p.addKeyframe(2.0, -3.0);
    REQUIRE(p.evaluate(0.0) == Approx(2.0));
    REQUIRE(p.evaluate(1.0) == Approx(5.0));
    REQUIRE(p.evaluate(2.0) == Approx(-3.0));
}

// Outside the authored range the value is HELD (clamped), not extrapolated.
TEST_CASE("Property holds endpoints outside the keyframe range", "[property]")
{
    Property<double> p;
    p.addKeyframe(1.0, 4.0);
    p.addKeyframe(3.0, 8.0);
    REQUIRE(p.evaluate(0.0) == Approx(4.0));
    REQUIRE(p.evaluate(-50.0) == Approx(4.0));
    REQUIRE(p.evaluate(3.5) == Approx(8.0));
    REQUIRE(p.evaluate(100.0) == Approx(8.0));
}

// A linear ramp (constant slope) should interpolate ~linearly. With Catmull-Rom
// auto-tangents over evenly-spaced collinear points the spline is exactly the
// straight line, so the midpoint equals the linear midpoint.
TEST_CASE("Property linear keyframes interpolate to the linear midpoint", "[property]")
{
    Property<double> p;
    p.addKeyframe(0.0, 0.0);
    p.addKeyframe(1.0, 1.0);
    p.addKeyframe(2.0, 2.0);
    // Midpoint of the first segment: a collinear Catmull-Rom passes the line.
    REQUIRE(p.evaluate(0.5) == Approx(0.5));
    REQUIRE(p.evaluate(1.5) == Approx(1.5));
}

// A known eased two-keyframe curve: with auto-tangents on the two endpoints the
// tangents are the secant slope, so the Hermite reduces to the straight line.
// Verify the midpoint exactly, plus monotonicity.
TEST_CASE("Property two-keyframe eased curve has correct midpoint", "[property]")
{
    Property<double> p;
    p.addKeyframe(0.0, 0.0);
    p.addKeyframe(2.0, 8.0);
    // Two-point auto-tangent spline == straight line. Midpoint t=1 -> 4.
    REQUIRE(p.evaluate(1.0) == Approx(4.0));
    // Monotonic increasing across the segment.
    double prev = p.evaluate(0.0);
    for (double t = 0.1; t <= 2.0; t += 0.1)
    {
        const double v = p.evaluate(t);
        REQUIRE(v >= prev - 1e-9);
        prev = v;
    }
}

// VELOCITY CONTINUITY: a numerically-estimated derivative is continuous across an
// interior keyframe (no jump). This is the property that makes blur track speed.
TEST_CASE("Property has continuous velocity across an interior keyframe", "[property]")
{
    Property<double> p;
    // Eased spin-up-ish curve: accelerate then decelerate.
    p.addKeyframe(0.0, 0.0);
    p.addKeyframe(1.0, 1.0);
    p.addKeyframe(2.0, 3.0);
    p.addKeyframe(3.0, 4.0);

    const double h = 1e-4;
    auto velocity = [&](double t) {
        return (p.evaluate(t + h) - p.evaluate(t - h)) / (2.0 * h);
    };

    // Velocity just before and just after the interior keyframe at t=1 and t=2
    // should match (C1 continuity of the Hermite spline with shared tangents).
    REQUIRE(velocity(1.0 - 1e-3) == Approx(velocity(1.0 + 1e-3)).margin(1e-2));
    REQUIRE(velocity(2.0 - 1e-3) == Approx(velocity(2.0 + 1e-3)).margin(1e-2));
}

// An eased ease-in/ease-out curve (explicit zero tangents at both ends) has zero
// velocity at the endpoints and a non-zero velocity in the middle — the signature
// of an eased curve, and exactly the spin-up/slow/reverse profile the fan uses.
TEST_CASE("Property explicit zero-tangent ease has zero endpoint velocity", "[property]")
{
    Property<double> p;
    Keyframe<double> k0;
    k0.time = 0.0; k0.value = 0.0; k0.useAutoTangent = false;
    k0.inTangent = 0.0; k0.outTangent = 0.0;
    Keyframe<double> k1;
    k1.time = 1.0; k1.value = 1.0; k1.useAutoTangent = false;
    k1.inTangent = 0.0; k1.outTangent = 0.0;
    p.addKeyframe(k0);
    p.addKeyframe(k1);

    const double h = 1e-4;
    auto velocity = [&](double t) {
        return (p.evaluate(t + h) - p.evaluate(t - h)) / (2.0 * h);
    };

    // Zero slope at both ends, peak slope at the middle (classic smoothstep).
    REQUIRE(velocity(0.0 + 2e-4) == Approx(0.0).margin(1e-2));
    REQUIRE(velocity(1.0 - 2e-4) == Approx(0.0).margin(1e-2));
    REQUIRE(velocity(0.5) > 1.0);  // moving fast in the middle.
    // Midpoint of smoothstep(0->1) is exactly 0.5.
    REQUIRE(p.evaluate(0.5) == Approx(0.5));
}

// Property<Vector> works (used for animated position).
TEST_CASE("Property of Vector interpolates componentwise", "[property]")
{
    Property<Vector> p;
    p.addKeyframe(0.0, Vector{0.0, 0.0, 0.0});
    p.addKeyframe(2.0, Vector{2.0, 4.0, -8.0});
    const Vector mid = p.evaluate(1.0);
    REQUIRE(mid.x == Approx(1.0));
    REQUIRE(mid.y == Approx(2.0));
    REQUIRE(mid.z == Approx(-4.0));
    // Endpoint exactness.
    const Vector end = p.evaluate(2.0);
    REQUIRE(end.x == Approx(2.0));
    REQUIRE(end.y == Approx(4.0));
    REQUIRE(end.z == Approx(-8.0));
}
