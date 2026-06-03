#include <catch2/catch_all.hpp>

#include "Ray.h"
#include "Sphere.h"
#include "Vector.h"

#include <cmath>

using Catch::Matchers::WithinAbs;

// All tests use a unit-length ray direction so that the hit `distance`
// (computed as |position - origin|) equals the intersection parameter t.

TEST_CASE("Ray hits sphere at expected t and normal", "[Sphere]")
{
    // Sphere of radius 2 centered at the origin. A ray travelling along +z from
    // z = -10 enters the front face at z = -2, i.e. distance 8 from the origin,
    // with an outward normal pointing back along -z.
    const Sphere sphere{{0, 0, 0}, 2.0};
    const Ray ray{{0, 0, -10}, {0, 0, 1}};

    const std::optional<Hit> hit = rayIntersectsSphere(ray, sphere);

    REQUIRE(hit.has_value());

    REQUIRE_THAT(hit->position.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->position.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->position.z, WithinAbs(-2.0, 1e-9));

    REQUIRE_THAT(hit->distance, WithinAbs(8.0, 1e-9));

    // Outward normal at the near face points toward the incoming ray (-z).
    REQUIRE_THAT(hit->normal.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->normal.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(hit->normal.z, WithinAbs(-1.0, 1e-9));
}

TEST_CASE("Ray hits offset sphere at expected t", "[Sphere]")
{
    // Mirrors the MirrorTest geometry: a radius-55 sphere whose center is offset
    // from the origin. A ray along +z aimed straight at the center hits the near
    // face at center.z - radius.
    const Sphere sphere{{0, 90, 70}, 55.0};
    const Ray ray{{0, 90, -380}, {0, 0, 1}};

    const std::optional<Hit> hit = rayIntersectsSphere(ray, sphere);

    REQUIRE(hit.has_value());

    // Near face: z = 70 - 55 = 15; distance from origin z=-380 is 395.
    REQUIRE_THAT(hit->position.z, WithinAbs(15.0, 1e-6));
    REQUIRE_THAT(hit->distance, WithinAbs(395.0, 1e-6));
    REQUIRE_THAT(hit->normal.z, WithinAbs(-1.0, 1e-9));
}

TEST_CASE("Ray misses sphere returns no hit", "[Sphere]")
{
    const Sphere sphere{{0, 0, 0}, 2.0};

    SECTION("ray passes beside the sphere")
    {
        const Ray ray{{10, 0, -10}, {0, 0, 1}};
        REQUIRE_FALSE(rayIntersectsSphere(ray, sphere).has_value());
    }

    SECTION("sphere is entirely behind the ray origin")
    {
        const Ray ray{{0, 0, 10}, {0, 0, 1}};
        REQUIRE_FALSE(rayIntersectsSphere(ray, sphere).has_value());
    }
}

TEST_CASE("Ray originating inside the sphere hits the far surface", "[Sphere]")
{
    // Origin at the center of a radius-2 sphere; the near root is negative, so
    // the intersection should be the exit point on the far surface.
    const Sphere sphere{{0, 0, 0}, 2.0};
    const Ray ray{{0, 0, 0}, {0, 0, 1}};

    const std::optional<Hit> hit = rayIntersectsSphere(ray, sphere);

    REQUIRE(hit.has_value());

    REQUIRE_THAT(hit->position.z, WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(hit->distance, WithinAbs(2.0, 1e-9));

    // Outward normal at the exit point still points along +z (away from center).
    REQUIRE_THAT(hit->normal.z, WithinAbs(1.0, 1e-9));
}

TEST_CASE("Grazing ray tangent to the sphere", "[Sphere]")
{
    // A ray whose perpendicular distance to the center exactly equals the radius
    // touches at a single point (discriminant ~ 0). It should register a hit at
    // the tangent point with an outward normal perpendicular to the ray.
    const Sphere sphere{{0, 0, 0}, 2.0};
    const Ray ray{{2, 0, -10}, {0, 0, 1}};

    const std::optional<Hit> hit = rayIntersectsSphere(ray, sphere);

    REQUIRE(hit.has_value());

    REQUIRE_THAT(hit->position.x, WithinAbs(2.0, 1e-6));
    REQUIRE_THAT(hit->position.z, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(hit->normal.x, WithinAbs(1.0, 1e-6));
}
