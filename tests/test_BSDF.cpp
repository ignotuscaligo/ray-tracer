#include <catch2/catch_all.hpp>

#include "Color.h"
#include "LambertianMaterial.h"
#include "MicrofacetMaterial.h"
#include "MirrorMaterial.h"
#include "RandomGenerator.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>

// Contract tests for the unified BSDF interface that every Material exposes:
//   sample(incident, normal, rng) -> { direction, weight, pdf, isDelta, valid }
//   evaluate(wi, wo, normal)      -> Color  (BRDF value; 0 for delta lobes)
//   isDelta()                     -> bool   (Dirac specular vs smooth/glossy lobe)
//
// The gather/extend dispatch in the renderer keys on isDelta(): delta materials
// are EXTENDED (perfect-reflection ray recursion in MirrorGather) and contribute
// zero to the density-gather/splat; non-delta materials DEPOSIT into the grid and
// are evaluated by the gather. These tests pin the interface guarantees that
// dispatch relies on, independent of the pipeline wiring.

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kEps = 1e-9;

// Reflection of a downward (-z) incident ray off a +z-facing surface is +z-mirrored
// in x/y but the same convention as Vector::reflected: r = i - 2(i·n)n.
Vector expectedReflection(const Vector& incident, const Vector& normal)
{
    const double d = 2.0 * Vector::dot(incident, normal);
    return Vector::normalized(Vector{incident.x - d * normal.x,
                                     incident.y - d * normal.y,
                                     incident.z - d * normal.z});
}
}  // namespace

TEST_CASE("isDelta classifies the lobe type per material", "[BSDF]")
{
    LambertianMaterial diffuse{"d", Color{0.5f, 0.5f, 0.5f}};
    MirrorMaterial mirror{"m", Color{0.9f, 0.9f, 0.9f}};
    MicrofacetMaterial glossy{"g", Color{0.8f, 0.8f, 0.8f}, /*roughness=*/0.4};

    // The single property the gather/extend dispatch switches on.
    REQUIRE(diffuse.isDelta() == false);
    REQUIRE(mirror.isDelta() == true);
    REQUIRE(glossy.isDelta() == false);  // glossy is a smooth (non-Dirac) lobe
}

