#include "OmniLight.h"

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock) const
{
    for (auto& photon : photonBlock)
    {
        Vector direction = Vector::randomSphere();

        photon.ray = {position(), direction};
        photon.color = m_color * (m_brightness / photonBlock.size());
    }
}
