#include "SphereVolume.h"

SphereVolume::SphereVolume()
    : Volume()
    , m_sphere({0, 0, 0}, 1.0)
{
    registerType<SphereVolume>();
}

SphereVolume::SphereVolume(size_t materialIndex)
    : Volume(materialIndex)
    , m_sphere({0, 0, 0}, 1.0)
{
    registerType<SphereVolume>();
}

SphereVolume::SphereVolume(size_t materialIndex, const Vector& center, double radius)
    : Volume(materialIndex)
    , m_sphere(center, radius)
{
    registerType<SphereVolume>();
}

void SphereVolume::center(const Vector& center)
{
    m_sphere.center = center;
}

Vector SphereVolume::center() const
{
    return m_sphere.center;
}

void SphereVolume::radius(double radius)
{
    m_sphere.radius = radius;
}

double SphereVolume::radius() const
{
    return m_sphere.radius;
}

std::optional<Hit> SphereVolume::castTransformedRay(const Ray& ray, std::vector<Hit>& /*castBuffer*/) const
{
    return rayIntersectsSphere(ray, m_sphere);
}
