#include "MirrorMaterial.h"

MirrorMaterial::MirrorMaterial()
    : Material()
    , m_albedo({1, 1, 1})
{
}

MirrorMaterial::MirrorMaterial(const std::string& name, const Color& albedo)
    : Material(name)
    , m_albedo(albedo)
{
}

BSDFSample MirrorMaterial::sample(const Vector& incident, const UnitVector& normal, RandomGenerator& /*generator*/) const
{
    BSDFSample s;
    s.direction = Vector::normalized(Vector::reflected(incident, normal));
    // For a delta BRDF, the conventional bookkeeping is throughput = albedo with pdf = 1
    // and isDelta = true. The integrator must NOT divide by cos(theta) here — that factor
    // is already folded into the delta normalization.
    s.weight = m_albedo;
    s.pdf = 1.0;
    s.isDelta = true;
    s.valid = (Vector::dot(s.direction, normal) > 0.0);
    return s;
}

Color MirrorMaterial::evaluate(const Vector& /*wi*/, const Vector& /*wo*/, const UnitVector& /*normal*/) const
{
    return Color{0.0f, 0.0f, 0.0f};
}

double MirrorMaterial::pdf(const Vector& /*wi*/, const Vector& /*wo*/, const UnitVector& /*normal*/) const
{
    return 0.0;
}
