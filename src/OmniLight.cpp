#include "OmniLight.h"

#include "Utility.h"

OmniLight::OmniLight()
    : Light()
{
    registerType<OmniLight>();
    // Isotropic point source: emits into the full 4*pi steradian sphere.
    m_emissionSolidAngle = Utility::pi * 4.0;
}

void OmniLight::innerRadius(double innerRadius)
{
    m_innerRadius = innerRadius;
}

double OmniLight::innerRadius() const
{
    return m_innerRadius;
}

void OmniLight::emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const
{
    // photonFlux is the count-independent per-photon weight (the light's total
    // luminous flux Phi, in lumens). No 1/count factor here — the single divide
    // by photon count happens once at image conversion.
    Color photonColor = m_color * static_cast<float>(photonFlux);

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
