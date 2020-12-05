#include "OmniLight.h"

#include <cmath>
#include <cstdlib>

#define _USE_MATH_DEFINES
#include <math.h>

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock) const
{
    for (auto& photon : photonBlock)
    {
        float theta = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2 * M_PI;
        float z = -1.0f + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2;
        float z2 = z * z;

        Vector direction{
            std::sqrt(1 - z2) * std::cos(theta),
            std::sqrt(1 - z2) * std::sin(theta),
            z
        };

        photon.ray = {position(), direction};
        photon.color = m_color * (m_brightness / photonBlock.size());
    }
}
