#include "LambertianMaterial.h"

#include "Utility.h"

#include <algorithm>
#include <cmath>

namespace
{

// Build an orthonormal basis (tangent, bitangent) such that `normal` is +Z in the local
// frame. Frisvad 2012 branchless formulation.
void buildBasis(const Vector& normal, Vector& tangent, Vector& bitangent)
{
    if (normal.z < -0.9999999)
    {
        tangent = Vector{0.0, -1.0, 0.0};
        bitangent = Vector{-1.0, 0.0, 0.0};
        return;
    }
    const double a = 1.0 / (1.0 + normal.z);
    const double b = -normal.x * normal.y * a;
    tangent = Vector{1.0 - normal.x * normal.x * a, b, -normal.x};
    bitangent = Vector{b, 1.0 - normal.y * normal.y * a, -normal.y};
}

// Cosine-weighted hemisphere sample in local (Z-up) space.
Vector cosineSampleHemisphereLocal(double u1, double u2)
{
    const double r = std::sqrt(u1);
    const double phi = Utility::pi2 * u2;
    const double x = r * std::cos(phi);
    const double y = r * std::sin(phi);
    const double z = std::sqrt(std::max(0.0, 1.0 - u1));
    return Vector{x, y, z};
}

}

LambertianMaterial::LambertianMaterial()
    : Material()
    , m_albedo({1, 1, 1})
{
}

LambertianMaterial::LambertianMaterial(const std::string& name, const Color& albedo)
    : Material(name)
    , m_albedo(albedo)
{
}

BSDFSample LambertianMaterial::sample(const Vector& /*incident*/, const Vector& normal, RandomGenerator& generator) const
{
    Vector tangent, bitangent;
    buildBasis(normal, tangent, bitangent);

    const double u1 = generator.value();
    const double u2 = generator.value();
    const Vector local = cosineSampleHemisphereLocal(u1, u2);

    Vector world = tangent * local.x + bitangent * local.y + normal * local.z;
    world = Vector::normalized(world);

    const double cosTheta = std::max(0.0, Vector::dot(world, normal));

    BSDFSample s;
    s.direction = world;
    // For cosine-weighted sampling: f * cos(theta) / pdf
    // = (albedo/pi) * cos(theta) / (cos(theta)/pi) = albedo.
    s.weight = m_albedo;
    s.pdf = cosTheta / Utility::pi;
    s.isDelta = false;
    s.valid = (cosTheta > 0.0);
    return s;
}

BSDFSample LambertianMaterial::sampleMode(const Vector& /*incident*/, const Vector& normal, RandomGenerator& /*generator*/) const
{
    // Cosine-weighted hemisphere peaks along the normal (cos(theta) is maximal
    // at theta=0). Throughput at the peak is the same as for any cosine sample:
    // f * cos(theta) / pdf = (albedo/pi) * 1 / (1/pi) = albedo. Treating this
    // deterministic direction as if it were drawn from the cosine pdf at its
    // mode gives the same per-daughter weight as a Monte Carlo sample would,
    // which is what the 1/N energy split in Material::bounce expects.
    BSDFSample s;
    s.direction = normal;
    s.weight = m_albedo;
    s.pdf = 1.0 / Utility::pi;
    s.isDelta = false;
    s.valid = true;
    return s;
}

Color LambertianMaterial::evaluate(const Vector& /*wi*/, const Vector& wo, const Vector& normal) const
{
    if (Vector::dot(wo, normal) <= 0.0)
    {
        return Color{0.0f, 0.0f, 0.0f};
    }
    return m_albedo * static_cast<float>(1.0 / Utility::pi);
}

double LambertianMaterial::pdf(const Vector& /*wi*/, const Vector& wo, const Vector& normal) const
{
    const double cosTheta = Vector::dot(wo, normal);
    if (cosTheta <= 0.0)
    {
        return 0.0;
    }
    return cosTheta / Utility::pi;
}
