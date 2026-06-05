#include <catch2/catch_all.hpp>

#include "AreaLight.h"
#include "Color.h"
#include "Photon.h"
#include "Quaternion.h"
#include "RandomGenerator.h"
#include "Utility.h"
#include "Vector.h"
#include "WorkQueue.h"

#include <cmath>
#include <vector>

// AreaLight emission / radiance tests (review gap: AreaLight had ZERO tests).
//   - surfaceRadiance() == luminousFlux / area / pi, tinted by color (square + disc)
//   - the $luminousFlux override path vs the I*pi fallback
//   - emit() origins lie uniformly across the surface (square + disc-by-area)
//   - emit() directions are front-hemisphere and cosine-distributed (Malley)

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{

// Build an axis-aligned square AreaLight (normal = +Z) with a flux override so the
// radiance is a known constant independent of the I*pi convention.
std::shared_ptr<AreaLight> makeSquareLight(double width, double height, double flux,
                                           const Color& color)
{
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Square);
    light->width(width);
    light->height(height);
    light->luminousFluxOverride(flux);
    light->color(color);
    // identity rotation => right=+X, up=+Y, normal=+Z
    light->transform.rotation = Quaternion();
    light->transform.position = Vector{0, 0, 0};
    return light;
}

std::shared_ptr<AreaLight> makeDiscLight(double radius, double flux, const Color& color)
{
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Disc);
    light->radius(radius);
    light->luminousFluxOverride(flux);
    light->color(color);
    light->transform.rotation = Quaternion();
    light->transform.position = Vector{0, 0, 0};
    return light;
}

}  // namespace

TEST_CASE("AreaLight surfaceRadiance is flux/area/pi, tinted (square)", "[AreaLight]")
{
    const double width = 2.0;
    const double height = 3.0;
    const double flux = 600.0;
    const Color tint{0.5f, 0.25f, 1.0f};
    auto light = makeSquareLight(width, height, flux, tint);

    const double area = width * height;
    REQUIRE_THAT(light->surfaceArea(), WithinRel(area, 1e-9));

    const double expectedRadiance = (flux / area) / Utility::pi;
    const Color r = light->surfaceRadiance();
    REQUIRE_THAT(r.red, WithinRel(static_cast<float>(expectedRadiance) * tint.red, 1e-5f));
    REQUIRE_THAT(r.green, WithinRel(static_cast<float>(expectedRadiance) * tint.green, 1e-5f));
    REQUIRE_THAT(r.blue, WithinRel(static_cast<float>(expectedRadiance) * tint.blue, 1e-5f));
}

TEST_CASE("AreaLight surfaceRadiance is flux/area/pi (disc)", "[AreaLight]")
{
    const double radius = 1.5;
    const double flux = 900.0;
    const Color tint{1.0f, 1.0f, 1.0f};
    auto light = makeDiscLight(radius, flux, tint);

    const double area = Utility::pi * radius * radius;
    REQUIRE_THAT(light->surfaceArea(), WithinRel(area, 1e-9));

    const double expectedRadiance = (flux / area) / Utility::pi;
    const Color r = light->surfaceRadiance();
    REQUIRE_THAT(r.red, WithinRel(static_cast<float>(expectedRadiance), 1e-5f));
}

TEST_CASE("AreaLight luminousFlux override replaces the I*pi fallback", "[AreaLight]")
{
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Square);
    light->width(1.0);
    light->height(1.0);
    light->color(Color{1.0f, 1.0f, 1.0f});

    // With NO override, luminousFlux() == I * pi (AreaLight sets m_emissionSolidAngle = pi).
    const double intensity = 10.0;
    light->intensityCandela(intensity);
    REQUIRE(light->luminousFluxOverride() == 0.0);
    REQUIRE_THAT(light->luminousFlux(), WithinRel(intensity * Utility::pi, 1e-9));

    // Setting an override (> 0) replaces I*pi directly.
    const double flux = 250.0;
    light->luminousFluxOverride(flux);
    REQUIRE_THAT(light->luminousFlux(), WithinRel(flux, 1e-9));
}

