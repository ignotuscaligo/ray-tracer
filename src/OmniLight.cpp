#include "OmniLight.h"

#include "Utility.h"

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
}

void OmniLight::innerRadius(double innerRadius)
{
    m_innerRadius = innerRadius;
}

double OmniLight::innerRadius() const
{
    return m_innerRadius;
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const
{
    double candela = m_brightness / (Utility::pi * 4.0);

    for (auto& photon : photonBlock)
    {
        Vector direction = Vector::randomSphere(generator);

        Vector offset{};
        if (m_innerRadius > 0)
        {
            offset = Vector::random(generator, m_innerRadius);
        }

        photon.ray = {position() + offset, direction};
        photon.color = m_color * (candela * photonBrightness);
        photon.bounces = 0;
    }
}
