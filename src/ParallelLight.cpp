#include "ParallelLight.h"

#include "Utility.h"

#include <cmath>

ParallelLight::ParallelLight()
    : Light()
{
    registerType<ParallelLight>();
}

void ParallelLight::radius(double radius)
{
    m_radius = radius;
    m_area = Utility::pi * m_radius * m_radius;
    updateParameters();
}

double ParallelLight::radius() const
{
    return m_radius;
}

void ParallelLight::emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const
{
    Vector direction = forward();
    Color photonColor = m_color * m_lumens * photonBrightness;

    for (auto& photon : photonBlock)
    {
        Vector offset{};
        if (m_radius > 0)
        {
            offset = Vector::random(generator, m_radius);
            // project onto plane with `direction` normal
            offset = offset - (Vector::dot(offset, direction) * direction);
            // spread out distribution so that it is uniform
            offset = Vector::normalized(offset) * (std::pow(offset.magnitude() / m_radius, 0.5) * m_radius);
        }

        photon.ray = {position() + offset, direction};
        photon.color = photonColor;
        photon.bounces = 0;
    }
}
