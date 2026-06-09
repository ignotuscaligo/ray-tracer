#include <catch2/catch_all.hpp>

#include "Ray.h"
#include "Triangle.h"
#include "Vector.h"

#include <vector>

// These tests pin the watertightness guarantee of rayIntersectsTriangle: for a
// quad split into two triangles along a shared diagonal, a ray hitting exactly
// on that diagonal must be claimed by EXACTLY ONE triangle — never by neither
// (the crack that produced the thin black line on the Cornell back wall) and,
// for the deterministic tie-break, never by both.
//
// Geometry mirrors how the OBJ loader (tinyobj) triangulates a quad
// `f v0 v1 v2 v3`: triangles (v0,v1,v2) and (v0,v2,v3), sharing the diagonal
// v0--v2. The quad lies in the z=0 plane; corners are wound so the geometric
// normal faces +z (toward the camera at +z), matching the front-face cull.

namespace
{

// A unit-ish quad in z=0, corners chosen so (v0,v1,v2),(v0,v2,v3) wind CCW seen
// from +z (front-facing for a ray travelling in -z).
const Vector kV0{-1.0, -1.0, 0.0};
const Vector kV1{1.0, -1.0, 0.0};
const Vector kV2{1.0, 1.0, 0.0};
const Vector kV3{-1.0, 1.0, 0.0};

Triangle makeTri(const Vector& a, const Vector& b, const Vector& c)
{
    Triangle tri{a, b, c};
    // Flat shading normals so getNormal() is well-defined.
    tri.aNormal = tri.normal;
    tri.bNormal = tri.normal;
    tri.cNormal = tri.normal;
    return tri;
}

// Count how many of the two adjacent triangles a -z ray through (x, y) hits.
int hitCount(double x, double y, const Triangle& t1, const Triangle& t2)
{
    const Ray ray{{x, y, 10.0}, {0.0, 0.0, -1.0}};
    int count = 0;
    if (rayIntersectsTriangle(ray, t1)) { ++count; }
    if (rayIntersectsTriangle(ray, t2)) { ++count; }
    return count;
}

} // namespace

TEST_CASE("Ray through a shared diagonal edge hits exactly one triangle", "[Triangle][Watertight]")
{
    const Triangle t1 = makeTri(kV0, kV1, kV2);
    const Triangle t2 = makeTri(kV0, kV2, kV3);

    // Sweep points along the INTERIOR of the shared diagonal v0=(-1,-1) ->
    // v2=(1,1), i.e. y == x. (The endpoints i=0/i=200 are the quad's outer
    // corner vertices, not interior to the diagonal; they sit on the quad
    // boundary and are covered separately below.) Each interior point lands
    // exactly on the edge, where the top-left fill rule must award the hit to
    // exactly one of the two triangles — never both (non-deterministic) and
    // never neither (the crack that produced the black line).
    for (int i = 1; i < 200; ++i)
    {
        const double s = static_cast<double>(i) / 200.0; // 0..1 along the diagonal
        const double x = -1.0 + 2.0 * s;
        const double y = x; // exactly on the diagonal

        const int count = hitCount(x, y, t1, t2);

        INFO("diagonal sample i=" << i << " x=" << x << " y=" << y << " hits=" << count);
        REQUIRE(count == 1);
    }
}

TEST_CASE("Interior and exterior points are classified consistently", "[Triangle][Watertight]")
{
    const Triangle t1 = makeTri(kV0, kV1, kV2);
    const Triangle t2 = makeTri(kV0, kV2, kV3);

    SECTION("a point strictly inside the quad hits exactly one triangle")
    {
        // Centroid of t1 (lower-right triangle): mean of v0,v1,v2.
        REQUIRE(hitCount(1.0 / 3.0, -1.0 / 3.0, t1, t2) == 1);
        // Centroid of t2 (upper-left triangle): mean of v0,v2,v3.
        REQUIRE(hitCount(-1.0 / 3.0, 1.0 / 3.0, t1, t2) == 1);
    }

    SECTION("a point outside the quad hits neither triangle")
    {
        REQUIRE(hitCount(5.0, 5.0, t1, t2) == 0);
        REQUIRE(hitCount(-5.0, 0.0, t1, t2) == 0);
    }
}

TEST_CASE("No crack for arbitrary ray directions through the diagonal", "[Triangle][Watertight]")
{
    const Triangle t1 = makeTri(kV0, kV1, kV2);
    const Triangle t2 = makeTri(kV0, kV2, kV3);

    // Aim rays from a fixed off-axis origin at points along the diagonal so the
    // ray direction is not axis-aligned. This exercises the axis-permutation /
    // shear path of the watertight transform, where a naive test is most likely
    // to leak. The guarantee is NO GAP (count >= 1, so no ray slips through to
    // the background) and, thanks to the exact-negation edge functions + the
    // top-left fill rule, exactly one owner. This only holds because FP
    // contraction is disabled in the intersection routine; with FMA fusion the
    // two triangles' shared-edge values stop being exact negations and the crack
    // reopens (count drops to 0) — the regression this case guards against.
    const Vector origin{0.3, -0.7, 12.0};

    for (int i = 1; i < 200; ++i)
    {
        const double s = static_cast<double>(i) / 200.0;
        const double x = -1.0 + 2.0 * s;
        const Vector target{x, x, 0.0};
        const Vector direction = target - origin;

        const Ray ray{origin, direction};
        int count = 0;
        if (rayIntersectsTriangle(ray, t1)) { ++count; }
        if (rayIntersectsTriangle(ray, t2)) { ++count; }

        INFO("oblique diagonal sample i=" << i << " target=(" << x << "," << x << ") count=" << count);
        REQUIRE(count == 1);
    }
}

TEST_CASE("Backface culling: a ray hitting the back of the quad is rejected", "[Triangle][Watertight]")
{
    const Triangle t1 = makeTri(kV0, kV1, kV2);
    const Triangle t2 = makeTri(kV0, kV2, kV3);

    // Ray travelling in +z hits the back side; front-face cull should reject.
    const Ray ray{{0.2, -0.3, -10.0}, {0.0, 0.0, 1.0}};
    REQUIRE_FALSE(rayIntersectsTriangle(ray, t1).has_value());
    REQUIRE_FALSE(rayIntersectsTriangle(ray, t2).has_value());
}
