#pragma once

#include "Color.h"
#include "Material.h"
#include "Vector.h"

// GGX/Trowbridge-Reitz isotropic microfacet BRDF (Walter et al. 2007, "Microfacet Models
// for Refraction through Rough Surfaces"; also PBRT 4e ch. 9). Reflection-only.
//
// Parameters:
//   albedo    — base reflectance (F0 for the Schlick Fresnel approximation). For a real
//               conductor or dielectric this would be a per-wavelength quantity; the
//               current ray tracer is RGB-only, so the user supplies an RGB tint.
//   roughness — alpha in [0, 1]. alpha -> 0 collapses to a near-mirror, alpha = 1 is a
//               very rough diffuse-like surface. The implementation clamps alpha to a
//               small positive minimum so the delta limit doesn't produce NaNs.
//
// Sampling uses straight GGX half-vector sampling (sample wh from D(wh), reflect wi about
// wh to get wo). Smith's separable masking-shadowing G2 is used for both the BRDF
// evaluation and the throughput weighting. The Monte Carlo throughput weight collapses to
//   F * G2(wi, wo) * |wo . wh| / ( |wi . n| * |wh . n| )
// which is the standard Walter et al. result.
class MicrofacetMaterial : public Material
{
public:
    static constexpr double kDefaultRoughness = 0.3;

    MicrofacetMaterial();
    MicrofacetMaterial(const std::string& name, const Color& albedo = {1.0f, 1.0f, 1.0f}, double roughness = kDefaultRoughness);

    BSDFSample sample(const Vector& incident, const Vector& normal, RandomGenerator& generator) const override;
    Color evaluate(const Vector& wi, const Vector& wo, const Vector& normal) const override;
    double pdf(const Vector& wi, const Vector& wo, const Vector& normal) const override;

private:
    Color m_albedo;
    double m_alpha;
};
