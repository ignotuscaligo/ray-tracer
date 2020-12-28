#include "OmniLight.h"

#include "Utility.h"

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
    m_area = Utility::pi * 4.0;
}

void OmniLight::innerRadius(double innerRadius)
{
    m_innerRadius = innerRadius;
    updateParameters();
}

double OmniLight::innerRadius() const
{
    return m_innerRadius;
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const
{
    Color photonColor = m_color * m_lumens * photonBrightness;

    for (auto& photon : photonBlock)
    {
        Vector direction = Vector::randomSphere(generator);

        Vector offset{};
        if (m_innerRadius > 0)
        {
            offset = Vector::random(generator, m_innerRadius);
        }

        photon.ray = {position() + offset, direction};
        photon.color = photonColor;
        photon.bounces = 0;
    }
}
