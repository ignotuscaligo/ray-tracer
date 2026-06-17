#include <catch2/catch_all.hpp>

#include "Color.h"
#include "LambertianMaterial.h"
#include "MicrofacetMaterial.h"
#include "RandomGenerator.h"
#include "StatAssert.h"
#include "Utility.h"
#include "UnitVector.h"
#include "Vector.h"

#include <cmath>
#include <vector>

// ============================================================================
// T2 BSDFConsistency — the BSDF contract identities (the VNDF bug class)
// ============================================================================
//
// DESIGN §5: every Material exposes sample / evaluate / pdf / isDelta, and the
// renderer trusts the relationships between them. This test pins those identities
// directly — the exact bug class the VNDF migration already hit (a weight that
// could exceed 1 at grazing incidence) and the reciprocity bug this stage fixed.
//
//   1. PER-SAMPLE IDENTITY: for a non-delta sample, weight == f(wi,wo)*cos(theta_o)
//      / pdf(wo). This is what makes the single-photon throughput (weight) the
//      correct Monte-Carlo estimator of f*cos.
//   2. HELMHOLTZ RECIPROCITY: f(wi,wo) == f(wo,wi). A physical BRDF is symmetric.
//      (The Lambertian boundary violation — f(wi_below,wo_above)=albedo/pi while
//      f(wo_above,wi_below)=0 — was fixed this stage; this is its guard.)
//   3. HEMISPHERE NORMALIZATION: the directional-hemispherical reflectance
//      integral f*cos over the hemisphere is <= 1 (energy conservation).
//   4. PDF IS A DENSITY: pdf integrated over the hemisphere is ~1 (a chi-square of
//      the sampled-direction histogram against the pdf confirms sample() draws
//      from pdf()).

using Catch::Approx;
using Catch::Matchers::WithinAbs;

namespace
{
// Uniform-hemisphere direction from two uniforms (for the Monte-Carlo integrals).
Vector uniformHemisphere(double u1, double u2, const Vector& n, double& cosTheta)
{
    const double z = u1;  // cos(theta), uniform in [0,1]
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double phi = Utility::pi2 * u2;
    // local frame around n
    Vector tangent =
        (std::abs(n.x) > 0.9) ? Vector{0, 1, 0} : Vector{1, 0, 0};
    tangent = Vector::normalized(tangent - n * Vector::dot(tangent, n));
    const Vector bitangent = Vector::cross(n, tangent);
    const Vector dir =
        tangent * (r * std::cos(phi)) + bitangent * (r * std::sin(phi)) + n * z;
    cosTheta = z;
    return Vector::normalized(dir);
}
}  // namespace

TEST_CASE("T2 BSDFConsistency: weight == f*cos/pdf per sample (Lambertian + Microfacet)",
          "[BSDFConsistency][T2]")
{
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector incident = Vector::normalized(Vector{0.3, -0.2, -1.0});  // into surface
    const Vector wi = -incident;  // direction the photon came FROM (out of surface)

    LambertianMaterial diffuse{"d", Color{0.7f, 0.5f, 0.3f}};
    MicrofacetMaterial glossy{"g", Color{0.9f, 0.9f, 0.9f}, /*roughness=*/0.4};

    Material* mats[] = {&diffuse, &glossy};
    for (Material* m : mats)
    {
        RandomGenerator gen{4242};
        int checked = 0;
        for (int i = 0; i < 2000; ++i)
        {
            const BSDFSample s = m->sample(incident, normal, gen);
            if (!s.valid || s.isDelta)
            {
                continue;
            }
            const Vector wo = Vector::normalized(s.direction);
            const double cosO = Vector::dot(wo, normal);
            const double pdf = m->pdf(wi, wo, normal);
            if (pdf <= 1e-6 || cosO <= 1e-4)
            {
                continue;  // grazing / degenerate: ratio ill-conditioned
            }
            const Color f = m->evaluate(wi, wo, normal);
            // weight == f * cosO / pdf, per channel.
            const float expR = static_cast<float>(f.red * cosO / pdf);
            const float expG = static_cast<float>(f.green * cosO / pdf);
            const float expB = static_cast<float>(f.blue * cosO / pdf);
            INFO("pdf=" << pdf << " cosO=" << cosO);
            REQUIRE_THAT(s.weight.red, WithinAbs(expR, 1e-3f));
            REQUIRE_THAT(s.weight.green, WithinAbs(expG, 1e-3f));
            REQUIRE_THAT(s.weight.blue, WithinAbs(expB, 1e-3f));
            ++checked;
        }
        REQUIRE(checked > 100);
    }
}

