#include "OmniLight.h"

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
    for (auto& photon : photonBlock)
    {
        Vector direction = Vector::randomSphere();

        Vector offset{};
        if (m_innerRadius > 0)
        {
            offset = Vector::random(m_innerRadius);
        }
        photon.ray = {position() + offset, direction};
        photon.color = m_color * (m_brightness / photonBlock.size());
    }
}
