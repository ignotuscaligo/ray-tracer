#include <catch2/catch_all.hpp>

#include "Quad.h"
#include "Ray.h"
#include "Vector.h"

#include <cmath>

using Catch::Matchers::WithinAbs;

// rayIntersectsQuad: ray-plane then an inside test in the quad's 2D parameter
// basis, returning position, a view-facing normal, UV in [0,1]^2, and distance.

namespace
{
// A 2x2 axis-aligned rectangle in the z=0 plane: origin at (-1,-1,0), edgeU
// along +x (length 2), edgeV along +y (length 2). So the quad spans
// x in [-1,1], y in [-1,1].
Quad makeUnitRect()
{
    return Quad{{-1.0, -1.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 2.0, 0.0}};
}
} // namespace

TEST_CASE("Ray hits quad center at expected point, normal, distance, uv", "[Quad]")
{
    const Quad quad = makeUnitRect();
    // Ray from +z aimed at the quad center (0,0,0).
    const Ray ray{{0.0, 0.0, 10.0}, {0.0, 0.0, -1.0}};

    const std::optional<Hit> hit = rayIntersectsQuad(ray, quad);

    REQUIRE(hit.has_value());

    REQUIRE_THAT(hit->position.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->position.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->position.z, WithinAbs(0.0, 1e-9));

    REQUIRE_THAT(hit->distance, WithinAbs(10.0, 1e-9));

    // Two-sided: the normal faces back toward the +z ray origin.
    REQUIRE_THAT(hit->normal.z, WithinAbs(1.0, 1e-9));

    // Center maps to (u, v) = (0.5, 0.5).
    REQUIRE_THAT(hit->uv.x, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(hit->uv.y, WithinAbs(0.5, 1e-9));
}

TEST_CASE("Quad UV spans corners 0..1", "[Quad]")
{
    const Quad quad = makeUnitRect();

    struct Sample { double x, y, u, v; };
    const Sample samples[] = {
        {-1.0, -1.0, 0.0, 0.0}, // origin corner
        { 1.0, -1.0, 1.0, 0.0}, // along edgeU
        {-1.0,  1.0, 0.0, 1.0}, // along edgeV
        { 1.0,  1.0, 1.0, 1.0}, // opposite corner
    };

    for (const auto& s : samples)
    {
        const Ray ray{{s.x, s.y, 5.0}, {0.0, 0.0, -1.0}};
        const std::optional<Hit> hit = rayIntersectsQuad(ray, quad);

        INFO("corner (" << s.x << ", " << s.y << ")");
        REQUIRE(hit.has_value());
        REQUIRE_THAT(hit->uv.x, WithinAbs(s.u, 1e-9));
        REQUIRE_THAT(hit->uv.y, WithinAbs(s.v, 1e-9));
    }
}

TEST_CASE("Ray missing the quad returns no hit", "[Quad]")
{
    const Quad quad = makeUnitRect();

    SECTION("ray passes outside the quad extent")
    {
        const Ray ray{{5.0, 0.0, 10.0}, {0.0, 0.0, -1.0}};
        REQUIRE_FALSE(rayIntersectsQuad(ray, quad).has_value());
    }

    SECTION("just outside an edge")
    {
        const Ray ray{{1.0001, 0.0, 10.0}, {0.0, 0.0, -1.0}};
        REQUIRE_FALSE(rayIntersectsQuad(ray, quad).has_value());
    }

    SECTION("quad behind the ray origin")
    {
        const Ray ray{{0.0, 0.0, -10.0}, {0.0, 0.0, -1.0}};
        REQUIRE_FALSE(rayIntersectsQuad(ray, quad).has_value());
    }

    SECTION("ray parallel to the quad plane")
    {
        const Ray ray{{0.0, 0.0, 1.0}, {1.0, 0.0, 0.0}};
        REQUIRE_FALSE(rayIntersectsQuad(ray, quad).has_value());
    }
}

TEST_CASE("Quad is two-sided: visible from the back", "[Quad]")
{
    const Quad quad = makeUnitRect();

    // Ray travelling in +z hits the back face; the quad should still register a
    // hit, with the normal flipped to face the incoming ray (-z).
    const Ray ray{{0.2, -0.3, -10.0}, {0.0, 0.0, 1.0}};
    const std::optional<Hit> hit = rayIntersectsQuad(ray, quad);

    REQUIRE(hit.has_value());
    REQUIRE_THAT(hit->normal.z, WithinAbs(-1.0, 1e-9));
}

TEST_CASE("Quad from four corners matches origin+edges", "[Quad]")
{
    const Vector c0{-1.0, -1.0, 0.0};
    const Vector c1{1.0, -1.0, 0.0};
    const Vector c2{1.0, 1.0, 0.0};
    const Vector c3{-1.0, 1.0, 0.0};

    const Quad fromCorners = Quad::fromCorners(c0, c1, c2, c3);
    const Quad fromEdges = makeUnitRect();

    const Ray ray{{0.3, 0.4, 8.0}, {0.0, 0.0, -1.0}};
    const std::optional<Hit> a = rayIntersectsQuad(ray, fromCorners);
    const std::optional<Hit> b = rayIntersectsQuad(ray, fromEdges);

    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE_THAT(a->uv.x, WithinAbs(b->uv.x, 1e-12));
    REQUIRE_THAT(a->uv.y, WithinAbs(b->uv.y, 1e-12));
    REQUIRE_THAT(a->distance, WithinAbs(b->distance, 1e-12));
}

TEST_CASE("Non-axis-aligned (sheared) quad recovers correct uv", "[Quad]")
{
    // A parallelogram (edges not perpendicular) to exercise the dual-basis
    // parameter recovery rather than a trivial orthonormal case.
    const Quad quad{{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {1.0, 2.0, 0.0}};

    // P(0.5, 0.5) = origin + 0.5*edgeU + 0.5*edgeV = (1.5, 1.0, 0).
    const Ray ray{{1.5, 1.0, 7.0}, {0.0, 0.0, -1.0}};
    const std::optional<Hit> hit = rayIntersectsQuad(ray, quad);

    REQUIRE(hit.has_value());
    REQUIRE_THAT(hit->uv.x, WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(hit->uv.y, WithinAbs(0.5, 1e-9));
}