TEST_CASE("AreaLight surfaceRadiance is black for a degenerate area", "[AreaLight]")
{
    auto light = std::make_shared<AreaLight>();
    light->shape(AreaLight::Shape::Square);
    light->width(0.0);
    light->height(0.0);
    light->luminousFluxOverride(100.0);
    light->color(Color{1.0f, 1.0f, 1.0f});

    const Color r = light->surfaceRadiance();
    REQUIRE(r.red == 0.0f);
    REQUIRE(r.green == 0.0f);
    REQUIRE(r.blue == 0.0f);
}

TEST_CASE("AreaLight emit: square origins lie within the patch", "[AreaLight]")
{
    const double width = 4.0;
    const double height = 2.0;
    auto light = makeSquareLight(width, height, 100.0, Color{1.0f, 1.0f, 1.0f});

    const size_t n = 4000;
    std::vector<Photon> photons(n);
    WorkQueue<Photon>::Block block{0, n, photons};
    RandomGenerator rng{123};
    light->emit(block, /*photonFlux=*/1.0, rng);

    const double halfW = width * 0.5;
    const double halfH = height * 0.5;
    double meanU = 0.0;
    double meanV = 0.0;
    for (const Photon& p : photons)
    {
        // Origins are in the z=0 plane (normal=+Z), offsets along right=+X, up=+Y.
        REQUIRE_THAT(p.ray.origin.z, WithinAbs(0.0, 1e-9));
        REQUIRE(std::abs(p.ray.origin.x) <= halfW + 1e-9);
        REQUIRE(std::abs(p.ray.origin.y) <= halfH + 1e-9);
        meanU += p.ray.origin.x;
        meanV += p.ray.origin.y;
    }
    meanU /= static_cast<double>(n);
    meanV /= static_cast<double>(n);
    // Uniform over the rectangle => mean offset near center (0,0).
    REQUIRE_THAT(meanU, WithinAbs(0.0, 0.15));
    REQUIRE_THAT(meanV, WithinAbs(0.0, 0.10));
}

TEST_CASE("AreaLight emit: disc origins are uniform by area (not center-bunched)", "[AreaLight]")
{
    const double radius = 2.0;
    auto light = makeDiscLight(radius, 100.0, Color{1.0f, 1.0f, 1.0f});

    const size_t n = 8000;
    std::vector<Photon> photons(n);
    WorkQueue<Photon>::Block block{0, n, photons};
    RandomGenerator rng{77};
    light->emit(block, 1.0, rng);

    size_t inInnerHalfRadius = 0;
    for (const Photon& p : photons)
    {
        const double r = std::sqrt(p.ray.origin.x * p.ray.origin.x +
                                   p.ray.origin.y * p.ray.origin.y);
        REQUIRE(r <= radius + 1e-9);
        if (r <= radius * 0.5)
        {
            ++inInnerHalfRadius;
        }
    }
    // For a UNIFORM-BY-AREA disc, the fraction of points within half the radius is
    // (0.5R)^2 / R^2 = 0.25. A center-bunched (r ~ xi) sampler would put ~0.5 here.
    const double frac = static_cast<double>(inInnerHalfRadius) / static_cast<double>(n);
    REQUIRE_THAT(frac, WithinAbs(0.25, 0.04));
}

TEST_CASE("AreaLight emit: directions are front-hemisphere, cosine-distributed", "[AreaLight]")
{
    auto light = makeSquareLight(1.0, 1.0, 100.0, Color{1.0f, 1.0f, 1.0f});

    const size_t n = 20000;
    std::vector<Photon> photons(n);
    WorkQueue<Photon>::Block block{0, n, photons};
    RandomGenerator rng{555};
    light->emit(block, 1.0, rng);

    const Vector normal{0, 0, 1};
    double meanCos = 0.0;
    for (const Photon& p : photons)
    {
        const Vector d = p.ray.direction;
        REQUIRE_THAT(d.magnitude(), WithinAbs(1.0, 1e-6));
        const double c = Vector::dot(d, normal);
        // Front hemisphere only (Lambertian emitter).
        REQUIRE(c >= -1e-9);
        meanCos += c;
    }
    meanCos /= static_cast<double>(n);
    // The mean cosine of a cosine-weighted hemisphere distribution is
    //   E[cos] = integral cos * (cos/pi) dOmega = 2/3.
    // A uniform-hemisphere sampler would give 1/2; this distinguishes Malley's
    // cosine sampling from uniform.
    REQUIRE_THAT(meanCos, WithinAbs(2.0 / 3.0, 0.02));
}
