#include <catch2/catch_all.hpp>

#include "Ray.h"
#include "Sphere.h"
#include "Vector.h"

#include <limits>

// ===== Self-hit / shadow-acne epsilon regression =====
//
// The photon pass spawns each continuation ray EXACTLY at the bare hit position
// (Material::generateDaughters sets out.ray = {position, direction} with no
// offset), and the camera splat's occlusion ray likewise starts at the hit. The
// only backstop against a ray re-intersecting the SAME surface it was spawned on
// is Worker.cpp's selfHitThreshold, compared against hit.distance.
//
// The bug (review 2c): that threshold was DBL_EPSILON (~2.2e-16). The geometric
// primitives have NO matching positive world-space floor of their own — the
// watertight triangle test only rejects t <= 0, and the sphere test's 1e-6 floor
// is PARAMETRIC (shrinks with a long direction). So a grazing continuation off a
// curved surface self-re-intersects at a tiny-but-positive distance (~1e-4 here)
// that sails past DBL_EPSILON, producing a double deposit / double attenuation
// (shadow acne / energy error).
//
// The fix raises selfHitThreshold to an ABSOLUTE world-space 1e-4 (the same order
// as the gather path's kReflectionEpsilon ray-spawn offset), so the photon side
// and camera side are consistent. These tests pin the mechanism: a grazing
// self-re-hit lands in the (DBL_EPSILON, 1e-4) band the OLD threshold admitted and
// the NEW one rejects, while a legitimate far hit stays above 1e-4 and is kept.

namespace
{
// Mirror of Worker.cpp's selfHitThreshold. Kept in sync by intent: if the worker
// constant changes, this expectation should change with it (and a self-hit at a
// distance below it must be rejected).
constexpr double kSelfHitThreshold = 1e-4;
} // namespace

TEST_CASE("Grazing continuation off a sphere self-re-hits inside the acne band",
          "[SelfHit][ShadowAcne]")
{
    // Cornell-scale sphere (the blocker in CornellBoxArea: center y=40, r=40).
    Sphere sphere;
    sphere.center = Vector{0.0, 40.0, 40.0};
    sphere.radius = 40.0;

    // A camera/primary ray hits the +z pole of the sphere at (0,40,80).
    const Ray primary{Vector{0.0, 40.0, 800.0}, Vector{0.0, 0.0, -1.0}};
    const std::optional<Hit> first = rayIntersectsSphere(primary, sphere);
    REQUIRE(first.has_value());

    const Vector P = first->position;          // on-surface spawn point
    const Vector n = (P - sphere.center).normalize();

    // Continuation spawned EXACTLY on the surface, aimed nearly tangent with a
    // tiny INWARD bias — exactly what a grazing scattered photon does. It curves
    // back into the sphere and re-hits a hair downrange.
    const Vector tangent{1.0, 0.0, 0.0};       // tangent at the +z pole
    const double tinyInward = 1e-6;
    Vector dir = tangent - n * tinyInward;
    dir = dir / dir.magnitude();

    const Ray continuation{P, dir};
    const std::optional<Hit> selfHit = rayIntersectsSphere(continuation, sphere);
    REQUIRE(selfHit.has_value());

    // The self re-hit sits in the acne band: above DBL_EPSILON (so the OLD
    // threshold WOULD have admitted it — the bug) but below the new 1e-4 floor
    // (so the worker now REJECTS it — the fix).
    INFO("self re-hit distance = " << selfHit->distance);
    REQUIRE(selfHit->distance > std::numeric_limits<double>::epsilon());
    REQUIRE(selfHit->distance < kSelfHitThreshold);
}

TEST_CASE("A legitimate far hit survives the self-hit threshold", "[SelfHit][ShadowAcne]")
{
    // The fix must NOT reject real geometry. A continuation with a larger inward
    // component re-enters the sphere at a real, far distance well above 1e-4.
    Sphere sphere;
    sphere.center = Vector{0.0, 40.0, 40.0};
    sphere.radius = 40.0;

    const Vector P{0.0, 40.0, 80.0};           // +z pole
    const Vector n{0.0, 0.0, 1.0};

    // 1e-2 inward bias -> the ray clearly crosses into the sphere again.
    Vector dir = Vector{1.0, 0.0, 0.0} - n * 1e-2;
    dir = dir / dir.magnitude();

    const Ray continuation{P, dir};
    const std::optional<Hit> selfHit = rayIntersectsSphere(continuation, sphere);
    REQUIRE(selfHit.has_value());

    INFO("far hit distance = " << selfHit->distance);
    REQUIRE(selfHit->distance > kSelfHitThreshold);  // kept by the worker
}
