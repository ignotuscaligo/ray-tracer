#include "Light.h"

Light::Light()
    : Object()
{
    registerType<Light>();
}

void Light::color(const Color& color)
{
    m_color = color;
}

Color Light::color() const
{
    return m_color;
}

void Light::intensityCandela(double intensity)
{
    m_intensityCandela = intensity;
}

double Light::intensityCandela() const
{
    return m_intensityCandela;
}

void Light::brightness(double brightness)
{
    // Legacy alias: historical "$brightness" now maps directly onto luminous
    // intensity (candela). See Light.h for the unit-of-record rationale.
    m_intensityCandela = brightness;
}

double Light::brightness() const
{
    return m_intensityCandela;
}

double Light::luminousFlux() const
{
    // Phi = I * Omega. m_emissionSolidAngle is 4*pi for an isotropic source and
    // the cone/disc extent for directional lights.
    return m_intensityCandela * m_emissionSolidAngle;
}

void Light::emit(WorkQueue<Photon>::Block photonBlock, double photonFlux, RandomGenerator& generator) const
{
}
