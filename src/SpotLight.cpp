#include "SpotLight.h"

#include "Utility.h"

SpotLight::SpotLight()
    : Light()
{
    registerType<SpotLight>();
}

void SpotLight::innerRadius(double innerRadius)
{
    m_innerRadius = innerRadius;
}

double SpotLight::innerRadius() const
{
    return m_innerRadius;
}

void SpotLight::angle(double angle)
{
    m_angle = std::min(std::max(0.0, angle), Utility::pi2);
    double coneAngle = m_angle;

    if (m_angle > Utility::pi)
    {
        coneAngle = Utility::pi2 - m_angle;
    }

    double coneHeight = std::sin(coneAngle / 2.0);
    double capHeight = 1.0 - coneHeight;
    m_area = Utility::pi2 * capHeight;

    if (m_angle > Utility::pi)
    {
        m_area = Utility::pi4 - m_area;
    }

    m_angleGenerator.maxAngle = m_angle / 2.0;
}

double SpotLight::angle() const
{
    return m_angle;
}

void SpotLight::emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const
{
    double candela = 0.0;

    if (m_area > 0.0)
    {
        candela = m_brightness / m_area;
    }

    Vector direction = forward();

    for (auto& photon : photonBlock)
    {
        Vector offsetDirection = m_angleGenerator.generateOffsetVector(direction, generator);

        Vector offset{};
        if (m_innerRadius > 0)
        {
            offset = Vector::random(generator, m_innerRadius);
        }

        photon.ray = {position() + offset, offsetDirection};
        photon.color = m_color * (candela * photonBrightness);
        photon.bounces = 0;
    }
}