TEST_CASE("Mirror BSDF is a perfect-reflection delta lobe", "[BSDF]")
{
    MirrorMaterial mirror{"m", Color{0.8f, 0.7f, 0.6f}};
    RandomGenerator g{1};

    const Vector normal{0, 0, 1};
    const Vector incident = Vector::normalized(Vector{0.3, 0.0, -1.0});  // travelling into surface

    const BSDFSample s = mirror.sample(incident, normal, g);
    REQUIRE(s.valid);
    REQUIRE(s.isDelta == true);

    // Sampled direction is the exact mirror reflection.
    const Vector r = expectedReflection(incident, normal);
    REQUIRE_THAT(s.direction.x, WithinAbs(r.x, kEps));
    REQUIRE_THAT(s.direction.y, WithinAbs(r.y, kEps));
    REQUIRE_THAT(s.direction.z, WithinAbs(r.z, kEps));

    // Delta bookkeeping: throughput = albedo, pdf = 1, no cos divide folded in here.
    REQUIRE_THAT(s.weight.red, WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(s.weight.green, WithinAbs(0.7f, 1e-6f));
    REQUIRE_THAT(s.weight.blue, WithinAbs(0.6f, 1e-6f));
    REQUIRE_THAT(s.pdf, WithinAbs(1.0, 1e-12));

    // evaluate()/pdf() are zero everywhere for a delta lobe (measure zero), so the
    // density gather / camera splat correctly contribute nothing for a pure mirror.
    const Vector wo = Vector::normalized(Vector{0.2, 0.1, 1.0});
    const Vector wi = Vector::normalized(Vector{-0.2, -0.1, 1.0});
    const Color f = mirror.evaluate(wi, wo, normal);
    REQUIRE(f.red == 0.0f);
    REQUIRE(f.green == 0.0f);
    REQUIRE(f.blue == 0.0f);
    REQUIRE(mirror.pdf(wi, wo, normal) == 0.0);
}

TEST_CASE("Mirror sample is invalid when reflection points below the surface", "[BSDF]")
{
    MirrorMaterial mirror{"m", Color{1.0f, 1.0f, 1.0f}};
    RandomGenerator g{1};

    const Vector normal{0, 0, 1};
    // A photon travelling away from the surface (same side as the normal) reflects
    // to the back side; the sample must be flagged invalid so the gather drops it.
    const Vector incident = Vector::normalized(Vector{0.0, 0.0, 1.0});
    const BSDFSample s = mirror.sample(incident, normal, g);
    REQUIRE(s.valid == false);
}

TEST_CASE("Lambert BSDF throughput equals albedo and stays in the hemisphere", "[BSDF]")
{
    const Color albedo{0.6f, 0.4f, 0.2f};
    LambertianMaterial diffuse{"d", albedo};
    RandomGenerator g{1234};

    const Vector normal{0, 0, 1};
    const Vector incident = Vector::normalized(Vector{0.1, -0.2, -1.0});

    for (int i = 0; i < 256; ++i)
    {
        const BSDFSample s = diffuse.sample(incident, normal, g);
        REQUIRE(s.valid);
        REQUIRE(s.isDelta == false);

        // Cosine-weighted sampling makes f*cos/pdf collapse to exactly the albedo,
        // independent of the sampled direction — the energy-conservation guarantee
        // the 1/N daughter split relies on.
        REQUIRE_THAT(s.weight.red, WithinAbs(albedo.red, 1e-6f));
        REQUIRE_THAT(s.weight.green, WithinAbs(albedo.green, 1e-6f));
        REQUIRE_THAT(s.weight.blue, WithinAbs(albedo.blue, 1e-6f));

        // Sampled direction is unit length and in the upper hemisphere.
        REQUIRE_THAT(s.direction.magnitude(), WithinAbs(1.0, 1e-9));
        REQUIRE(Vector::dot(s.direction, normal) >= -kEps);
    }
}

TEST_CASE("Lambert evaluate is the constant albedo/pi over the hemisphere", "[BSDF]")
{
    const Color albedo{0.9f, 0.6f, 0.3f};
    LambertianMaterial diffuse{"d", albedo};
    const Vector normal{0, 0, 1};

    const float invPi = static_cast<float>(1.0 / Utility::pi);

    // View-independent: any wo in the hemisphere yields albedo/pi.
    const Vector woA = Vector::normalized(Vector{0.0, 0.0, 1.0});
    const Vector woB = Vector::normalized(Vector{0.7, 0.2, 0.6});
    const Vector wi = Vector::normalized(Vector{-0.3, 0.1, 0.9});

    for (const Vector& wo : {woA, woB})
    {
        const Color f = diffuse.evaluate(wi, wo, normal);
        REQUIRE_THAT(f.red, WithinAbs(albedo.red * invPi, 1e-6f));
        REQUIRE_THAT(f.green, WithinAbs(albedo.green * invPi, 1e-6f));
        REQUIRE_THAT(f.blue, WithinAbs(albedo.blue * invPi, 1e-6f));
    }

    // Below the surface returns zero.
    const Vector woDown = Vector::normalized(Vector{0.0, 0.0, -1.0});
    const Color f = diffuse.evaluate(wi, woDown, normal);
    REQUIRE(f.red == 0.0f);
    REQUIRE(f.green == 0.0f);
    REQUIRE(f.blue == 0.0f);
}

TEST_CASE("Lambert sampleMode peaks along the normal", "[BSDF]")
{
    LambertianMaterial diffuse{"d", Color{1.0f, 1.0f, 1.0f}};
    RandomGenerator g{1};
    const Vector normal = Vector::normalized(Vector{0.2, 0.3, 1.0});
    const Vector incident = Vector::normalized(Vector{0.0, 0.0, -1.0});

    const BSDFSample s = diffuse.sampleMode(incident, normal, g);
    REQUIRE(s.valid);
    REQUIRE_THAT(Vector::dot(s.direction, normal), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Microfacet sampleMode peaks at the perfect-reflection direction", "[BSDF]")
{
    MicrofacetMaterial glossy{"g", Color{0.5f, 0.5f, 0.5f}, /*roughness=*/0.3};
    RandomGenerator g{1};
    const Vector normal{0, 0, 1};
    const Vector incident = Vector::normalized(Vector{0.4, 0.0, -1.0});

    const BSDFSample s = glossy.sampleMode(incident, normal, g);
    REQUIRE(s.valid);
    REQUIRE(s.isDelta == false);

    const Vector r = expectedReflection(incident, normal);
    REQUIRE_THAT(s.direction.x, WithinAbs(r.x, 1e-9));
    REQUIRE_THAT(s.direction.y, WithinAbs(r.y, 1e-9));
    REQUIRE_THAT(s.direction.z, WithinAbs(r.z, 1e-9));
}

TEST_CASE("Microfacet samples stay in the hemisphere with bounded throughput", "[BSDF]")
{
    MicrofacetMaterial glossy{"g", Color{0.8f, 0.8f, 0.8f}, /*roughness=*/0.5};
    RandomGenerator g{99};
    const Vector normal{0, 0, 1};
    const Vector incident = Vector::normalized(Vector{0.2, 0.1, -1.0});

    int valid = 0;
    for (int i = 0; i < 512; ++i)
    {
        const BSDFSample s = glossy.sample(incident, normal, g);
        if (!s.valid)
        {
            continue;
        }
        ++valid;
        REQUIRE(s.isDelta == false);
        REQUIRE(Vector::dot(s.direction, normal) >= -kEps);
        // Energy-conserving throughput: f*cos/pdf weight must stay in [0,1] per channel
        // (no energy gain on a bounce).
        REQUIRE(s.weight.red >= 0.0f);
        REQUIRE(s.weight.red <= 1.0f + 1e-4f);
        REQUIRE(s.weight.green <= 1.0f + 1e-4f);
        REQUIRE(s.weight.blue <= 1.0f + 1e-4f);
    }
    REQUIRE(valid > 0);
}

TEST_CASE("Daughter counts reflect lobe width", "[BSDF]")
{
    // The fan-out count is the material's expression of how many directional
    // samples its lobe needs: a delta mirror has one valid direction; a wide
    // diffuse lobe fans out broadly; a microfacet scales with roughness.
    REQUIRE(MirrorMaterial{"m"}.daughterPhotonCount() == 1);
    REQUIRE(LambertianMaterial{"d"}.daughterPhotonCount() == 9);

    MicrofacetMaterial rough{"g", Color{1, 1, 1}, /*roughness=*/1.0};
    MicrofacetMaterial sharp{"g", Color{1, 1, 1}, /*roughness=*/0.01};
    REQUIRE(rough.daughterPhotonCount() >= sharp.daughterPhotonCount());
    REQUIRE(sharp.daughterPhotonCount() >= 1);
}
