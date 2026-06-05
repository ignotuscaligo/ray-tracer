#include <catch2/catch_all.hpp>

#include "Color.h"
#include "LambertianMaterial.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Vector.h"
#include "WorkQueue.h"

#include <cmath>
#include <vector>

// Single-photon scatter lobe test (review HIGH gap: a statistical test that the
// stochastically scattered directions follow the BRDF lobe). The single-photon model
// produces EXACTLY ONE outgoing photon per bounce via the material's stochastic
// importance sample (Material::generateDaughters, src/Material.cpp:79-105), carrying
// parentColor * weight (no 1/N split). For a Lambertian the cosine-weighted lobe is
// symmetric about the surface normal, so the MEAN scattered direction over many
// independent single-photon samples converges to the normal — and the spread is wide
// (mean cosine 2/3), which distinguishes a true cosine draw from the degenerate
// mode-only direction.
//
// Mechanics (one photon out, parentColor * BSDF weight, stochastic-not-mode) are
// covered in test_Emitter; this adds the lobe-shape statistics.

using Catch::Matchers::WithinAbs;

namespace
{

// Scatter a single outgoing photon off a Lambertian surface and return its direction.
Vector scatterOne(const LambertianMaterial& material, const Vector& incident,
                  const Vector& normal, RandomGenerator& generator)
{
    std::vector<Photon> out(1);
    WorkQueue<Photon>::Block block{0, 1, out};
    material.generateDaughters(block,
                               /*blockStart=*/0,
                               /*globalStart=*/0,
                               /*count=*/1,
                               /*totalDaughters=*/1,
                               incident,
                               normal,
                               /*position=*/Vector{0, 0, 0},
                               /*parentColor=*/Color{1.0f, 1.0f, 1.0f},
                               /*parentTime=*/0.0f,
                               /*parentBounces=*/0,
                               /*parentLightId=*/-1,
                               generator);
    return out[0].ray.direction;
}

}  // namespace

TEST_CASE("Single-photon Lambertian scatter: mean direction converges to the normal",
          "[SinglePhotonScatter]")
{
    LambertianMaterial material{"d", Color{1.0f, 1.0f, 1.0f}};
    const Vector normal{0, 0, 1};
    const Vector incident{0, 0, -1};  // travelling into the +z-facing surface

    RandomGenerator rng{2024};
    const int trials = 40000;
    Vector sum{0, 0, 0};
    double sumCos = 0.0;
    for (int i = 0; i < trials; ++i)
    {
        const Vector d = scatterOne(material, incident, normal, rng);
        REQUIRE(Vector::dot(d, normal) > 0.0);  // always in the upper hemisphere
        sum += d;
        sumCos += Vector::dot(d, normal);
    }

    const Vector mean = sum / static_cast<double>(trials);
    // By symmetry the lateral components average to ~0 and the mean points along +z.
    REQUIRE_THAT(mean.x, WithinAbs(0.0, 0.02));
    REQUIRE_THAT(mean.y, WithinAbs(0.0, 0.02));
    REQUIRE(mean.z > 0.6);  // strongly aligned with the normal

    // The mean cosine of a cosine-weighted hemisphere is E[cos] = 2/3 (a uniform
    // hemisphere would be 1/2; the degenerate mode-only sampler would be 1.0). This
    // pins the lobe SHAPE, not just the axis.
    const double meanCos = sumCos / static_cast<double>(trials);
    REQUIRE_THAT(meanCos, WithinAbs(2.0 / 3.0, 0.02));
}

TEST_CASE("Single-photon Lambertian scatter: lobe tracks a tilted normal",
          "[SinglePhotonScatter]")
{
    // The lobe is built about the SURFACE NORMAL, not a fixed axis. With a tilted
    // normal the mean scattered direction follows it.
    LambertianMaterial material{"d", Color{1.0f, 1.0f, 1.0f}};
    const Vector normal = Vector::normalized(Vector{1.0, 0.0, 1.0});  // 45 deg in x/z
    const Vector incident = Vector::normalized(Vector{0.0, 0.0, -1.0});

    RandomGenerator rng{99};
    const int trials = 40000;
    Vector sum{0, 0, 0};
    for (int i = 0; i < trials; ++i)
    {
        const Vector d = scatterOne(material, incident, normal, rng);
        REQUIRE(Vector::dot(d, normal) > 0.0);
        sum += d;
    }
    const Vector meanDir = Vector::normalized(sum / static_cast<double>(trials));
    // The mean direction aligns with the tilted normal (cosine-symmetric lobe).
    REQUIRE_THAT(Vector::dot(meanDir, normal), WithinAbs(1.0, 0.02));
}
