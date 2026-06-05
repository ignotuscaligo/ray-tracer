#include <catch2/catch_all.hpp>

#include "Color.h"
#include "DielectricMaterial.h"
#include "RandomGenerator.h"
#include "Utility.h"
#include "Vector.h"

#include <cmath>

// Geometry/contract tests for the smooth dielectric (glass) BSDF:
//   - Snell refraction direction (entering and exiting)
//   - Fresnel reflectance at normal and grazing incidence
//   - Total internal reflection threshold (critical angle)
//   - Entering vs exiting normal handling (sign of dot(rayDir, normal))
//   - isDelta()/evaluate()/pdf() match the mirror's delta contract
//
// resolve() is pure geometry (no RNG); sample() adds the stochastic Fresnel pick.

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kIor = 1.5;
}  // namespace

TEST_CASE("Dielectric is a delta material with zero evaluate/pdf", "[Dielectric]")
{
    DielectricMaterial glass{"g", kIor};
    REQUIRE(glass.isDelta() == true);
    REQUIRE(glass.daughterPhotonCount() == 1);

    const Vector n{0, 0, 1};
    const Vector a = Vector::normalized(Vector{0.2, 0.1, 1.0});
    const Vector b = Vector::normalized(Vector{-0.2, -0.1, 1.0});
    const Color f = glass.evaluate(a, b, n);
    REQUIRE(f.red == 0.0f);
    REQUIRE(f.green == 0.0f);
    REQUIRE(f.blue == 0.0f);
    REQUIRE(glass.pdf(a, b, n) == 0.0);
}

