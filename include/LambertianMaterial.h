#pragma once

#include "Color.h"
#include "Material.h"
#include "Vector.h"

// Ideal diffuse reflector. BRDF f(wi, wo) = albedo / pi (constant in all directions over
// the upper hemisphere). Sampling is cosine-weighted on the hemisphere around `normal`,
// which makes f*cos(theta)/pdf = albedo — i.e. the bounce throughput is exactly the
// material's reflectance, independent of geometry. Energy-conserving as long as every
// channel of `albedo` stays in [0, 1].
class LambertianMaterial : public Material
{
public:
    LambertianMaterial();
    LambertianMaterial(const std::string& name, const Color& albedo = {1.0f, 1.0f, 1.0f});

    BSDFSample sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const override;
    Color evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    double pdf(const Vector& wi, const Vector& wo, const Vector& normal) const override;

private:
    Color m_albedo;
};