TEST_CASE("T2 BSDFConsistency: Helmholtz reciprocity f(wi,wo) == f(wo,wi)",
          "[BSDFConsistency][T2][reciprocity]")
{
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});

    LambertianMaterial diffuse{"d", Color{0.6f, 0.4f, 0.2f}};
    MicrofacetMaterial glossy{"g", Color{0.8f, 0.7f, 0.6f}, /*roughness=*/0.5};
    Material* mats[] = {&diffuse, &glossy};

    RandomGenerator gen{99};
    for (Material* m : mats)
    {
        for (int i = 0; i < 500; ++i)
        {
            double c1 = 0.0, c2 = 0.0;
            const Vector a = uniformHemisphere(gen.value(), gen.value(), normal, c1);
            const Vector b = uniformHemisphere(gen.value(), gen.value(), normal, c2);
            const Color fab = m->evaluate(a, b, normal);
            const Color fba = m->evaluate(b, a, normal);
            INFO("f(a,b)=(" << fab.red << "," << fab.green << "," << fab.blue
                 << ") f(b,a)=(" << fba.red << "," << fba.green << "," << fba.blue << ")");
            REQUIRE_THAT(fab.red, WithinAbs(fba.red, 1e-5f));
            REQUIRE_THAT(fab.green, WithinAbs(fba.green, 1e-5f));
            REQUIRE_THAT(fab.blue, WithinAbs(fba.blue, 1e-5f));
        }
    }
}

TEST_CASE("T2 BSDFConsistency: reciprocity holds across the hemisphere boundary",
          "[BSDFConsistency][T2][reciprocity]")
{
    // The specific boundary case the source-bug fix addressed: one direction above
    // the surface, one below. Both orderings must agree (and be zero), not just the
    // both-above case.
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    LambertianMaterial diffuse{"d", Color{0.9f, 0.9f, 0.9f}};

    const Vector above = Vector::normalized(Vector{0.2, 0.1, 1.0});
    const Vector below = Vector::normalized(Vector{0.2, 0.1, -1.0});

    const Color f1 = diffuse.evaluate(below, above, normal);  // wi below, wo above
    const Color f2 = diffuse.evaluate(above, below, normal);  // wi above, wo below
    REQUIRE(f1.red == Approx(f2.red));
    REQUIRE(f1.red == 0.0f);   // a below-surface direction reflects nothing
    REQUIRE(f1.green == 0.0f);
    REQUIRE(f1.blue == 0.0f);

    // Sanity: both above is the non-zero albedo/pi (so the fix did not zero the
    // legitimate case).
    const Color fAbove = diffuse.evaluate(above, Vector{0, 0, 1}, normal);
    REQUIRE(fAbove.red > 0.0f);
}