TEST_CASE("Entering: normal-incidence refraction passes straight through", "[Dielectric]")
{
    // Ray travelling along -z into a +z-facing surface (air -> glass). At normal
    // incidence the transmitted ray is undeviated and the reflected ray bounces
    // straight back.
    const Vector incident{0, 0, -1};
    const Vector normal{0, 0, 1};

    const auto it = DielectricMaterial::resolve(incident, normal, kIor);
    REQUIRE(it.entering == true);
    REQUIRE(it.totalInternalReflection == false);

    // Refraction undeviated: still -z.
    REQUIRE_THAT(it.refractDir.x, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(it.refractDir.y, WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(it.refractDir.z, WithinAbs(-1.0, 1e-9));

    // Reflection straight back: +z.
    REQUIRE_THAT(it.reflectDir.z, WithinAbs(1.0, 1e-9));

    // Oriented normal on the incident side is the outward normal (entering).
    REQUIRE_THAT(it.orientedNormal.z, WithinAbs(1.0, 1e-9));
}

TEST_CASE("Entering: Snell's law bends the ray toward the normal", "[Dielectric]")
{
    // 45-degree incidence in the x/z plane, air -> glass (ior 1.5).
    // sin(theta_t) = sin(45)/1.5 -> theta_t = asin(sin45/1.5).
    const double thetaI = Utility::radians(45.0);
    const Vector incident = Vector::normalized(Vector{std::sin(thetaI), 0.0, -std::cos(thetaI)});
    const Vector normal{0, 0, 1};

    const auto it = DielectricMaterial::resolve(incident, normal, kIor);
    REQUIRE(it.totalInternalReflection == false);

    const double expectedThetaT = std::asin(std::sin(thetaI) / kIor);
    // The transmitted ray is on the far side (-z) and bent toward the normal.
    const Vector t = it.refractDir;
    const double cosT = -t.z;                     // angle from -z axis (the normal on far side)
    const double sinT = std::sqrt(std::max(0.0, t.x * t.x + t.y * t.y));
    REQUIRE_THAT(std::atan2(sinT, cosT), WithinAbs(expectedThetaT, 1e-6));
    // Same lateral direction (+x) as the incident ray.
    REQUIRE(t.x > 0.0);
    REQUIRE(t.z < 0.0);
}

TEST_CASE("Exiting: flips the normal and uses glass->air indices", "[Dielectric]")
{
    // Ray travelling +z from inside the glass through a +z-facing outward normal:
    // dot(dir, normal) > 0 -> exiting. Small angle so no TIR.
    const double thetaI = Utility::radians(10.0);
    const Vector incident = Vector::normalized(Vector{std::sin(thetaI), 0.0, std::cos(thetaI)});
    const Vector normal{0, 0, 1};

    const auto it = DielectricMaterial::resolve(incident, normal, kIor);
    REQUIRE(it.entering == false);
    REQUIRE(it.totalInternalReflection == false);
    // Oriented normal is flipped to face back against the incident ray (-z).
    REQUIRE_THAT(it.orientedNormal.z, WithinAbs(-1.0, 1e-9));

    // Exiting bends AWAY from the normal: theta_t > theta_i.
    const double expectedThetaT = std::asin(std::sin(thetaI) * kIor);
    const Vector t = it.refractDir;
    const double cosT = t.z;  // transmitted travels +z out of the surface
    const double sinT = std::sqrt(std::max(0.0, t.x * t.x + t.y * t.y));
    REQUIRE_THAT(std::atan2(sinT, cosT), WithinAbs(expectedThetaT, 1e-6));
    REQUIRE(t.z > 0.0);
}

TEST_CASE("Total internal reflection past the critical angle", "[Dielectric]")
{
    // Critical angle for glass->air: theta_c = asin(1/1.5) ~ 41.8 degrees.
    const double critical = std::asin(1.0 / kIor);

    // Just inside the critical angle: refraction exists.
    {
        const double thetaI = critical - Utility::radians(2.0);
        const Vector incident = Vector::normalized(Vector{std::sin(thetaI), 0.0, std::cos(thetaI)});
        const auto it = DielectricMaterial::resolve(incident, Vector{0, 0, 1}, kIor);
        REQUIRE(it.totalInternalReflection == false);
        REQUIRE(it.reflectance < 1.0);
    }

    // Just past the critical angle: TIR, all reflection.
    {
        const double thetaI = critical + Utility::radians(2.0);
        const Vector incident = Vector::normalized(Vector{std::sin(thetaI), 0.0, std::cos(thetaI)});
        const auto it = DielectricMaterial::resolve(incident, Vector{0, 0, 1}, kIor);
        REQUIRE(it.totalInternalReflection == true);
        REQUIRE_THAT(it.reflectance, WithinAbs(1.0, 1e-12));
        // Outward normal is +z, so the glass occupies the -z half-space. A ray
        // travelling +z hits the interface from inside; under TIR it reflects
        // back INTO the glass, i.e. its z-component flips to negative.
        REQUIRE(it.reflectDir.z < 0.0);
    }
}

TEST_CASE("Fresnel: low reflectance at normal incidence, ->1 at grazing", "[Dielectric]")
{
    // At normal incidence (cos = 1), R = R0 = ((1-1.5)/(1+1.5))^2 = 0.04.
    const double r0 = DielectricMaterial::schlickReflectance(1.0, 1.0, kIor);
    REQUIRE_THAT(r0, WithinAbs(0.04, 1e-6));

    // Approaching grazing (cos -> 0), R -> 1.
    const double rGrazing = DielectricMaterial::schlickReflectance(0.0, 1.0, kIor);
    REQUIRE_THAT(rGrazing, WithinAbs(1.0, 1e-9));

    // Monotonic: more reflection as the angle increases (cos decreases).
    const double rMid = DielectricMaterial::schlickReflectance(0.5, 1.0, kIor);
    REQUIRE(rMid > r0);
    REQUIRE(rMid < rGrazing);
}

TEST_CASE("Fresnel reflectance is continuous across the entering/exiting boundary",
          "[Dielectric]")
{
    // Review gap: assert the cosForFresnel side-selection (DielectricMaterial.cpp:97)
    // makes reflectance continuous as the incidence sweeps from just-entering (air->
    // glass, n1<n2, uses cosI) to just-exiting (glass->air, n1>n2, uses cosT). Probe
    // resolve() at a small angle on each side; both should report ~R0 (~0.04) and be
    // close to each other rather than jumping.
    const double smallAngle = Utility::radians(3.0);

    // Entering: ray opposes the outward +z normal (dot < 0), air -> glass.
    const Vector enterDir =
        Vector::normalized(Vector{std::sin(smallAngle), 0.0, -std::cos(smallAngle)});
    const auto entering = DielectricMaterial::resolve(enterDir, Vector{0, 0, 1}, kIor);
    REQUIRE(entering.entering == true);
    REQUIRE(entering.totalInternalReflection == false);

    // Exiting: ray travels with the outward +z normal (dot > 0), glass -> air. A 3-deg
    // internal angle is well under the ~41.8-deg critical angle, so no TIR.
    const Vector exitDir =
        Vector::normalized(Vector{std::sin(smallAngle), 0.0, std::cos(smallAngle)});
    const auto exiting = DielectricMaterial::resolve(exitDir, Vector{0, 0, 1}, kIor);
    REQUIRE(exiting.entering == false);
    REQUIRE(exiting.totalInternalReflection == false);

    // Both sides near-normal incidence report ~R0 = 0.04 and agree closely: no
    // discontinuity at the boundary.
    REQUIRE_THAT(entering.reflectance, WithinAbs(0.04, 5e-3));
    REQUIRE_THAT(exiting.reflectance, WithinAbs(0.04, 5e-3));
    REQUIRE_THAT(entering.reflectance, WithinAbs(exiting.reflectance, 5e-3));
}

TEST_CASE("Schlick reflectance increases monotonically toward grazing", "[Dielectric]")
{
    // Review gap (grazing emphasis): reflectance is low near normal incidence and
    // rises monotonically to 1 at grazing. Sweep cosTheta from 1 (normal) to 0
    // (grazing) and assert a strictly increasing R.
    double previous = DielectricMaterial::schlickReflectance(1.0, 1.0, kIor);
    REQUIRE_THAT(previous, WithinAbs(0.04, 1e-6));
    for (int i = 9; i >= 0; --i)
    {
        const double cosTheta = static_cast<double>(i) / 10.0;
        const double r = DielectricMaterial::schlickReflectance(cosTheta, 1.0, kIor);
        REQUIRE(r > previous);  // strictly increasing as the angle approaches grazing
        previous = r;
    }
    // At exact grazing R -> 1.
    REQUIRE_THAT(DielectricMaterial::schlickReflectance(0.0, 1.0, kIor), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Stochastic sample picks reflect/refract roughly by Fresnel R", "[Dielectric]")
{
    // At ~normal incidence R ~ 0.04, so ~96% of samples should be the refracted
    // (through, -z) lobe. We count how many land on the far side.
    DielectricMaterial glass{"g", kIor};
    RandomGenerator rng{7};

    const Vector incident{0, 0, -1};
    const Vector normal{0, 0, 1};

    int refracted = 0;
    const int trials = 4000;
    for (int i = 0; i < trials; ++i)
    {
        const BSDFSample s = glass.sample(incident, normal, rng);
        REQUIRE(s.valid);
        REQUIRE(s.isDelta == true);
        if (s.direction.z < 0.0)
        {
            ++refracted;  // transmitted through to the far side
        }
    }
    const double refractFraction = static_cast<double>(refracted) / trials;
    // Expect ~0.96 transmitted; allow generous Monte Carlo slack.
    REQUIRE(refractFraction > 0.90);
    REQUIRE(refractFraction < 1.0);
}

TEST_CASE("Sample under TIR always reflects", "[Dielectric]")
{
    DielectricMaterial glass{"g", kIor};
    RandomGenerator rng{3};

    const double critical = std::asin(1.0 / kIor);
    const double thetaI = critical + Utility::radians(5.0);
    const Vector incident = Vector::normalized(Vector{std::sin(thetaI), 0.0, std::cos(thetaI)});
    const Vector normal{0, 0, 1};

    for (int i = 0; i < 500; ++i)
    {
        const BSDFSample s = glass.sample(incident, normal, rng);
        REQUIRE(s.valid);
        // Reflected back into the glass (-z half-space) every time.
        REQUIRE(s.direction.z < 0.0);
    }
}
