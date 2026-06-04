#pragma once

#include "Color.h"
#include "Material.h"
#include "Vector.h"

// Smooth dielectric (glass). At every surface hit a single incoming ray splits
// into a perfectly specular REFLECTED lobe and a perfectly specular REFRACTED
// (transmitted) lobe. Both lobes are Dirac deltas, so isDelta()=true, evaluate()
// and pdf() return zero — exactly like MirrorMaterial. Glass therefore rides the
// camera EXTENSION path (DielectricGather / MirrorGather), not the density gather.
//
// The split between the two lobes is governed by Fresnel reflectance R (Schlick
// approximation). The geometry:
//   - Refraction direction: Snell's law, n1 sin(theta_i) = n2 sin(theta_t).
//   - Reflection direction: mirror reflection.
//   - Total internal reflection (TIR): when leaving the denser medium past the
//     critical angle there is no transmitted ray; all energy reflects.
//   - Entering vs exiting is decided by sign(dot(rayDir, outwardNormal)):
//       dot < 0  -> entering (air -> glass): n1 = 1, n2 = ior, normal as given.
//       dot > 0  -> exiting  (glass -> air): n1 = ior, n2 = 1, normal flipped.
//
// CAMERA SIDE (sharp): the gather traces BOTH lobes and weights them by R / 1-R.
// PHOTON SIDE (forward pass): sample() picks ONE lobe stochastically by R
// (Russian roulette), returning weight 1 (energy conserved by the choice), so a
// single photon continues. Refracted photons that land on diffuse surfaces
// deposit caustic energy into the density grid.
class DielectricMaterial : public Material
{
public:
    DielectricMaterial();
    DielectricMaterial(const std::string& name, double ior = 1.5, const Color& tint = {1.0f, 1.0f, 1.0f});

    double ior() const { return m_ior; }

    // Forward (photon) sample: stochastically pick reflect or refract by Fresnel.
    BSDFSample sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const override;

    // Delta lobes contribute nothing to the density gather / splat.
    Color evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    double pdf(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    bool isDelta() const override { return true; }

    // One stochastic continuation per hit, like the mirror.
    size_t daughterPhotonCount() const override { return 1; }

    // ===== Deterministic dielectric geometry (used by the camera gather) =====

    // Result of resolving a dielectric interaction for a given incident ray.
    struct Interaction
    {
        Vector reflectDir{};   // mirror reflection (always valid)
        Vector refractDir{};   // Snell refraction (valid only when !totalInternalReflection)
        double reflectance = 1.0;  // Fresnel R in [0,1]; 1.0 under TIR
        bool totalInternalReflection = false;
        Vector orientedNormal{};   // normal oriented to the incident side (entering: as given)
        bool entering = false;
    };

    // Resolve Snell + Fresnel + TIR + entering/exiting for an incident travel
    // direction `incident` (pointing INTO the surface) and the OUTWARD geometric
    // `normal`. Pure geometry; no RNG. Static so the camera gather and the unit
    // tests can call it without a material instance.
    static Interaction resolve(const Vector& incident, const Vector& normal, double ior);

    // Schlick Fresnel reflectance for cosTheta (the cosine of the angle between
    // the incident ray and the interface normal on the incident side) and the
    // two indices of refraction. Caller is responsible for passing the cosine on
    // the side the ray approaches from (i.e. the larger-angle cosine under TIR
    // handling). Exposed for unit tests.
    static double schlickReflectance(double cosTheta, double n1, double n2);

    const Color& tint() const { return m_tint; }

private:
    double m_ior;
    Color m_tint;  // per-channel transmission/reflection tint (1,1,1 = clear glass)
};
