#include "Light.h"

Light::Light()
    : Object()
{
    registerType<Light>();
    updateParameters();
}

void Light::color(const Color& color)
{
    m_color = color;
    updateParameters();
}

Color Light::color() const
{
    return m_color;
}

void Light::brightness(double brightness)
{
    m_brightness = brightness;
    updateParameters();
}

double Light::brightness() const
{
    return m_brightness;
}

void Light::emit(WorkQueue<Photon>::Block photonBlock, double photonBrightness, RandomGenerator& generator) const
{
}

void Light::updateParameters()
{
    m_lumens = m_brightness;

    if (m_area > 0.0)
    {
        m_lumens = m_brightness * m_area;
    }
}
