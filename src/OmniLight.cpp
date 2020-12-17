#include "OmniLight.h"

#include "Utility.h"

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
}

void OmniLight::innerRadius(float innerRadius)
{
    m_innerRadius = innerRadius;
}

float OmniLight::innerRadius() const
{
    return m_innerRadius;
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock) const
{
    float candela = m_brightness / (Utility::pi * 4.0f);

    for (auto& photon : photonBlock)
    {
        Vector direction = Vector::randomSphere();

        Vector offset{};
        if (m_innerRadius > 0)
        {
            offset = Vector::random(m_innerRadius);
        }
        photon.ray = {position() + offset, direction};
        photon.color = m_color * (candela / photonBlock.size());
    }
}
