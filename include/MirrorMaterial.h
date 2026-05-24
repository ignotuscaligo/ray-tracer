#pragma once

#include "Color.h"
#include "Material.h"
#include "Vector.h"

// Perfect specular reflector. BRDF is a Dirac delta along the reflection direction. The
// sample() always returns the mirror-reflected direction with throughput = albedo and
// pdf = 1 (with isDelta = true so density-based code knows not to evaluate the BRDF
// numerically). evaluate() and pdf() return zero in non-mirror directions, which is the
// honest answer — a delta distribution has measure zero everywhere except the impulse.
class MirrorMaterial : public Material
{
public:
    MirrorMaterial();
    MirrorMaterial(const std::string& name, const Color& albedo = {1.0f, 1.0f, 1.0f});

    BSDFSample sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const override;
    Color evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    double pdf(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    bool isDelta() const override { return true; }

    // Delta BRDF — only one valid outgoing direction.
    size_t daughterPhotonCount() const override { return 1; }

private:
    Color m_albedo;
};