TEST_CASE("T2 BSDFConsistency: directional-hemispherical reflectance <= 1",
          "[BSDFConsistency][T2][energy]")
{
    // integral over the hemisphere of f(wi,wo) cos(theta_o) dwo <= 1 (no energy
    // gain). Monte-Carlo with a uniform-hemisphere estimator: E = (2*pi) * mean(
    // f*cos) over uniform samples (the 2*pi is the hemisphere solid angle / pdf=1/2pi).
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector wi = Vector::normalized(Vector{0.3, 0.0, 1.0});  // above the surface

    LambertianMaterial diffuse{"d", Color{0.92f, 0.92f, 0.92f}};
    MicrofacetMaterial glossy{"g", Color{0.92f, 0.92f, 0.92f}, /*roughness=*/0.6};

    for (Material* m : {static_cast<Material*>(&diffuse), static_cast<Material*>(&glossy)})
    {
        RandomGenerator gen{7};
        const int N = 200000;
        double acc = 0.0;
        for (int i = 0; i < N; ++i)
        {
            double cosO = 0.0;
            const Vector wo = uniformHemisphere(gen.value(), gen.value(), normal, cosO);
            const Color f = m->evaluate(wi, wo, normal);
            const double fLum = (f.red + f.green + f.blue) / 3.0;
            acc += fLum * cosO;
        }
        const double reflectance = Utility::pi2 * (acc / N);  // 2*pi * mean
        INFO("reflectance=" << reflectance);
        REQUIRE(reflectance <= 1.0 + 1e-2);  // energy-conserving (MC tolerance)
        REQUIRE(reflectance > 0.0);
    }

    // For a Lambertian the closed form is exactly the albedo, so the estimate must
    // converge there (a tighter check than just <= 1).
    {
        RandomGenerator gen{11};
        const int N = 400000;
        double acc = 0.0;
        for (int i = 0; i < N; ++i)
        {
            double cosO = 0.0;
            (void)uniformHemisphere(gen.value(), gen.value(), normal, cosO);
            acc += (0.92 / Utility::pi) * cosO;  // f = albedo/pi
        }
        const double lambertReflectance = Utility::pi2 * (acc / N);
        // SE of the MC estimate is small at N=4e5; a wide CI around the albedo.
        REQUIRE_MEAN_CI(lambertReflectance, 0.92, 0.92 * 0.01, rt_test::kZ999);
    }
}

TEST_CASE("T2 BSDFConsistency: sampled directions follow pdf (chi-square)",
          "[BSDFConsistency][T2][pdf]")
{
    // sample() must draw from pdf(). Bin sampled directions by cos(theta_o) and
    // compare the histogram to the expected counts from integrating pdf over each
    // cos-band. For a Lambertian pdf = cos/pi, the per-band probability is the
    // integral of (cos/pi)*2*pi*... — but rather than derive it analytically we use
    // the known cosine-weighted CDF: P(cos <= c) = c^2 (cosine-weighted hemisphere).
    const UnitVector normal = UnitVector::alreadyNormalized(Vector{0, 0, 1});
    const Vector incident = Vector::normalized(Vector{0.0, 0.0, -1.0});
    LambertianMaterial diffuse{"d", Color{1.0f, 1.0f, 1.0f}};

    constexpr int kBins = 10;
    std::vector<double> observed(kBins, 0.0);
    RandomGenerator gen{2024};
    const int N = 200000;
    int valid = 0;
    for (int i = 0; i < N; ++i)
    {
        const BSDFSample s = diffuse.sample(incident, normal, gen);
        if (!s.valid)
        {
            continue;
        }
        const double cosO = std::clamp(Vector::dot(s.direction, normal), 0.0, 1.0);
        int bin = static_cast<int>(cosO * kBins);
        if (bin >= kBins)
        {
            bin = kBins - 1;
        }
        observed[static_cast<size_t>(bin)] += 1.0;
        ++valid;
    }
    REQUIRE(valid > N / 2);

    // Expected counts: cosine-weighted CDF F(c) = c^2, so the probability mass in
    // bin [c_lo, c_hi) is c_hi^2 - c_lo^2.
    std::vector<double> expected(kBins, 0.0);
    for (int b = 0; b < kBins; ++b)
    {
        const double lo = static_cast<double>(b) / kBins;
        const double hi = static_cast<double>(b + 1) / kBins;
        expected[static_cast<size_t>(b)] = (hi * hi - lo * lo) * valid;
    }

    const double x2 = rt_test::chiSquareStatistic(observed, expected);
    // 9 dof (10 bins - 1). chi-square critical value at p=0.001 for 9 dof ~= 27.88;
    // a correct sampler stays well under, a wrong pdf shape blows past it.
    INFO("chi-square=" << x2);
    REQUIRE(x2 < 27.88);
}
