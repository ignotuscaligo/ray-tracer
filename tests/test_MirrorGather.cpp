#include <catch2/catch_all.hpp>

#include "Color.h"
#include "DensityGrid.h"
#include "LambertianMaterial.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>

// MirrorGather additive-gather brightness invariant (review HIGH gap: MirrorGather
// had ZERO tests). The redesign's core correctness claim is that a mirror reads the
// reflected surface's outgoing radiance from the DensityGrid with NO 1/N photon-count
// normalization (the 1/N is baked at emission). The reflected radiance the gather
// computes at a non-delta surface is exactly:
//
//     reflected = material->evaluate(wo, wo, n) * grid.lookupIrradiance(p)
//               = (albedo / pi) * (sumPower / cellArea)
//
// (MirrorGather::reflectedRadiance, src/MirrorGather.cpp:146-149). These tests
// reproduce that composition through the same public APIs the gather calls, so they
// guard the additive / count-independent / cellArea-only behavior end-to-end without
// reaching into the gather's anonymous-namespace internals.

using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

namespace
{

// The exact radiance the mirror gather reflects toward the camera from a Lambertian
// surface point: BRDF(wo) * cell irradiance. Mirrors MirrorGather.cpp:146-149.
Color gatherReflected(const LambertianMaterial& material,
                      const DensityGrid& grid,
                      const Vector& surfacePoint,
                      const UnitVector& normal,
                      const Vector& wo)
{
    const Color brdf = material.evaluate(wo, wo, normal);
    const Color irradiance = grid.lookupIrradiance(surfacePoint);
    return brdf * irradiance;
}

}  // namespace

TEST_CASE("MirrorGather reflected radiance = BRDF * power/cellArea, no 1/N", "[MirrorGather]")
{
    const double cellSize = 2.0;
    const double cellArea = cellSize * cellSize;
    DensityGrid grid(cellSize);

    const Color albedo{0.8f, 0.6f, 0.4f};
    LambertianMaterial diffuse{"d", albedo};

    const Vector p{1.0, 1.0, 1.0};   // falls in cell (0,0,0)
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector wo{0, 0, 1};        // viewer above the surface (same-side)

    // Deposit a known TOTAL power E into the cell across several deposits.
    const Color e1{4.0f, 4.0f, 4.0f};
    const Color e2{2.0f, 2.0f, 2.0f};
    grid.add(p, e1);
    grid.add(p, e2);
    // Total deposited power = 6.0 per channel; deposit count = 2.

    const Color reflected = gatherReflected(diffuse, grid, p, normal, wo);

    // Expected: (albedo/pi) * (E / cellArea) with E = 6.0, NO division by the
    // deposit count (2). If a 1/N factor leaked in, red would be half this.
    const double E = 6.0;
    const float brdfR = albedo.red * static_cast<float>(1.0 / Utility::pi);
    const float expectedR = brdfR * static_cast<float>(E / cellArea);
    REQUIRE_THAT(reflected.red, WithinRel(expectedR, 1e-5f));

    const float brdfG = albedo.green * static_cast<float>(1.0 / Utility::pi);
    REQUIRE_THAT(reflected.green, WithinRel(brdfG * static_cast<float>(E / cellArea), 1e-5f));
    const float brdfB = albedo.blue * static_cast<float>(1.0 / Utility::pi);
    REQUIRE_THAT(reflected.blue, WithinRel(brdfB * static_cast<float>(E / cellArea), 1e-5f));
}

TEST_CASE("MirrorGather is additive: independent deposits sum", "[MirrorGather]")
{
    const double cellSize = 1.0;
    DensityGrid grid(cellSize);
    LambertianMaterial diffuse{"d", Color{1.0f, 1.0f, 1.0f}};

    const Vector p{0.5, 0.5, 0.5};
    const UnitVector n = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector wo{0, 0, 1};

    grid.add(p, Color{3.0f, 0.0f, 0.0f});
    const Color afterOne = gatherReflected(diffuse, grid, p, n, wo);

    grid.add(p, Color{5.0f, 0.0f, 0.0f});
    const Color afterTwo = gatherReflected(diffuse, grid, p, n, wo);

    // The second independent deposit of 5.0 adds linearly on top of the first 3.0:
    // afterTwo / afterOne == 8 / 3.
    REQUIRE_THAT(afterTwo.red / afterOne.red, WithinRel(8.0f / 3.0f, 1e-5f));
}

TEST_CASE("MirrorGather scales with total energy, NOT with deposit count", "[MirrorGather]")
{
    // Two grids holding the SAME total energy but with different deposit counts must
    // produce identical reflected radiance (no per-photon-count division). Doubling
    // the total energy (regardless of count) must double the output.
    const double cellSize = 1.0;
    LambertianMaterial diffuse{"d", Color{1.0f, 1.0f, 1.0f}};
    const Vector p{0.5, 0.5, 0.5};
    const UnitVector n = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector wo{0, 0, 1};

    // Grid A: total power 10.0 deposited as ONE deposit.
    DensityGrid gridA(cellSize);
    gridA.add(p, Color{10.0f, 10.0f, 10.0f});

    // Grid B: total power 10.0 deposited as TEN deposits of 1.0.
    DensityGrid gridB(cellSize);
    for (int i = 0; i < 10; ++i)
    {
        gridB.add(p, Color{1.0f, 1.0f, 1.0f});
    }

    const Color a = gatherReflected(diffuse, gridA, p, n, wo);
    const Color b = gatherReflected(diffuse, gridB, p, n, wo);
    // Same total energy, different counts (1 vs 10) => identical output.
    REQUIRE_THAT(b.red, WithinRel(a.red, 1e-5f));

    // Grid C: DOUBLE the total energy (20.0). Output must double, independent of count.
    DensityGrid gridC(cellSize);
    gridC.add(p, Color{20.0f, 20.0f, 20.0f});
    const Color c = gatherReflected(diffuse, gridC, p, n, wo);
    REQUIRE_THAT(c.red, WithinRel(2.0f * a.red, 1e-5f));
}

TEST_CASE("MirrorGather reflected radiance scales inversely with cell area", "[MirrorGather]")
{
    // The only geometric divide is by cellArea (a units conversion, not a count).
    // A grid with twice the cell edge has 4x the cell area, so the same deposited
    // power yields 1/4 the irradiance and 1/4 the reflected radiance.
    LambertianMaterial diffuse{"d", Color{1.0f, 1.0f, 1.0f}};
    const Vector p{0.25, 0.25, 0.25};
    const UnitVector n = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector wo{0, 0, 1};

    DensityGrid fine(1.0);
    fine.add(p, Color{4.0f, 4.0f, 4.0f});
    DensityGrid coarse(2.0);
    coarse.add(p, Color{4.0f, 4.0f, 4.0f});

    const Color rf = gatherReflected(diffuse, fine, p, n, wo);
    const Color rc = gatherReflected(diffuse, coarse, p, n, wo);
    // cellArea ratio is 4x => coarse is 1/4 the brightness.
    REQUIRE_THAT(rc.red, WithinRel(rf.red / 4.0f, 1e-5f));
}
